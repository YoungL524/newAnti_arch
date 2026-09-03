#include "core/persistence/SqliteRuntimeRepository.h"

#include "core/archive/SqliteArchiveRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>

#include <limits>
#include <utility>

namespace core::persistence {

namespace {

/**
 * @brief 把 Qt 的 null QString 规范化为空文本，匹配数据库 NOT NULL + 空字符串业务语义。
 */
QString normalizedText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

} // namespace

/**
 * @brief 只借用已存在的单写连接 owner，不打开第二个 SQLite 写连接。
 */
SqliteRuntimeRepository::SqliteRuntimeRepository(
    core::archive::SqliteArchiveRepository &databaseOwner)
    : m_databaseOwner(databaseOwner)
{
}

/**
 * @brief 查询设备当前 revision 并在同一立即事务追加下一版，防止两个 UI 编辑静默互相覆盖。
 */
bool SqliteRuntimeRepository::saveDeviceRevision(
    core::config::DeviceConfiguration &configuration,
    qint64 &deviceRevisionId)
{
    deviceRevisionId = 0;
    if (!requireDatabase("saveDeviceRevision")) {
        return false;
    }
    if (configuration.deviceUuid.isNull()) {
        if (configuration.revision != 0) {
            m_lastError = QStringLiteral("新设备不能携带非零基 revision");
            return false;
        }
        configuration.deviceUuid = QUuid::createUuid();
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = m_databaseOwner.lastError();
        return false;
    }

    QSqlQuery current(m_databaseOwner.m_database);
    current.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(revision),0) FROM devices WHERE device_uuid=?"));
    current.addBindValue(configuration.deviceUuid.toRfc4122());
    if (!current.exec() || !current.next()) {
        return rollbackWithError(current.lastError().text());
    }
    const quint32 currentRevision = current.value(0).toUInt();
    if (configuration.revision != currentRevision) {
        return rollbackWithError(QStringLiteral("设备配置 revision 已过期"));
    }
    if (currentRevision == std::numeric_limits<quint32>::max()) {
        return rollbackWithError(QStringLiteral("设备配置 revision 已耗尽"));
    }
    const quint32 nextRevision = currentRevision + 1;

    QSqlQuery insert(m_databaseOwner.m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO devices("
        "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
        "longitude_deg,latitude_deg,altitude_m,absolute_heading_deg,pitch_deg,roll_deg,"
        "endpoints_json,protocol_config_json,changed_utc_ms,changed_by,change_reason,is_deleted) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)"));
    insert.addBindValue(configuration.deviceUuid.toRfc4122());
    insert.addBindValue(nextRevision);
    insert.addBindValue(configuration.displayName);
    insert.addBindValue(normalizedText(configuration.manufacturer));
    insert.addBindValue(normalizedText(configuration.model));
    insert.addBindValue(normalizedText(configuration.protocolAdapterKey));
    insert.addBindValue(configuration.longitudeDeg);
    insert.addBindValue(configuration.latitudeDeg);
    insert.addBindValue(configuration.altitudeM);
    insert.addBindValue(configuration.absoluteHeadingDeg);
    insert.addBindValue(configuration.pitchDeg);
    insert.addBindValue(configuration.rollDeg);
    insert.addBindValue(configuration.endpointsJson);
    insert.addBindValue(configuration.protocolConfigJson);
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    insert.addBindValue(normalizedText(configuration.changedBy));
    insert.addBindValue(normalizedText(configuration.changeReason));
    if (!insert.exec()) {
        return rollbackWithError(insert.lastError().text());
    }
    const qint64 insertedId = insert.lastInsertId().toLongLong();
    if (insertedId <= 0) {
        return rollbackWithError(QStringLiteral("SQLite 未返回有效设备 revision 主键"));
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("COMMIT"))) {
        return rollbackWithError(m_databaseOwner.lastError());
    }
    configuration.revision = nextRevision;
    deviceRevisionId = insertedId;
    m_lastError.clear();
    return true;
}

/**
 * @brief 使用反连接读取每个设备 UUID 的最大 revision，并排除最新版本已经软删除的设备。
 */
bool SqliteRuntimeRepository::loadCurrentDevices(
    QVector<core::config::DeviceConfiguration> &configurations,
    QVector<qint64> &deviceRevisionIds)
{
    if (!requireDatabase("loadCurrentDevices")) {
        return false;
    }
    QVector<core::config::DeviceConfiguration> loadedConfigurations;
    QVector<qint64> loadedIds;
    QSqlQuery query(m_databaseOwner.m_database);
    if (!query.exec(QStringLiteral(
            "SELECT d.device_revision_id,d.device_uuid,d.revision,d.display_name,d.manufacturer,d.model,"
            "d.protocol_adapter_key,d.longitude_deg,d.latitude_deg,d.altitude_m,d.absolute_heading_deg,"
            "d.pitch_deg,d.roll_deg,d.endpoints_json,d.protocol_config_json,d.changed_by,d.change_reason "
            "FROM devices d WHERE d.is_deleted=0 AND NOT EXISTS("
            "SELECT 1 FROM devices newer WHERE newer.device_uuid=d.device_uuid "
            "AND newer.revision>d.revision) ORDER BY d.device_revision_id"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    while (query.next()) {
        core::config::DeviceConfiguration configuration;
        configuration.deviceUuid = QUuid::fromRfc4122(query.value(1).toByteArray());
        configuration.revision = query.value(2).toUInt();
        configuration.displayName = query.value(3).toString();
        configuration.manufacturer = query.value(4).toString();
        configuration.model = query.value(5).toString();
        configuration.protocolAdapterKey = query.value(6).toString();
        configuration.longitudeDeg = query.value(7).toDouble();
        configuration.latitudeDeg = query.value(8).toDouble();
        configuration.altitudeM = query.value(9).toDouble();
        configuration.absoluteHeadingDeg = query.value(10).toDouble();
        configuration.pitchDeg = query.value(11).toDouble();
        configuration.rollDeg = query.value(12).toDouble();
        configuration.endpointsJson = query.value(13).toString();
        configuration.protocolConfigJson = query.value(14).toString();
        configuration.changedBy = query.value(15).toString();
        configuration.changeReason = query.value(16).toString();
        loadedIds.push_back(query.value(0).toLongLong());
        loadedConfigurations.push_back(std::move(configuration));
    }
    configurations = std::move(loadedConfigurations);
    deviceRevisionIds = std::move(loadedIds);
    m_lastError.clear();
    return true;
}

/**
 * @brief 生成缺失 UUID 并在立即事务中创建任务，保证返回 TaskId 前数据库已成为真源。
 */
bool SqliteRuntimeRepository::createTask(core::task::TaskDefinition &task,
                                         qint64 &databaseTaskId)
{
    databaseTaskId = 0;
    if (!requireDatabase("createTask")) {
        return false;
    }
    if (task.taskUuid.isNull()) {
        task.taskUuid = QUuid::createUuid();
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = m_databaseOwner.lastError();
        return false;
    }
    const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery insert(m_databaseOwner.m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO tasks(task_uuid,task_name,operator_name,description,created_utc_ms,updated_utc_ms) "
        "VALUES(?,?,?,?,?,?)"));
    insert.addBindValue(task.taskUuid.toRfc4122());
    insert.addBindValue(task.taskName);
    insert.addBindValue(normalizedText(task.operatorName));
    insert.addBindValue(normalizedText(task.description));
    insert.addBindValue(nowUtcMs);
    insert.addBindValue(nowUtcMs);
    if (!insert.exec()) {
        return rollbackWithError(insert.lastError().text());
    }
    const qint64 insertedId = insert.lastInsertId().toLongLong();
    if (insertedId <= 0) {
        return rollbackWithError(QStringLiteral("SQLite 未返回有效任务主键"));
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("COMMIT"))) {
        return rollbackWithError(m_databaseOwner.lastError());
    }
    databaseTaskId = insertedId;
    m_lastError.clear();
    return true;
}

/**
 * @brief 顺序读取未软删除任务，返回临时集合后一次替换调用方结果。
 */
bool SqliteRuntimeRepository::loadActiveTasks(
    QVector<core::task::TaskDefinition> &tasks,
    QVector<qint64> &databaseTaskIds)
{
    if (!requireDatabase("loadActiveTasks")) {
        return false;
    }
    QVector<core::task::TaskDefinition> loadedTasks;
    QVector<qint64> loadedIds;
    QSqlQuery query(m_databaseOwner.m_database);
    if (!query.exec(QStringLiteral(
            "SELECT task_id,task_uuid,task_name,operator_name,description "
            "FROM tasks WHERE is_deleted=0 ORDER BY task_id"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    while (query.next()) {
        core::task::TaskDefinition task;
        task.taskUuid = QUuid::fromRfc4122(query.value(1).toByteArray());
        task.taskName = query.value(2).toString();
        task.operatorName = query.value(3).toString();
        task.description = query.value(4).toString();
        loadedIds.push_back(query.value(0).toLongLong());
        loadedTasks.push_back(std::move(task));
    }
    tasks = std::move(loadedTasks);
    databaseTaskIds = std::move(loadedIds);
    m_lastError.clear();
    return true;
}

/**
 * @brief 使用当前 run、任务、冻结设备 revision 和 endpointKey 插入一条 Running 会话。
 */
bool SqliteRuntimeRepository::createCollectionSession(
    qint64 databaseTaskId,
    qint64 deviceRevisionId,
    const QString &endpointKey,
    QUuid &sessionUuid,
    qint64 &collectionSessionId)
{
    sessionUuid = QUuid();
    collectionSessionId = 0;
    if (!requireDatabase("createCollectionSession") || databaseTaskId <= 0
        || deviceRevisionId <= 0 || endpointKey.trimmed().isEmpty()) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("采集会话参数无效");
        return false;
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = m_databaseOwner.lastError();
        return false;
    }
    const QUuid newUuid = QUuid::createUuid();
    QSqlQuery insert(m_databaseOwner.m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO collection_sessions("
        "session_uuid,run_id,task_id,device_revision_id,endpoint_key,started_utc_ms,stop_reason) "
        "VALUES(?,?,?,?,?,?,0)"));
    insert.addBindValue(newUuid.toRfc4122());
    insert.addBindValue(m_databaseOwner.m_runId);
    insert.addBindValue(databaseTaskId);
    insert.addBindValue(deviceRevisionId);
    insert.addBindValue(endpointKey);
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()) {
        return rollbackWithError(insert.lastError().text());
    }
    const qint64 insertedId = insert.lastInsertId().toLongLong();
    if (insertedId <= 0) {
        return rollbackWithError(QStringLiteral("SQLite 未返回有效采集会话主键"));
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("COMMIT"))) {
        return rollbackWithError(m_databaseOwner.lastError());
    }
    sessionUuid = newUuid;
    collectionSessionId = insertedId;
    m_lastError.clear();
    return true;
}

/**
 * @brief 只允许将当前 run 中唯一 Running 行收口为已知停止原因，拒绝重复停止和跨 run 更新。
 */
bool SqliteRuntimeRepository::stopCollectionSession(
    qint64 collectionSessionId,
    quint8 stopReason,
    qint64 stoppedUtcMs)
{
    if (!requireDatabase("stopCollectionSession") || collectionSessionId <= 0
        || stopReason < 1 || stopReason > 4 || stoppedUtcMs <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("停止采集会话参数无效");
        return false;
    }
    QSqlQuery update(m_databaseOwner.m_database);
    update.prepare(QStringLiteral(
        "UPDATE collection_sessions SET stopped_utc_ms=?,stop_reason=? "
        "WHERE collection_session_id=? AND run_id=? AND stop_reason=0"));
    update.addBindValue(stoppedUtcMs);
    update.addBindValue(stopReason);
    update.addBindValue(collectionSessionId);
    update.addBindValue(m_databaseOwner.m_runId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        m_lastError = update.lastError().text().isEmpty()
            ? QStringLiteral("采集会话不存在、已停止或不属于当前 run")
            : update.lastError().text();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 返回最近失败文本的值副本，避免上层保存悬空引用。
 */
QString SqliteRuntimeRepository::lastError() const
{
    return m_lastError;
}

/**
 * @brief 复用底层 Repository 的打开状态和 owner 线程合同，不允许第二连接绕开存储 Lane。
 */
bool SqliteRuntimeRepository::requireDatabase(const char *operation)
{
    if (!m_databaseOwner.isOpen()) {
        m_lastError = QStringLiteral("%1 要求已打开的 SQLite 数据库")
                          .arg(QString::fromLatin1(operation));
        return false;
    }
    if (QThread::currentThread() != m_databaseOwner.m_ownerThread) {
        m_lastError = QStringLiteral("%1 只能在 SQLite 写线程执行")
                          .arg(QString::fromLatin1(operation));
        return false;
    }
    return true;
}

/**
 * @brief 保留低频持久化首因并回滚当前事务，ROLLBACK 失败时附加数据库错误。
 */
bool SqliteRuntimeRepository::rollbackWithError(const QString &error)
{
    m_lastError = error.isEmpty() ? QStringLiteral("SQLite 低频实体事务失败") : error;
    QSqlQuery rollback(m_databaseOwner.m_database);
    if (!rollback.exec(QStringLiteral("ROLLBACK"))) {
        m_lastError += QStringLiteral("；ROLLBACK 失败：%1").arg(rollback.lastError().text());
    }
    return false;
}

} // namespace core::persistence
