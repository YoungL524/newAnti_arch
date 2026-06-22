#include "TaskService.h"
#include "DeviceRegistry.h"
#include "DataStore.h"

#include <QDateTime>
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
    return m_store->execute(tasksSql) && m_store->execute(taskDevicesSql);
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
    if (task.status.isEmpty())
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
        return get(task.id);
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

    if (ok && m_store->commitTransaction())
        return true;

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
    if (ok && m_store->commitTransaction())
        return true;
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

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
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

    if (ok && m_store->commitTransaction())
        return true;

    m_store->rollbackTransaction();
    setError(QStringLiteral("激活任务失败"));
    return false;
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
    return true;
}

bool TaskService::remove(const QString& id)
{
    clearError();
    if (!m_store) return false;
    if (!get(id).has_value()) {
        setError(QStringLiteral("任务不存在"));
        return false;
    }

    if (!m_store->beginTransaction()) {
        setError(QStringLiteral("无法开始事务"));
        return false;
    }

    bool ok = m_store->execute(QStringLiteral("DELETE FROM task_devices WHERE task_id = ?"), {id});
    if (ok)
        ok = m_store->execute(QStringLiteral("DELETE FROM tasks WHERE id = ?"), {id});

    if (ok && m_store->commitTransaction())
        return true;

    m_store->rollbackTransaction();
    setError(QStringLiteral("删除任务失败"));
    return false;
}
