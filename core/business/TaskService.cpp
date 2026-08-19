#include "TaskService.h"
#include "DeviceRegistry.h"
#include "DataStore.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QVariantMap>

/**
 * @brief 把任务及其设备成员列表转换为 Web API JSON，时间保持 Unix 毫秒。
 */
QJsonObject task::TaskInfo::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("id")] = id;
    j[QStringLiteral("name")] = name;
    j[QStringLiteral("description")] = description;
    j[QStringLiteral("operatorName")] = operatorName;
    j[QStringLiteral("status")] = status;
    QJsonArray devArr;
    for (const auto& d : deviceIds)
        devArr.append(d);
    j[QStringLiteral("deviceIds")] = devArr;
    j[QStringLiteral("createdAt")] = createdAt;
    if (activatedAt > 0) j[QStringLiteral("activatedAt")] = activatedAt;
    if (completedAt > 0) j[QStringLiteral("completedAt")] = completedAt;
    if (archivedAt > 0) j[QStringLiteral("archivedAt")] = archivedAt;
    return j;
}

/**
 * @brief 从创建/更新请求提取任务字段；服务层随后负责 ID、状态和设备有效性校验。
 */
task::TaskInfo task::TaskInfo::fromJson(const QJsonObject& body)
{
    task::TaskInfo t;
    t.id            = body[QStringLiteral("id")].toString();
    t.name          = body[QStringLiteral("name")].toString();
    t.description   = body[QStringLiteral("description")].toString();
    t.operatorName  = body[QStringLiteral("operatorName")].toString();
    t.status        = body[QStringLiteral("status")].toString(task::kStatusDraft);
    for (const auto& v : body[QStringLiteral("deviceIds")].toArray())
        t.deviceIds.append(v.toString());
    return t;
}

/**
 * @brief 保存非拥有数据库和设备台账依赖；二者必须覆盖服务生命周期。
 */
TaskService::TaskService(DataStore* store, DeviceRegistry* deviceRegistry)
    : m_store(store)
    , m_deviceReg(deviceRegistry)
{
}

/**
 * @brief 生成 task_ 前缀 UUID，避免任务复制/重跑依赖数据库自增键。
 */
QString TaskService::generateId()
{
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    const uint32_t rand = QRandomGenerator::global()->bounded(10000u);
    return QStringLiteral("task_%1_%2").arg(ms).arg(rand);
}

/**
 * @brief 在新操作开始前清除上一次错误，防止成功响应携带陈旧诊断。
 */
void TaskService::clearError() { m_lastError.clear(); }
/**
 * @brief 保存最近失败原因，供 REST 层形成操作员可读响应。
 */
void TaskService::setError(const QString& msg) { m_lastError = msg; }

/**
 * @brief 判断状态是否已结束运行、可用于重跑或归档，避免散落字符串判断。
 */
bool TaskService::isTerminalStatus(const QString& status) const
{
    return status == QString::fromUtf8(task::kStatusCompleted)
        || status == QString::fromUtf8(task::kStatusArchived);
}

/**
 * @brief 创建任务、任务设备关系、任务事件和任务日志表及必要索引。
 */
bool TaskService::ensureSchema()
{
    if (!m_store) return false;
    const bool postgres = m_store->database().driverName().startsWith(
        QStringLiteral("QPSQL"));
    const QString tasksSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  operator_name TEXT,"
        "  status TEXT NOT NULL,"
        "  created_at BIGINT NOT NULL,"
        "  activated_at BIGINT,"
        "  completed_at BIGINT,"
        "  archived_at BIGINT"
        ")");
    const QString taskDevicesSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS task_devices ("
        "  task_id TEXT NOT NULL,"
        "  device_id TEXT NOT NULL,"
        "  PRIMARY KEY(task_id, device_id),"
        "  FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE"
        ")");
    const QString taskLogsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS task_logs ("
        "  id %1,"
        "  task_id TEXT NOT NULL,"
        "  device_id TEXT,"
        "  ts_ms BIGINT NOT NULL,"
        "  level TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE"
        ")").arg(postgres
                      ? QStringLiteral("BIGSERIAL PRIMARY KEY")
                      : QStringLiteral("INTEGER PRIMARY KEY AUTOINCREMENT"));
    if (!m_store->execute(tasksSql)
        || !m_store->execute(taskDevicesSql)
        || !m_store->execute(taskLogsSql)) {
        return false;
    }
    if (postgres
        && (!m_store->execute(QStringLiteral(
                "ALTER TABLE tasks ALTER COLUMN created_at TYPE BIGINT, "
                "ALTER COLUMN activated_at TYPE BIGINT, "
                "ALTER COLUMN completed_at TYPE BIGINT, "
                "ALTER COLUMN archived_at TYPE BIGINT"))
            || !m_store->execute(QStringLiteral(
                "ALTER TABLE task_logs ALTER COLUMN ts_ms TYPE BIGINT")))) {
        return false;
    }

    const auto activeRows = m_store->query(
        QStringLiteral("SELECT id FROM tasks WHERE status = ? "
                       "ORDER BY activated_at DESC, created_at DESC"),
        {QString::fromUtf8(task::kStatusActive)});
    if (activeRows.size() > 1) {
        const QString keepId = activeRows.first().value(QStringLiteral("id")).toString();
        qWarning() << "[TaskService] 检测到多个活动任务，仅保留" << keepId;
        if (!m_store->execute(
                QStringLiteral("UPDATE tasks SET status = ?, completed_at = ? "
                               "WHERE status = ? AND id != ?"),
                {QString::fromUtf8(task::kStatusCompleted),
                 QDateTime::currentMSecsSinceEpoch(),
                 QString::fromUtf8(task::kStatusActive), keepId})) {
            return false;
        }
    }

    return m_store->execute(QStringLiteral(
               "CREATE INDEX IF NOT EXISTS idx_task_logs_task_ts ON task_logs(task_id, ts_ms)"))
        && m_store->execute(QStringLiteral(
               "CREATE UNIQUE INDEX IF NOT EXISTS idx_tasks_one_active "
               "ON tasks(status) WHERE status = 'Active'"));
}

/**
 * @brief 把任务主表行和关联设备查询组合为完整 TaskInfo。
 */
task::TaskInfo TaskService::rowToTask(const QVariantMap& row) const
{
    task::TaskInfo t;
    t.id           = row.value(QStringLiteral("id")).toString();
    t.name         = row.value(QStringLiteral("name")).toString();
    t.description  = row.value(QStringLiteral("description")).toString();
    t.operatorName = row.value(QStringLiteral("operator_name")).toString();
    t.status       = row.value(QStringLiteral("status")).toString();
    t.createdAt    = row.value(QStringLiteral("created_at")).toLongLong();
    t.activatedAt  = row.value(QStringLiteral("activated_at")).toLongLong();
    t.completedAt  = row.value(QStringLiteral("completed_at")).toLongLong();
    t.archivedAt   = row.value(QStringLiteral("archived_at")).toLongLong();

    if (m_store) {
        auto devRows = m_store->query(
            QStringLiteral("SELECT device_id FROM task_devices WHERE task_id = ? ORDER BY device_id"),
            {t.id});
        for (const auto& dr : devRows)
            t.deviceIds.append(dr.value(QStringLiteral("device_id")).toString());
    }
    return t;
}

/**
 * @brief 按更新时间查询任务列表，并为每项补齐设备成员关系。
 */
QVector<task::TaskInfo> TaskService::list() const
{
    QVector<task::TaskInfo> result;
    if (!m_store) return result;
    auto rows = m_store->query(QStringLiteral("SELECT * FROM tasks ORDER BY created_at DESC"));
    for (const auto& row : rows)
        result.append(rowToTask(row));
    return result;
}

/**
 * @brief 按 ID 查询任务主记录和设备成员；不存在时返回空 optional。
 */
std::optional<task::TaskInfo> TaskService::get(const QString& id) const
{
    if (!m_store) return std::nullopt;
    auto rows = m_store->query(QStringLiteral("SELECT * FROM tasks WHERE id = ?"), {id});
    if (rows.isEmpty()) return std::nullopt;
    return rowToTask(rows.first());
}

/**
 * @brief 查询数据库中唯一运行任务；发现多条时拒绝任选并记录一致性错误。
 */
std::optional<task::TaskInfo> TaskService::activeTask() const
{
    if (!m_store) return std::nullopt;
    auto rows = m_store->query(
        QStringLiteral("SELECT * FROM tasks WHERE status = ? ORDER BY activated_at DESC LIMIT 1"),
        {QString::fromUtf8(task::kStatusActive)});
    if (rows.isEmpty()) return std::nullopt;
    return rowToTask(rows.first());
}

/**
 * @brief 返回内存活动任务快照，供航迹热路径避免每包查询数据库。
 */
std::optional<task::ActiveTaskContext> TaskService::getActiveTask() const
{
    if (m_activeTaskCache.taskId.isEmpty())
        return std::nullopt;
    task::ActiveTaskContext ctx;
    ctx.id = m_activeTaskCache.taskId;
    for (const auto& devId : m_activeTaskCache.deviceIds)
        ctx.deviceIds.append(devId);
    return ctx;
}

/**
 * @brief 在缓存成员集合判断设备是否属于当前运行任务。
 */
bool TaskService::isDeviceInActiveTask(const QString& deviceId) const
{
    if (deviceId.isEmpty() || m_activeTaskCache.taskId.isEmpty())
        return false;
    return m_activeTaskCache.deviceIds.contains(deviceId);
}

/**
 * @brief 从活动任务缓存解析设备归属；不属于活动任务时返回空字符串。
 */
QString TaskService::resolveTaskIdForDevice(const QString& deviceId) const
{
    if (deviceId.isEmpty() || m_activeTaskCache.taskId.isEmpty())
        return {};
    if (m_activeTaskCache.deviceIds.contains(deviceId))
        return m_activeTaskCache.taskId;
    return {};
}

/**
 * @brief 提供与 resolveTaskIdForDevice 相同的显式活动任务语义，供调用点自解释。
 */
QString TaskService::resolveActiveTaskIdForDevice(const QString& deviceId) const
{
    return resolveTaskIdForDevice(deviceId);
}

/**
 * @brief 从数据库重建活动任务 ID 和设备集合；无活动任务时原子语义上清空两者。
 */
void TaskService::refreshActiveTaskCache()
{
    m_activeTaskCache = {};
    if (!m_store)
        return;

    auto rows = m_store->query(
        QStringLiteral("SELECT id FROM tasks WHERE status = ? ORDER BY activated_at DESC LIMIT 1"),
        {QString::fromUtf8(task::kStatusActive)});
    if (rows.isEmpty())
        return;

    m_activeTaskCache.taskId = rows.first().value(QStringLiteral("id")).toString();
    auto devRows = m_store->query(
        QStringLiteral("SELECT device_id FROM task_devices WHERE task_id = ?"),
        {m_activeTaskCache.taskId});
    for (const auto& dr : devRows)
        m_activeTaskCache.deviceIds.insert(dr.value(QStringLiteral("device_id")).toString());

    if (!m_activeTaskCache.taskId.isEmpty()) {
        qDebug() << "[TaskService] refreshActiveTaskCache active=" << m_activeTaskCache.taskId
                 << "devices=" << m_activeTaskCache.deviceIds.values();
    }
}

/**
 * @brief 去重检查任务设备 ID，并确认每台设备都存在于台账。
 */
bool TaskService::validateDeviceIds(const QStringList& deviceIds)
{
    if (!m_deviceReg) return true;
    for (const auto& devId : deviceIds) {
        if (devId.isEmpty()) continue;
        if (!m_deviceReg->get(devId).has_value()) {
            setError(QStringLiteral("设备不存在: %1").arg(devId));
            return false;
        }
    }
    return true;
}

/**
 * @brief 判断成员中是否至少有一台绑定通信端点的雷达，作为任务可启动条件。
 */
bool TaskService::hasControllableRadar(const QStringList& deviceIds) const
{
    if (!m_deviceReg)
        return false;
    for (const auto& devId : deviceIds) {
        const auto dev = m_deviceReg->get(devId);
        if (dev.has_value() && dev->kind == DeviceKind::Radar && !dev->endpointId.isEmpty())
            return true;
    }
    return false;
}

bool TaskService::appendTaskEvent(const QString& taskId,
                                  const QString& category,
                                  const QString& detail,
                                  const QString& deviceId)
{
    if (!m_store || taskId.isEmpty() || !m_store->tableExists(QStringLiteral("system_events")))
        return false;

    QVariantMap row;
    row.insert(QStringLiteral("ts_ms"), QDateTime::currentMSecsSinceEpoch());
    row.insert(QStringLiteral("category"), category);
    row.insert(QStringLiteral("source"), QStringLiteral("TaskService"));
    row.insert(QStringLiteral("detail"), detail);
    row.insert(QStringLiteral("task_id"), taskId);
    if (!deviceId.isEmpty())
        row.insert(QStringLiteral("device_id"), deviceId);
    return m_store->insert(QStringLiteral("system_events"), row);
}

bool TaskService::appendActiveTaskEvent(const QString& category,
                                        const QString& detail,
                                        const QString& deviceId)
{
    if (m_activeTaskCache.taskId.isEmpty())
        return false;
    return appendTaskEvent(m_activeTaskCache.taskId, category, detail, deviceId);
}

bool TaskService::appendTaskLog(const QString& taskId,
                                const QString& deviceId,
                                const QString& level,
                                const QString& message,
                                qint64 timestampMs)
{
    if (!m_store || taskId.isEmpty() || message.isEmpty())
        return false;

    QVariantMap row;
    row.insert(QStringLiteral("task_id"), taskId);
    if (!deviceId.isEmpty())
        row.insert(QStringLiteral("device_id"), deviceId);
    row.insert(QStringLiteral("ts_ms"), timestampMs > 0
        ? timestampMs : QDateTime::currentMSecsSinceEpoch());
    row.insert(QStringLiteral("level"), level);
    row.insert(QStringLiteral("message"), message);
    return m_store->insert(QStringLiteral("task_logs"), row);
}

/**
 * @brief 重建任务设备关联表；调用者负责把本操作放在更大的任务写事务中。
 */
bool TaskService::saveDeviceIds(const QString& taskId, const QStringList& deviceIds)
{
    if (!m_store) return false;
    if (!validateDeviceIds(deviceIds))
        return false;
    if (!m_store->execute(QStringLiteral("DELETE FROM task_devices WHERE task_id = ?"), {taskId}))
        return false;
    for (const auto& devId : deviceIds) {
        if (devId.isEmpty()) continue;
        if (!m_store->execute(
                QStringLiteral("INSERT INTO task_devices (task_id, device_id) VALUES (?, ?)"),
                {taskId, devId}))
            return false;
    }
    return true;
}

/**
 * @brief 校验名称和设备后，在事务中创建 Draft 任务、成员关系和创建事件。
 */
std::optional<task::TaskInfo> TaskService::create(task::TaskInfo task)
{
    clearError();
    if (!m_store || task.name.isEmpty()) {
        setError(QStringLiteral("name 不能为空"));
        return std::nullopt;
    }
    if (task.id.isEmpty())
        task.id = generateId();
    if (get(task.id).has_value()) {
        setError(QStringLiteral("任务 id 已存在"));
        return std::nullopt;
    }
    if (!validateDeviceIds(task.deviceIds))
        return std::nullopt;

    task.createdAt = QDateTime::currentMSecsSinceEpoch();
    task.status = QString::fromUtf8(task::kStatusDraft);

    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return std::nullopt;
    }

    bool ok = m_store->execute(
        QStringLiteral("INSERT INTO tasks (id, name, description, operator_name, status, created_at)"
                       " VALUES (?, ?, ?, ?, ?, ?)"),
        {task.id, task.name, task.description, task.operatorName, task.status, task.createdAt});
    if (ok)
        ok = saveDeviceIds(task.id, task.deviceIds);

    if (ok) {
        if (!m_store->commitTransaction()) {
            setError(QStringLiteral("事务提交失败"));
            m_store->rollbackTransaction();
            return std::nullopt;
        }
        auto created = get(task.id);
        appendTaskEvent(task.id, QStringLiteral("task"), QStringLiteral("任务已创建"));
        return created;
    }

    m_store->rollbackTransaction();
    setError(QStringLiteral("创建任务失败"));
    return std::nullopt;
}

/**
 * @brief 仅允许非运行任务更新元数据和成员，并在事务中记录变更事件。
 */
bool TaskService::update(const task::TaskInfo& task)
{
    clearError();
    if (!m_store || task.id.isEmpty()) return false;
    if (task.name.trimmed().isEmpty()) {
        setError(QStringLiteral("name 不能为空"));
        return false;
    }
    auto existing = get(task.id);
    if (!existing.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (isTerminalStatus(existing->status)) {
        setError(QStringLiteral("已完成或已归档任务的执行配置不可修改"));
        return false;
    }
    if (!validateDeviceIds(task.deviceIds))
        return false;
    if (existing->status == QString::fromUtf8(task::kStatusActive)
        && !hasControllableRadar(task.deviceIds)) {
        setError(QStringLiteral("活动任务必须至少保留一个已绑定端点的雷达设备"));
        return false;
    }

    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }

    bool ok = m_store->execute(
        QStringLiteral("UPDATE tasks SET name = ?, description = ?, operator_name = ? WHERE id = ?"),
        {task.name, task.description, task.operatorName, task.id});
    if (ok)
        ok = saveDeviceIds(task.id, task.deviceIds);

    if (ok && m_store->commitTransaction()) {
        refreshActiveTaskCache();
        return true;
    }

    m_store->rollbackTransaction();
    if (m_lastError.isEmpty())
        setError(QStringLiteral("更新任务失败"));
    return false;
}

/**
 * @brief 单独替换非运行任务的设备集合，并刷新相关审计事件。
 */
bool TaskService::updateDevices(const QString& id, const QStringList& deviceIds)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (isTerminalStatus(t->status)) {
        setError(QStringLiteral("已完成或已归档任务的执行配置不可修改"));
        return false;
    }
    if (t->status == QString::fromUtf8(task::kStatusActive)
        && !hasControllableRadar(deviceIds)) {
        setError(QStringLiteral("活动任务必须至少保留一个已绑定端点的雷达设备"));
        return false;
    }
    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }
    const bool ok = saveDeviceIds(id, deviceIds);
    if (ok && m_store->commitTransaction()) {
        refreshActiveTaskCache();
        return true;
    }
    m_store->rollbackTransaction();
    if (m_lastError.isEmpty())
        setError(QStringLiteral("更新任务设备失败"));
    return false;
}

/**
 * @brief 在不存在其他运行任务且包含可控雷达时，把任务切换为 Running 并刷新热缓存。
 */
bool TaskService::activate(const QString& id)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (t->status != QString::fromUtf8(task::kStatusDraft)) {
        setError(QStringLiteral("仅草稿任务可以激活"));
        return false;
    }

    if (!hasControllableRadar(t->deviceIds)) {
        setError(QStringLiteral("激活任务前请至少绑定一个已绑定端点的雷达设备"));
        return false;
    }

    const auto previousActive = activeTask();
    if (previousActive.has_value() && previousActive->id != id) {
        setError(QStringLiteral("已有进行中的任务，请先完成当前任务"));
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }

    const bool ok = m_store->execute(
        QStringLiteral("UPDATE tasks SET status = ?, activated_at = ?, completed_at = NULL WHERE id = ?"),
        {QString::fromUtf8(task::kStatusActive), now, id});

    if (ok && m_store->commitTransaction()) {
        refreshActiveTaskCache();
        appendTaskEvent(id, QStringLiteral("task"), QStringLiteral("任务已激活"));
        qDebug() << "[TaskService] task activated:" << id
                 << "devices:" << m_activeTaskCache.deviceIds.values();
        return true;
    }

    m_store->rollbackTransaction();
    setError(QStringLiteral("激活任务失败"));
    return false;
}

/**
 * @brief 从终态任务复制配置和设备到新 ID 的 Draft 任务，保留旧任务历史不变。
 */
std::optional<task::TaskInfo> TaskService::rerun(const QString& id)
{
    clearError();
    const auto source = get(id);
    if (!source.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return std::nullopt;
    }
    if (source->status != QString::fromUtf8(task::kStatusCompleted)
        && source->status != QString::fromUtf8(task::kStatusArchived)) {
        setError(QStringLiteral("仅已完成或已归档任务可以再次执行"));
        return std::nullopt;
    }
    if (getActiveTask().has_value()) {
        setError(QStringLiteral("已有进行中的任务，请先完成当前任务"));
        return std::nullopt;
    }
    if (!hasControllableRadar(source->deviceIds)) {
        setError(QStringLiteral("原任务已无可控雷达，请检查设备及端点绑定"));
        return std::nullopt;
    }

    task::TaskInfo next = source.value();
    next.id.clear();
    next.status = QString::fromUtf8(task::kStatusDraft);
    next.createdAt = 0;
    next.activatedAt = 0;
    next.completedAt = 0;
    next.archivedAt = 0;

    const auto created = create(next);
    if (!created.has_value())
        return std::nullopt;

    if (!activate(created->id)) {
        const QString activateError = m_lastError;
        remove(created->id);
        setError(activateError);
        return std::nullopt;
    }

    appendTaskEvent(created->id, QStringLiteral("task"),
                    QStringLiteral("按历史任务 %1 的完整配置再次执行").arg(id));
    return get(created->id);
}

/**
 * @brief 仅把当前 Running 任务结束为 Completed，写结束时间并清空活动缓存。
 */
bool TaskService::complete(const QString& id)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (t->status != QString::fromUtf8(task::kStatusActive)) {
        setError(QStringLiteral("仅活动任务可以完成"));
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_store->execute(
            QStringLiteral("UPDATE tasks SET status = ?, completed_at = ? WHERE id = ?"),
            {QString::fromUtf8(task::kStatusCompleted), now, id})) {
        setError(QStringLiteral("完成任务失败"));
        return false;
    }
    refreshActiveTaskCache();
    appendTaskEvent(id, QStringLiteral("task"), QStringLiteral("任务已完成"));
    return true;
}

/**
 * @brief 把非运行任务标记 Archived；不删除其航迹、事件和日志。
 */
bool TaskService::archive(const QString& id)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (t->status != QString::fromUtf8(task::kStatusDraft)
        && t->status != QString::fromUtf8(task::kStatusCompleted)) {
        setError(QStringLiteral("仅草稿或已完成任务可以归档"));
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_store->execute(
            QStringLiteral("UPDATE tasks SET status = ?, archived_at = ? WHERE id = ?"),
            {QString::fromUtf8(task::kStatusArchived), now, id})) {
        setError(QStringLiteral("归档任务失败"));
        return false;
    }
    refreshActiveTaskCache();
    appendTaskEvent(id, QStringLiteral("task"), QStringLiteral("任务已归档"));
    return true;
}

/**
 * @brief 检查任务是否存在航迹、事件或日志等运行数据，用于限制物理删除。
 */
bool TaskService::hasRunData(const QString& id) const
{
    if (!m_store || id.isEmpty()) return false;

    const auto trackRows = m_store->query(
        QStringLiteral("SELECT 1 FROM tracks WHERE task_id = ? LIMIT 1"), {id});
    if (!trackRows.isEmpty()) return true;

    const auto eventRows = m_store->query(
        QStringLiteral("SELECT 1 FROM system_events WHERE task_id = ? LIMIT 1"), {id});
    if (!eventRows.isEmpty()) return true;

    const auto logRows = m_store->query(
        QStringLiteral("SELECT 1 FROM task_logs WHERE task_id = ? LIMIT 1"), {id});
    return !logRows.isEmpty();
}

/**
 * @brief 只允许删除非运行且无运行数据的任务，并在事务中清理成员关系。
 */
bool TaskService::remove(const QString& id)
{
    clearError();
    if (!m_store) return false;
    const auto task = get(id);
    if (!task.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }

    if (task->status == QString::fromUtf8(task::kStatusActive)) {
        setError(QStringLiteral("进行中的任务无法删除"));
        return false;
    }
    const bool isDraft = task->status == QString::fromUtf8(task::kStatusDraft)
                         && task->activatedAt <= 0;
    const bool isArchived = task->status == QString::fromUtf8(task::kStatusArchived);
    if (!isDraft && !isArchived) {
        setError(QStringLiteral("请先完成并归档任务，再执行永久删除"));
        return false;
    }

    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }

    bool ok = true;
    if (isArchived)
        ok = m_store->execute(QStringLiteral("DELETE FROM tracks WHERE task_id = ?"), {id});
    if (ok)
        ok = m_store->execute(QStringLiteral("DELETE FROM system_events WHERE task_id = ?"), {id});
    if (ok)
        ok = m_store->execute(QStringLiteral("DELETE FROM task_logs WHERE task_id = ?"), {id});
    if (ok)
        ok = m_store->execute(QStringLiteral("DELETE FROM task_devices WHERE task_id = ?"), {id});
    if (ok)
        ok = m_store->execute(QStringLiteral("DELETE FROM tasks WHERE id = ?"), {id});

    if (ok && m_store->commitTransaction()) {
        refreshActiveTaskCache();
        return true;
    }

    m_store->rollbackTransaction();
    setError(QStringLiteral("删除任务失败"));
    return false;
}
