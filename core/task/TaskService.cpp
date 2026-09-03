#include "core/task/TaskService.h"

#include "core/archive/SqliteArchiveRepository.h"
#include "core/config/DeviceConfigService.h"
#include "core/persistence/SqliteRuntimeRepository.h"

#include <QVector>

#include <limits>

namespace core::task {

/**
 * @brief 只绑定三个职责明确的协作者，不加载任务或创建会话。
 */
TaskService::TaskService(
    core::persistence::SqliteRuntimeRepository &runtimeRepository,
    const core::config::DeviceConfigService &deviceConfigService,
    core::archive::SqliteArchiveRepository &archiveRepository)
    : m_runtimeRepository(runtimeRepository)
    , m_deviceConfigService(deviceConfigService)
    , m_archiveRepository(archiveRepository)
{
}

/**
 * @brief 读取全部未软删除任务，为每项分配新 TaskId 并建立归档映射。
 * @details TaskId 仅在当前进程稳定，加载顺序按数据库 task_id 确定，重启后允许得到不同数值。
 */
bool TaskService::loadFromDatabase()
{
    if (!m_databaseTaskIds.isEmpty() || !m_taskIdsByUuid.isEmpty()) {
        m_lastError = QStringLiteral("任务已经加载，禁止重复重建 TaskId");
        return false;
    }
    QVector<TaskDefinition> tasks;
    QVector<qint64> databaseIds;
    if (!m_runtimeRepository.loadActiveTasks(tasks, databaseIds)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    if (tasks.size() != databaseIds.size()) {
        m_lastError = QStringLiteral("任务集合与数据库主键数量不一致");
        return false;
    }
    for (qsizetype index = 0; index < databaseIds.size(); ++index) {
        const qint64 databaseId = databaseIds[index];
        const auto taskId = allocateTaskId();
        if (!taskId.isSet()) {
            return false;
        }
        if (!m_archiveRepository.bindTask(taskId, databaseId)) {
            m_lastError = m_archiveRepository.lastError();
            return false;
        }
        m_databaseTaskIds.insert(taskId.value(), databaseId);
        m_taskIdsByUuid.insert(tasks[index].taskUuid, taskId);
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 拒绝空名称，提交 tasks 行后才分配 TaskId，避免数据库失败产生可见幽灵任务。
 */
bool TaskService::createTask(TaskDefinition &task, core::model::TaskId &taskId)
{
    taskId = core::model::TaskId();
    if (task.taskName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("任务名称不能为空");
        return false;
    }
    qint64 databaseId = 0;
    if (!m_runtimeRepository.createTask(task, databaseId)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    const auto allocated = allocateTaskId();
    if (!allocated.isSet()) {
        return false;
    }
    if (!m_archiveRepository.bindTask(allocated, databaseId)) {
        m_lastError = m_archiveRepository.lastError();
        return false;
    }
    m_databaseTaskIds.insert(allocated.value(), databaseId);
    m_taskIdsByUuid.insert(task.taskUuid, allocated);
    taskId = allocated;
    m_lastError.clear();
    return true;
}

/**
 * @brief 只查询 TaskService 重建的 UUID 映射，不把数据库主键暴露给 UI 或 Producer。
 */
bool TaskService::resolveTask(const QUuid &taskUuid, core::model::TaskId &taskId) const
{
    taskId = core::model::TaskId();
    const auto found = m_taskIdsByUuid.constFind(taskUuid);
    if (taskUuid.isNull() || found == m_taskIdsByUuid.cend()) {
        return false;
    }
    taskId = *found;
    return true;
}

/**
 * @brief 验证任务和设备配置后先创建 SQLite 会话，再分配 AcquisitionId 并冻结归档上下文。
 * @details protocolAdapterKey 为空时明确拒绝启动，因此当前可以搭好架构而不会假装未知协议已经可用。
 */
bool TaskService::startCollection(
    core::model::TaskId taskId,
    core::model::DeviceId deviceId,
    core::model::EndpointId endpointId,
    core::model::AcquisitionId &acquisitionId)
{
    acquisitionId = core::model::AcquisitionId();
    const auto taskFound = m_databaseTaskIds.constFind(taskId.value());
    if (!taskId.isSet() || taskFound == m_databaseTaskIds.cend()) {
        m_lastError = QStringLiteral("TaskId 未由当前 TaskService 创建或加载");
        return false;
    }
    if (!m_deviceConfigService.isCollectionReady(deviceId, endpointId)) {
        m_lastError = QStringLiteral("设备、航迹端点或协议适配器配置尚未就绪");
        return false;
    }
    qint64 deviceRevisionId = 0;
    QString endpointKey;
    if (!m_deviceConfigService.resolveDeviceRevision(deviceId, deviceRevisionId)
        || !m_deviceConfigService.resolveTrackEndpoint(deviceId, endpointId, endpointKey)) {
        m_lastError = QStringLiteral("设备 revision 与端点身份解析失败");
        return false;
    }

    QUuid sessionUuid;
    qint64 collectionSessionId = 0;
    if (!m_runtimeRepository.createCollectionSession(
            *taskFound, deviceRevisionId, endpointKey, sessionUuid, collectionSessionId)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    const auto allocated = allocateAcquisitionId();
    if (!allocated.isSet()) {
        return false;
    }
    if (!m_archiveRepository.bindAcquisition(
            allocated, collectionSessionId, deviceId, taskId, endpointId)) {
        m_lastError = m_archiveRepository.lastError();
        return false;
    }
    m_collections.insert(allocated.value(), ActiveCollection {collectionSessionId, true});
    acquisitionId = allocated;
    m_lastError.clear();
    return true;
}

/**
 * @brief 数据库成功收口后再把内存活动位改为 false，失败时 Producer 仍可按原状态处置。
 * @details 归档身份映射不会删除，使停止前已经进入 Topic、但尚未提交 SQLite 的记录仍能解析会话。
 */
bool TaskService::stopCollection(
    core::model::AcquisitionId acquisitionId,
    CollectionStopReason reason,
    qint64 stoppedUtcMs)
{
    auto found = m_collections.find(acquisitionId.value());
    const quint8 reasonValue = static_cast<quint8>(reason);
    if (!acquisitionId.isSet() || found == m_collections.end() || !found->active
        || reasonValue < 1 || reasonValue > 4 || stoppedUtcMs <= 0) {
        m_lastError = QStringLiteral("AcquisitionId、停止原因或停止时间无效");
        return false;
    }
    if (!m_runtimeRepository.stopCollectionSession(
            found->collectionSessionId, reasonValue, stoppedUtcMs)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    found->active = false;
    m_lastError.clear();
    return true;
}

/**
 * @brief 只读取 TaskService 自身拥有的活动位，不查询数据库或推断设备状态。
 */
bool TaskService::isCollectionActive(core::model::AcquisitionId acquisitionId) const
{
    const auto found = m_collections.constFind(acquisitionId.value());
    return acquisitionId.isSet() && found != m_collections.cend() && found->active;
}

/**
 * @brief 返回最近失败文本的值副本，便于上层转换为 CommandResult 或 SystemEvent。
 */
QString TaskService::lastError() const
{
    return m_lastError;
}

/**
 * @brief 单调分配 32 位 TaskId，不回收以避免晚到事实误指新任务。
 */
core::model::TaskId TaskService::allocateTaskId()
{
    if (m_nextTaskId == 0) {
        m_lastError = QStringLiteral("TaskId 运行时编号已经耗尽");
        return core::model::TaskId();
    }
    const quint32 value = m_nextTaskId;
    m_nextTaskId = value == std::numeric_limits<quint32>::max() ? 0 : value + 1;
    return core::model::TaskId::fromRaw(value);
}

/**
 * @brief 单调分配 64 位 AcquisitionId；停止、断线和重新启动都必须获得新值。
 */
core::model::AcquisitionId TaskService::allocateAcquisitionId()
{
    if (m_nextAcquisitionId == 0) {
        m_lastError = QStringLiteral("AcquisitionId 运行时编号已经耗尽");
        return core::model::AcquisitionId();
    }
    const quint64 value = m_nextAcquisitionId;
    m_nextAcquisitionId = value == std::numeric_limits<quint64>::max() ? 0 : value + 1;
    return core::model::AcquisitionId::fromRaw(value);
}

} // namespace core::task
