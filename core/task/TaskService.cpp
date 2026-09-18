#include "core/task/TaskService.h"

#include "core/archive/SqliteArchiveRepository.h"
#include "core/config/DeviceConfigService.h"
#include "core/persistence/SqliteRuntimeRepository.h"
#include "core/model/StableIdRegistry.h"
#include "core/producer/RuntimeStateGate.h"

#include <QDateTime>
#include <QVector>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <limits>

namespace core::task {

/**
 * @brief 只绑定三个职责明确的协作者，不加载任务或创建会话。
 */
TaskService::TaskService(
    core::persistence::SqliteRuntimeRepository &runtimeRepository,
    core::config::DeviceConfigService &deviceConfigService,
    core::archive::SqliteArchiveRepository &archiveRepository,
    core::model::StableIdRegistry &stableIds,
    core::domain::LatestState<core::model::TaskState> &taskState)
    : m_runtimeRepository(runtimeRepository)
    , m_deviceConfigService(deviceConfigService)
    , m_archiveRepository(archiveRepository)
    , m_stableIds(stableIds)
    , m_taskState(taskState)
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
        if (!m_archiveRepository.bindTask(taskId, databaseId)
            || !m_stableIds.registerTask(taskId, tasks[index].taskUuid)) {
            m_lastError = m_archiveRepository.lastError();
            if (m_lastError.isEmpty()) m_lastError = QStringLiteral("稳定任务身份登记冲突");
            return false;
        }
        m_databaseTaskIds.insert(taskId.value(), databaseId);
        m_taskIdsByUuid.insert(tasks[index].taskUuid, taskId);
        m_tasks.insert(taskId.value(), tasks[index]);
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
    if (task.deviceBindings.isEmpty()) {
        m_lastError = QStringLiteral("任务必须绑定至少一台设备");
        return false;
    }
    QSet<QUuid> uniqueDevices;
    for (const TaskDeviceBinding &binding : task.deviceBindings) {
        if (uniqueDevices.contains(binding.deviceUuid)
            || !m_deviceConfigService.acceptsTaskBinding(
                binding.deviceUuid, task.radarArrayType, binding.faceMask)) {
            m_lastError = QStringLiteral("任务设备重复、阵面形态不符或阵面位图无效");
            return false;
        }
        uniqueDevices.insert(binding.deviceUuid);
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
    if (!m_archiveRepository.bindTask(allocated, databaseId)
        || !m_stableIds.registerTask(allocated, task.taskUuid)) {
        m_lastError = m_archiveRepository.lastError();
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("稳定任务身份登记冲突");
        return false;
    }
    m_databaseTaskIds.insert(allocated.value(), databaseId);
    m_taskIdsByUuid.insert(task.taskUuid, allocated);
    m_tasks.insert(allocated.value(), task);
    taskId = allocated;
    m_lastError.clear();
    return true;
}

/**
 * @brief 按TaskId顺序复制低频任务元数据，不暴露内部可变容器。
 */
bool TaskService::listTasks(QVector<TaskDefinition> &tasks) const
{
    QVector<quint32> ids = m_tasks.keys().toVector();
    std::sort(ids.begin(), ids.end());
    QVector<TaskDefinition> result;
    result.reserve(ids.size());
    for (quint32 id : ids) result.push_back(m_tasks.value(id));
    tasks = std::move(result);
    return true;
}

/**
 * @brief 在存储线程内把已持久化任务从Draft选为Ready，成功发布后才向HTTP返回活动结果。
 * @details Ready只表达当前任务上下文已经选定；设备命令、ACK和collection_sessions仍留给后续正式控制链。
 */
bool TaskService::activateTask(const QUuid &taskUuid, TaskDefinition &task)
{
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    if (taskUuid.isNull() || id == m_taskIdsByUuid.cend()) {
        m_lastError = QStringLiteral("任务不存在或已经删除");
        return false;
    }
    if (m_tasks.value(id->value()).completedUtcMs > 0) {
        m_lastError = QStringLiteral("已完成任务不能再次直接激活");
        return false;
    }
    const auto current = m_taskState.latest();
    if (current && current->sample->value.phase != core::model::TaskPhase::None) {
        m_lastError = current->sample->value.taskId == *id
            ? QStringLiteral("任务已经激活") : QStringLiteral("已有其他活动任务");
        return false;
    }

    if (!publishTaskState(*id, core::model::TaskPhase::Ready)) return false;
    task = m_tasks.value(id->value());
    m_lastError.clear();
    return true;
}

/**
 * @brief 确认目标就是当前任务且没有活动会话后，先持久化完成事实，再发布Completed与None。
 * @details 连续两个快照允许Reader观察完成沿；最终None解除“同一时刻一个活动任务”的创建门禁。
 */
bool TaskService::completeTask(const QUuid &taskUuid, TaskDefinition &task)
{
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    const auto current = m_taskState.latest();
    if (id == m_taskIdsByUuid.cend() || !current
        || current->sample->value.taskId != *id
        || current->sample->value.phase != core::model::TaskPhase::Ready) {
        m_lastError = QStringLiteral("只能完成当前Ready任务");
        return false;
    }
    for (auto collection = m_collections.cbegin(); collection != m_collections.cend(); ++collection) {
        if (collection->active && collection->taskId == *id) {
            m_lastError = QStringLiteral("任务仍有活动采集，必须先完成设备停止");
            return false;
        }
    }
    const qint64 completedUtcMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_runtimeRepository.completeTask(m_databaseTaskIds.value(id->value(), 0), completedUtcMs)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    m_tasks[id->value()].completedUtcMs = completedUtcMs;
    if (!publishTaskState(*id, core::model::TaskPhase::Completed)
        || !publishTaskState(core::model::TaskId(), core::model::TaskPhase::None)) {
        return false;
    }
    task = m_tasks.value(id->value());
    m_lastError.clear();
    return true;
}

/**
 * @brief 组合LatestState中的紧凑TaskId与TaskService拥有的稳定任务台账，不复制设备运行状态。
 */
bool TaskService::activeTask(TaskDefinition &task) const
{
    const auto current = m_taskState.latest();
    if (!current || current->sample->value.phase == core::model::TaskPhase::None) return false;
    const auto found = m_tasks.constFind(current->sample->value.taskId.value());
    if (found == m_tasks.cend()) return false;
    task = *found;
    return true;
}

/**
 * @brief 通过稳定UUID解析TaskId后与唯一TaskState快照比较，避免HTTP层保存第二份活动标志。
 */
bool TaskService::isTaskActive(const QUuid &taskUuid) const
{
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    const auto current = m_taskState.latest();
    return id != m_taskIdsByUuid.cend() && current
        && current->sample->value.phase != core::model::TaskPhase::None
        && current->sample->value.taskId == *id;
}

/**
 * @brief 在确认不存在活动采集后先提交SQLite软删除，再移除当前可选任务映射。
 */
bool TaskService::softDeleteTask(const QUuid &taskUuid, const QString &deletedBy)
{
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    if (taskUuid.isNull() || id == m_taskIdsByUuid.cend()) {
        m_lastError = QStringLiteral("任务不存在或已经删除");
        return false;
    }
    if (isTaskActive(taskUuid)) {
        m_lastError = QStringLiteral("当前活动任务不能删除");
        return false;
    }
    for (auto collection = m_collections.cbegin(); collection != m_collections.cend(); ++collection) {
        if (collection->active && collection->taskId == *id) {
            m_lastError = QStringLiteral("活动采集任务不能删除");
            return false;
        }
    }
    const qint64 databaseId = m_databaseTaskIds.value(id->value(), 0);
    if (!m_runtimeRepository.softDeleteTask(databaseId, deletedBy)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    m_tasks.remove(id->value());
    m_databaseTaskIds.remove(id->value());
    m_taskIdsByUuid.erase(id);
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
    if (m_tasks.value(taskId.value()).completedUtcMs > 0) {
        m_lastError = QStringLiteral("已完成任务不能创建新采集会话");
        return false;
    }
    const auto current = m_taskState.latest();
    if (!current || current->sample->value.taskId != taskId
        || (current->sample->value.phase != core::model::TaskPhase::Ready
            && current->sample->value.phase != core::model::TaskPhase::Starting)) {
        m_lastError = QStringLiteral("只能为当前Ready或Starting任务创建采集会话");
        return false;
    }
    const QUuid deviceUuid = m_stableIds.resolveDevice(deviceId);
    const auto &bindings = m_tasks.value(taskId.value()).deviceBindings;
    const bool belongsToTask = std::any_of(
        bindings.cbegin(), bindings.cend(), [&deviceUuid](const TaskDeviceBinding &binding) {
            return !deviceUuid.isNull() && binding.deviceUuid == deviceUuid;
        });
    if (!belongsToTask) {
        m_lastError = QStringLiteral("设备不属于当前任务的冻结绑定");
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
            allocated, collectionSessionId, deviceId, taskId, endpointId)
        || !m_stableIds.registerAcquisition(allocated, sessionUuid)) {
        m_lastError = m_archiveRepository.lastError();
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("稳定采集会话身份登记冲突");
        return false;
    }
    m_collections.insert(allocated.value(), ActiveCollection {collectionSessionId, taskId, true});
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

/**
 * @brief 使用本地UTC与steady时钟形成状态元数据，门禁通过后原子替换LatestState。
 */
bool TaskService::publishTaskState(core::model::TaskId taskId, core::model::TaskPhase phase)
{
    core::model::Sample<core::model::TaskState> sample;
    sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    sample.meta.receiveTimeSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    sample.meta.traceId = m_nextStateTraceId++;
    if (m_nextStateTraceId == 0) m_nextStateTraceId = 1;
    sample.value.taskId = taskId;
    sample.value.phase = phase;
    if (core::producer::RuntimeStateGate::validateTaskState(sample)
        != core::producer::RuntimeStateRejection::None) {
        m_lastError = QStringLiteral("任务状态快照未通过运行状态门禁");
        return false;
    }
    try {
        m_taskState.set(std::move(sample));
    } catch (const std::overflow_error &) {
        m_lastError = QStringLiteral("任务状态revision已经耗尽");
        return false;
    }
    return true;
}

} // namespace core::task
