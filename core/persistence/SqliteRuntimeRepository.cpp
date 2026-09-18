#include "core/persistence/SqliteRuntimeRepository.h"

#include "core/archive/SqliteArchiveRepository.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
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

/**
 * @brief 把强类型任务设备选择编码为数据库有界JSON复合字段。
 */
QString taskBindingsJson(const QVector<core::task::TaskDeviceBinding> &bindings)
{
    QJsonArray array;
    for (const auto &binding : bindings) {
        array.append(QJsonObject {
            {QStringLiteral("deviceUuid"), binding.deviceUuid.toString(QUuid::WithoutBraces)},
            {QStringLiteral("faceMask"), static_cast<int>(binding.faceMask)}
        });
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

/**
 * @brief 从数据库复合字段恢复稳定设备UUID和阵面位图，拒绝损坏或重复记录。
 */
bool parseTaskBindings(const QString &json, QVector<core::task::TaskDeviceBinding> &bindings)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray()) return false;
    QVector<core::task::TaskDeviceBinding> parsed;
    QSet<QUuid> seen;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QUuid uuid(object.value(QStringLiteral("deviceUuid")).toString());
        const int mask = object.value(QStringLiteral("faceMask")).toInt(-1);
        if (!value.isObject() || uuid.isNull() || seen.contains(uuid) || mask < 1 || mask > 15) {
            return false;
        }
        seen.insert(uuid);
        parsed.push_back(core::task::TaskDeviceBinding {uuid, static_cast<quint8>(mask)});
    }
    bindings = std::move(parsed);
    return true;
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
        "array_type,max_range_m,faces_json,"
        "longitude_deg,latitude_deg,altitude_m,absolute_heading_deg,pitch_deg,roll_deg,"
        "endpoints_json,protocol_config_json,changed_utc_ms,changed_by,change_reason,is_deleted) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)"));
    insert.addBindValue(configuration.deviceUuid.toRfc4122());
    insert.addBindValue(nextRevision);
    insert.addBindValue(configuration.displayName);
    insert.addBindValue(normalizedText(configuration.manufacturer));
    insert.addBindValue(normalizedText(configuration.model));
    insert.addBindValue(normalizedText(configuration.protocolAdapterKey));
    insert.addBindValue(static_cast<int>(configuration.arrayType));
    insert.addBindValue(configuration.maxRangeM);
    insert.addBindValue(configuration.facesJson);
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
            "d.protocol_adapter_key,d.array_type,d.max_range_m,d.faces_json,"
            "d.longitude_deg,d.latitude_deg,d.altitude_m,d.absolute_heading_deg,"
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
        configuration.arrayType = static_cast<core::config::RadarArrayType>(query.value(7).toUInt());
        configuration.maxRangeM = query.value(8).toDouble();
        configuration.facesJson = query.value(9).toString();
        configuration.longitudeDeg = query.value(10).toDouble();
        configuration.latitudeDeg = query.value(11).toDouble();
        configuration.altitudeM = query.value(12).toDouble();
        configuration.absoluteHeadingDeg = query.value(13).toDouble();
        configuration.pitchDeg = query.value(14).toDouble();
        configuration.rollDeg = query.value(15).toDouble();
        configuration.endpointsJson = query.value(16).toString();
        configuration.protocolConfigJson = query.value(17).toString();
        configuration.changedBy = query.value(18).toString();
        configuration.changeReason = query.value(19).toString();
        loadedIds.push_back(query.value(0).toLongLong());
        loadedConfigurations.push_back(std::move(configuration));
    }
    configurations = std::move(loadedConfigurations);
    deviceRevisionIds = std::move(loadedIds);
    m_lastError.clear();
    return true;
}

/**
 * @brief 在一个立即事务中复制当前完整配置并追加删除revision，历史采集引用的旧行保持不变。
 */
bool SqliteRuntimeRepository::softDeleteDevice(
    const QUuid &deviceUuid, quint32 expectedRevision,
    const QString &changedBy, const QString &changeReason)
{
    if (!requireDatabase("softDeleteDevice") || deviceUuid.isNull() || expectedRevision == 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("软删除设备参数无效");
        return false;
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = m_databaseOwner.lastError();
        return false;
    }
    QSqlQuery insert(m_databaseOwner.m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO devices(device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
        "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,absolute_heading_deg,"
        "pitch_deg,roll_deg,endpoints_json,protocol_config_json,changed_utc_ms,changed_by,change_reason,is_deleted) "
        "SELECT device_uuid,revision+1,display_name,manufacturer,model,protocol_adapter_key,array_type,"
        "max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,absolute_heading_deg,pitch_deg,roll_deg,"
        "endpoints_json,protocol_config_json,?,?,?,1 FROM devices WHERE device_uuid=? AND revision=? "
        "AND is_deleted=0 AND NOT EXISTS(SELECT 1 FROM devices newer WHERE newer.device_uuid=devices.device_uuid "
        "AND newer.revision>devices.revision)"));
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    insert.addBindValue(normalizedText(changedBy));
    insert.addBindValue(normalizedText(changeReason));
    insert.addBindValue(deviceUuid.toRfc4122());
    insert.addBindValue(expectedRevision);
    if (!insert.exec() || insert.numRowsAffected() != 1) {
        return rollbackWithError(insert.lastError().text().isEmpty()
            ? QStringLiteral("设备不存在、已删除或revision已过期") : insert.lastError().text());
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("COMMIT"))) {
        return rollbackWithError(m_databaseOwner.lastError());
    }
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
        "INSERT INTO tasks(task_uuid,task_name,operator_name,description,radar_array_type,"
        "device_bindings_json,created_utc_ms,updated_utc_ms) VALUES(?,?,?,?,?,?,?,?)"));
    insert.addBindValue(task.taskUuid.toRfc4122());
    insert.addBindValue(task.taskName);
    insert.addBindValue(normalizedText(task.operatorName));
    insert.addBindValue(normalizedText(task.description));
    insert.addBindValue(static_cast<int>(task.radarArrayType));
    insert.addBindValue(taskBindingsJson(task.deviceBindings));
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
    task.createdUtcMs = nowUtcMs;
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
            "SELECT task_id,task_uuid,task_name,operator_name,description,radar_array_type,"
            "device_bindings_json,created_utc_ms,closed_utc_ms "
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
        task.radarArrayType = static_cast<core::config::RadarArrayType>(query.value(5).toUInt());
        if ((task.radarArrayType != core::config::RadarArrayType::SingleFace
             && task.radarArrayType != core::config::RadarArrayType::FourFace)
            || !parseTaskBindings(query.value(6).toString(), task.deviceBindings)) {
            m_lastError = QStringLiteral("数据库任务的阵面形态或设备绑定JSON无效");
            return false;
        }
        task.createdUtcMs = query.value(7).toLongLong();
        task.completedUtcMs = query.value(8).isNull() ? 0 : query.value(8).toLongLong();
        loadedIds.push_back(query.value(0).toLongLong());
        loadedTasks.push_back(std::move(task));
    }
    tasks = std::move(loadedTasks);
    databaseTaskIds = std::move(loadedIds);
    m_lastError.clear();
    return true;
}

/**
 * @brief 以单条条件更新收口任务；未停止collection_session存在时由SQL原子拒绝。
 */
bool SqliteRuntimeRepository::completeTask(qint64 databaseTaskId, qint64 completedUtcMs)
{
    if (!requireDatabase("completeTask") || databaseTaskId <= 0 || completedUtcMs <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("完成任务参数无效");
        return false;
    }
    QSqlQuery update(m_databaseOwner.m_database);
    update.prepare(QStringLiteral(
        "UPDATE tasks SET closed_utc_ms=?,updated_utc_ms=? "
        "WHERE task_id=? AND is_deleted=0 AND closed_utc_ms IS NULL AND NOT EXISTS("
        "SELECT 1 FROM collection_sessions WHERE task_id=? AND stop_reason=0)"));
    update.addBindValue(completedUtcMs);
    update.addBindValue(completedUtcMs);
    update.addBindValue(databaseTaskId);
    update.addBindValue(databaseTaskId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        m_lastError = update.lastError().text().isEmpty()
            ? QStringLiteral("任务不存在、已经完成或仍有活动采集") : update.lastError().text();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 只更新当前未删除任务；活动会话检查由拥有运行时状态的TaskService先完成。
 */
bool SqliteRuntimeRepository::softDeleteTask(qint64 databaseTaskId, const QString &deletedBy)
{
    if (!requireDatabase("softDeleteTask") || databaseTaskId <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("软删除任务参数无效");
        return false;
    }
    const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery update(m_databaseOwner.m_database);
    update.prepare(QStringLiteral(
        "UPDATE tasks SET is_deleted=1,deleted_utc_ms=?,deleted_by=?,updated_utc_ms=? "
        "WHERE task_id=? AND is_deleted=0 AND NOT EXISTS("
        "SELECT 1 FROM collection_sessions WHERE task_id=? AND stop_reason=0)"));
    update.addBindValue(nowUtcMs);
    update.addBindValue(normalizedText(deletedBy));
    update.addBindValue(nowUtcMs);
    update.addBindValue(databaseTaskId);
    update.addBindValue(databaseTaskId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        m_lastError = update.lastError().text().isEmpty()
            ? QStringLiteral("任务不存在、已删除或仍有活动采集") : update.lastError().text();
        return false;
    }
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
