#include "TaskService.h"
#include "DeviceRegistry.h"
#include "DataStore.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QVariantMap>

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

TaskService::TaskService(DataStore* store, DeviceRegistry* deviceRegistry)
    : m_store(store)
    , m_deviceReg(deviceRegistry)
{
}

QString TaskService::generateId()
{
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    const uint32_t rand = QRandomGenerator::global()->bounded(10000u);
    return QStringLiteral("task_%1_%2").arg(ms).arg(rand);
}

void TaskService::clearError() { m_lastError.clear(); }
void TaskService::setError(const QString& msg) { m_lastError = msg; }

bool TaskService::isTerminalStatus(const QString& status) const
{
    return status == QString::fromUtf8(task::kStatusArchived);
}

bool TaskService::ensureSchema()
{
    if (!m_store) return false;
    const QString tasksSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  operator_name TEXT,"
        "  status TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  activated_at INTEGER,"
        "  completed_at INTEGER,"
        "  archived_at INTEGER"
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
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  task_id TEXT NOT NULL,"
        "  device_id TEXT,"
        "  ts_ms INTEGER NOT NULL,"
        "  level TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE"
        ")");
    return m_store->execute(tasksSql)
        && m_store->execute(taskDevicesSql)
        && m_store->execute(taskLogsSql)
        && m_store->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_logs_task_ts ON task_logs(task_id, ts_ms)"));
}

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

QVector<task::TaskInfo> TaskService::list() const
{
    QVector<task::TaskInfo> result;
    if (!m_store) return result;
    auto rows = m_store->query(QStringLiteral("SELECT * FROM tasks ORDER BY created_at DESC"));
    for (const auto& row : rows)
        result.append(rowToTask(row));
    return result;
}

std::optional<task::TaskInfo> TaskService::get(const QString& id) const
{
    if (!m_store) return std::nullopt;
    auto rows = m_store->query(QStringLiteral("SELECT * FROM tasks WHERE id = ?"), {id});
    if (rows.isEmpty()) return std::nullopt;
    return rowToTask(rows.first());
}

std::optional<task::TaskInfo> TaskService::activeTask() const
{
    if (!m_store) return std::nullopt;
    auto rows = m_store->query(
        QStringLiteral("SELECT * FROM tasks WHERE status = ? ORDER BY activated_at DESC LIMIT 1"),
        {QString::fromUtf8(task::kStatusActive)});
    if (rows.isEmpty()) return std::nullopt;
    return rowToTask(rows.first());
}

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

bool TaskService::isDeviceInActiveTask(const QString& deviceId) const
{
    if (deviceId.isEmpty() || m_activeTaskCache.taskId.isEmpty())
        return false;
    return m_activeTaskCache.deviceIds.contains(deviceId);
}

QString TaskService::resolveTaskIdForDevice(const QString& deviceId) const
{
    if (deviceId.isEmpty() || m_activeTaskCache.taskId.isEmpty())
        return {};
    if (m_activeTaskCache.deviceIds.contains(deviceId))
        return m_activeTaskCache.taskId;
    return {};
}

QString TaskService::resolveActiveTaskIdForDevice(const QString& deviceId) const
{
    return resolveTaskIdForDevice(deviceId);
}

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

bool TaskService::update(const task::TaskInfo& task)
{
    clearError();
    if (!m_store || task.id.isEmpty()) return false;
    auto existing = get(task.id);
    if (!existing.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (isTerminalStatus(existing->status)) {
        setError(QStringLiteral("invalid task status transition"));
        return false;
    }
    if (!validateDeviceIds(task.deviceIds))
        return false;

    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }

    bool ok = m_store->execute(
        QStringLiteral("UPDATE tasks SET name = ?, description = ?, operator_name = ?, status = ?,"
                       " activated_at = ?, completed_at = ?, archived_at = ? WHERE id = ?"),
        {task.name, task.description, task.operatorName, task.status,
         task.activatedAt > 0 ? task.activatedAt : QVariant(),
         task.completedAt > 0 ? task.completedAt : QVariant(),
         task.archivedAt > 0 ? task.archivedAt : QVariant(),
         task.id});
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

bool TaskService::updateDevices(const QString& id, const QStringList& deviceIds)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (isTerminalStatus(t->status)) {
        setError(QStringLiteral("invalid task status transition"));
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

bool TaskService::activate(const QString& id)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (t->status != QString::fromUtf8(task::kStatusDraft)) {
        setError(QStringLiteral("invalid task status transition"));
        return false;
    }

    if (!hasControllableRadar(t->deviceIds)) {
        setError(QStringLiteral("激活任务前请至少绑定一个已绑定端点的雷达设备"));
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto previousActive = activeTask();
    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }

    bool ok = m_store->execute(
        QStringLiteral("UPDATE tasks SET status = ?, completed_at = ? WHERE status = ? AND id != ?"),
        {QString::fromUtf8(task::kStatusCompleted), now,
         QString::fromUtf8(task::kStatusActive), id});
    if (ok) {
        ok = m_store->execute(
            QStringLiteral("UPDATE tasks SET status = ?, activated_at = ?, completed_at = NULL WHERE id = ?"),
            {QString::fromUtf8(task::kStatusActive), now, id});
    }

    if (ok && m_store->commitTransaction()) {
        refreshActiveTaskCache();
        if (previousActive.has_value() && previousActive->id != id) {
            appendTaskEvent(previousActive->id, QStringLiteral("task"),
                            QStringLiteral("切换任务，原任务自动完成"));
        }
        appendTaskEvent(id, QStringLiteral("task"), QStringLiteral("任务已激活"));
        qDebug() << "[TaskService] task activated:" << id
                 << "devices:" << m_activeTaskCache.deviceIds.values();
        return true;
    }

    m_store->rollbackTransaction();
    setError(QStringLiteral("激活任务失败"));
    return false;
}

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

bool TaskService::complete(const QString& id)
{
    clearError();
    auto t = get(id);
    if (!t.has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }
    if (t->status != QString::fromUtf8(task::kStatusActive)) {
        setError(QStringLiteral("invalid task status transition"));
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
        setError(QStringLiteral("invalid task status transition"));
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
