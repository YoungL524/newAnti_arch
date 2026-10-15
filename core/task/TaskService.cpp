#include "core/task/TaskService.h"

#include "core/archive/SqliteArchiveRepository.h"
#include "core/config/DeviceConfigService.h"
#include "core/persistence/SqliteRuntimeRepository.h"
#include "core/model/StableIdRegistry.h"
#include "core/producer/RuntimeStateGate.h"
#include "core/producer/SystemEventPublisher.h"
#include "core/task/RadarControlSettingsJson.h"

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
    core::domain::LatestState<core::model::TaskState> &taskState,
    core::producer::SystemEventPublisher *eventPublisher)
    : m_runtimeRepository(runtimeRepository)
    , m_deviceConfigService(deviceConfigService)
    , m_archiveRepository(archiveRepository)
    , m_stableIds(stableIds)
    , m_taskState(taskState)
    , m_eventPublisher(eventPublisher)
{
}

/**
 * @brief 读取全部未软删除任务，为每项分配新 TaskId 并建立归档映射。
 * @details TaskId 仅在当前进程稳定，加载顺序按数据库 task_id 确定，重启后允许得到不同数值。
 * 加载完成后明确发布 None；历史任务台账不代表本进程已重新激活任务或重建采集会话。
 */
bool TaskService::loadFromDatabase()
{
    if (!m_databaseTaskIds.isEmpty() || !m_taskIdsByUuid.isEmpty()
        || !m_templateSettings.isEmpty()) {
        m_lastError = QStringLiteral("任务已经加载，禁止重复重建 TaskId");
        return false;
    }
    QVector<QUuid> templateUuids;
    QVector<QString> templateNames;
    QVector<RadarControlSettings> templateSettings;
    if (!m_runtimeRepository.loadControlTemplates(
            templateUuids, templateNames, templateSettings)
        || templateUuids.size() != templateNames.size()
        || templateUuids.size() != templateSettings.size()) {
        m_lastError = m_runtimeRepository.lastError();
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("任务模板集合不完整");
        return false;
    }
    for (qsizetype index = 0; index < templateUuids.size(); ++index) {
        if (templateUuids[index].isNull() || templateNames[index].trimmed().isEmpty()
            || m_templateSettings.contains(templateUuids[index])) {
            m_lastError = QStringLiteral("任务模板身份、名称或唯一性无效");
            return false;
        }
        m_templateNames.insert(templateUuids[index], templateNames[index]);
        m_templateSettings.insert(templateUuids[index], templateSettings[index]);
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
    if (!publishTaskState(core::model::TaskId(), core::model::TaskPhase::None)) return false;
    m_lastError.clear();
    return true;
}

/**
 * @brief 按模板UUID文本顺序复制三组低频数据，不向调用者暴露内部QHash。
 */
bool TaskService::listControlTemplates(
    QVector<QUuid> &templateUuids,
    QVector<QString> &names,
    QVector<RadarControlSettings> &settings) const
{
    QVector<QUuid> uuids = m_templateSettings.keys().toVector();
    std::sort(uuids.begin(), uuids.end(), [](const QUuid &left, const QUuid &right) {
        return left.toString(QUuid::WithoutBraces) < right.toString(QUuid::WithoutBraces);
    });
    QVector<QString> resultNames;
    QVector<RadarControlSettings> resultSettings;
    resultNames.reserve(uuids.size());
    resultSettings.reserve(uuids.size());
    for (const QUuid &uuid : uuids) {
        resultNames.push_back(m_templateNames.value(uuid));
        resultSettings.push_back(m_templateSettings.value(uuid));
    }
    templateUuids = std::move(uuids);
    names = std::move(resultNames);
    settings = std::move(resultSettings);
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
    const auto templateSettings = m_templateSettings.constFind(task.sourceTemplateUuid);
    if (task.sourceTemplateUuid.isNull() || templateSettings == m_templateSettings.cend()) {
        m_lastError = QStringLiteral("任务必须选择存在的参数模板");
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
    task.controlSettings = *templateSettings;
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
    publishTaskEvent(allocated, 1);
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
 * @details Ready只表达当前任务上下文已经选定；成功后由编排层准备0x0001快照，采集会话仍由开始采集创建。
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
    publishTaskEvent(*id, 2);
    m_lastError.clear();
    return true;
}

/**
 * @brief 激活后的设备快照准备失败时撤回 Ready 状态，使草稿可重新激活或修改设备配置。
 */
bool TaskService::cancelActivation(const QUuid &taskUuid)
{
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    const auto current = m_taskState.latest();
    if (id == m_taskIdsByUuid.cend() || !current
        || current->sample->value.taskId != *id
        || current->sample->value.phase != core::model::TaskPhase::Ready) {
        m_lastError = QStringLiteral("只能撤销当前尚未采集的Ready任务激活");
        return false;
    }
    if (!publishTaskState(core::model::TaskId(), core::model::TaskPhase::None)) return false;
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
    publishTaskEvent(*id, 3);
    m_lastError.clear();
    return true;
}

/**
 * @brief 在任务 Owner 内确认已完成且不活动，再由 SQLite 原子写入归档时间；历史航迹和事件不搬移。
 */
bool TaskService::archiveTask(const QUuid &taskUuid, TaskDefinition &task)
{
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    if (taskUuid.isNull() || id == m_taskIdsByUuid.cend()) {
        m_lastError = QStringLiteral("任务不存在或已经删除");
        return false;
    }
    const auto &currentTask = m_tasks.value(id->value());
    if (currentTask.completedUtcMs <= 0 || currentTask.archivedUtcMs > 0
        || isTaskActive(taskUuid)) {
        m_lastError = QStringLiteral("只能归档已完成且尚未归档的非活动任务");
        return false;
    }
    const qint64 archivedUtcMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_runtimeRepository.archiveTask(m_databaseTaskIds.value(id->value(), 0), archivedUtcMs)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    m_tasks[id->value()].archivedUtcMs = archivedUtcMs;
    task = m_tasks.value(id->value());
    publishTaskEvent(*id, 4);
    m_lastError.clear();
    return true;
}

/**
 * @brief 只允许当前 Ready 或 Running 活动任务改写参数快照；先过值域，再落库，成功后才替换内存值。
 * @details 任一步失败都不触碰内存快照、TaskState、模板或采集会话，保证失败时两份参数都保持旧值。
 */
bool TaskService::updateControlSettings(
    const QUuid &taskUuid,
    const RadarControlSettings &settings,
    core::model::TaskId &taskId,
    TaskDefinition &task)
{
    taskId = core::model::TaskId();
    const auto id = m_taskIdsByUuid.constFind(taskUuid);
    if (taskUuid.isNull() || id == m_taskIdsByUuid.cend()) {
        m_lastError = QStringLiteral("任务不存在或已经删除");
        return false;
    }
    const auto current = m_taskState.latest();
    if (!current || current->sample->value.taskId != *id
        || (current->sample->value.phase != core::model::TaskPhase::Ready
            && current->sample->value.phase != core::model::TaskPhase::Running)) {
        m_lastError = QStringLiteral("任务当前状态不允许修改控制参数");
        return false;
    }
    QString validationError;
    if (!validateRadarControlSettings(settings, validationError)) {
        m_lastError = QStringLiteral("任务控制参数无效：") + validationError;
        return false;
    }
    const qint64 databaseId = m_databaseTaskIds.value(id->value(), 0);
    if (databaseId <= 0) {
        m_lastError = QStringLiteral("任务数据库主键缺失");
        return false;
    }
    if (!m_runtimeRepository.updateTaskControlSettings(
            databaseId, settings, QDateTime::currentMSecsSinceEpoch())) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    m_tasks[id->value()].controlSettings = settings;
    taskId = *id;
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
 * @brief 校验完整阵面集合，预留不复用句柄，原子创建全部会话；身份绑定失败显式收口，清理失败置 Faulted。
 */
bool TaskService::startCollections(core::model::TaskId taskId, core::model::DeviceId deviceId,
    core::model::EndpointId endpointId, quint8 faceMask,
    std::array<core::model::AcquisitionId, 4> &acquisitionIds)
{
    acquisitionIds = {};
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
        || current->sample->value.phase != core::model::TaskPhase::Ready) {
        m_lastError = QStringLiteral("只能为当前Ready任务创建采集会话");
        return false;
    }
    const QUuid deviceUuid = m_stableIds.resolveDevice(deviceId);
    const auto &bindings = m_tasks.value(taskId.value()).deviceBindings;
    const bool belongsToTask = std::any_of(
        bindings.cbegin(), bindings.cend(), [&deviceUuid, faceMask](const TaskDeviceBinding &binding) {
            return !deviceUuid.isNull() && faceMask != 0U && (faceMask & 0xF0U) == 0U
                && binding.deviceUuid == deviceUuid
                && binding.faceMask == faceMask;
        });
    if (!belongsToTask) {
        m_lastError = QStringLiteral("设备或阵面不属于当前任务的冻结绑定");
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


    std::array<core::model::AcquisitionId, 4> allocated {};
    for (quint8 face = 0; face < 4; ++face) {
        if ((faceMask & (1U << face)) == 0) continue;
        allocated[face] = allocateAcquisitionId();
        if (!allocated[face].isSet()) return false;
    }
    std::array<QUuid, 4> uuids {};
    std::array<qint64, 4> ids {};
    if (!m_runtimeRepository.createCollectionSessions(*taskFound, deviceRevisionId, endpointKey, faceMask, uuids, ids)) {
        m_lastError = m_runtimeRepository.lastError(); return false;
    }
    QVector<core::model::AcquisitionId> created;
    for (quint8 face = 0; face < 4; ++face) if (allocated[face].isSet()) {
        m_collections.insert(allocated[face].value(), ActiveCollection {ids[face], taskId, face, true});
        created.append(allocated[face]);
    }
    bool bound = true;
    for (quint8 face = 0; face < 4; ++face) if (allocated[face].isSet()) {
        if (!m_archiveRepository.bindAcquisition(allocated[face], ids[face], deviceId, taskId, endpointId)
            || !m_stableIds.registerAcquisition(allocated[face], uuids[face])) {
            bound = false; m_lastError = QStringLiteral("采集会话归档或稳定身份绑定失败"); break;
        }
    }
    if (bound && publishTaskState(taskId, core::model::TaskPhase::Running)) {
        acquisitionIds = allocated; m_lastError.clear(); return true;
    }
    const QString firstError = m_lastError;
    if (!stopCollections(created, CollectionStopReason::Error, QDateTime::currentMSecsSinceEpoch())) {
        const QString cleanupError = m_lastError;
        publishTaskState(taskId, core::model::TaskPhase::Faulted);
        m_lastError = firstError + QStringLiteral("；采集回滚失败：") + cleanupError;
    } else m_lastError = firstError;
    return false;
}

/**
 * @brief 先校验所有活动句柄，再原子提交停止，成功后统一更新内存；晚到归档身份保持有效。
 */
bool TaskService::stopCollections(const QVector<core::model::AcquisitionId> &acquisitionIds,
    CollectionStopReason reason, qint64 stoppedUtcMs)
{
    QVector<qint64> ids;
    QSet<quint64> seen;
    core::model::TaskId taskId;
    for (const auto id : acquisitionIds) {
        const auto found = m_collections.constFind(id.value());
        if (!id.isSet() || seen.contains(id.value()) || found == m_collections.cend() || !found->active
            || (taskId.isSet() && taskId != found->taskId)) {
            m_lastError = QStringLiteral("停止集合包含无效、重复、已停止或跨任务句柄"); return false;
        }
        seen.insert(id.value()); ids.append(found->collectionSessionId); taskId = found->taskId;
    }
    if (!m_runtimeRepository.stopCollectionSessions(ids, static_cast<quint8>(reason), stoppedUtcMs)) {
        const QString failure = m_runtimeRepository.lastError();
        if (reason == CollectionStopReason::Error && taskId.isSet())
            publishTaskState(taskId, core::model::TaskPhase::Faulted);
        m_lastError = failure; return false;
    }
    for (const auto id : acquisitionIds) m_collections[id.value()].active = false;
    const bool running = std::any_of(m_collections.cbegin(), m_collections.cend(),
        [taskId](const ActiveCollection &collection) { return collection.active && collection.taskId == taskId; });
    if (!running && !publishTaskState(taskId, core::model::TaskPhase::Ready)) return false;
    m_lastError.clear(); return true;
}

/**
 * @brief Producer 停止后收口当前 run 全部活动会话；空集合成功，失败保留活动位供异常恢复。
 */
bool TaskService::stopAllCollections(qint64 stoppedUtcMs)
{
    QVector<core::model::AcquisitionId> active;
    for (auto it = m_collections.cbegin(); it != m_collections.cend(); ++it)
        if (it->active) active.append(core::model::AcquisitionId::fromRaw(it.key()));
    if (active.isEmpty()) { m_lastError.clear(); return true; }
    return stopCollections(active, CollectionStopReason::ProcessShutdown, stoppedUtcMs);
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

/**
 * @brief 任务操作成功后写入统一事件日志；Web 与 SQLite 归档通过各自 cursor 读取同一事实。
 * @details 事件入口可空只用于隔离调用者；运行时入口非空，发布拒绝不改变已经提交的任务状态。
 */
void TaskService::publishTaskEvent(core::model::TaskId taskId, qint64 stage)
{
    if (!m_eventPublisher) return;
    core::model::Sample<core::model::SystemEvent> sample;
    sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    sample.meta.receiveTimeSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    sample.meta.traceId = m_nextStateTraceId++;
    if (m_nextStateTraceId == 0) m_nextStateTraceId = 1;
    sample.value.domain = core::model::EventDomain::Control;
    sample.value.severity = core::model::EventSeverity::Info;
    sample.value.eventCode = core::model::EventCode::ControlTaskLifecycleChanged;
    sample.value.taskId = taskId;
    sample.value.argument0 = stage;
    sample.value.flags = core::model::eventSubjectBit(core::model::EventSubjectFlag::TaskIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument0Valid);
    try {
        m_eventPublisher->publish(std::move(sample));
    } catch (const std::overflow_error &) {
        // 已提交的任务事实不能因事件序号耗尽而回滚；后续操作仍以 SQLite 结果为准。
    }
}

} // namespace core::task
