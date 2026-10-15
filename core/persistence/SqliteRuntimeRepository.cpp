#include "core/persistence/SqliteRuntimeRepository.h"

#include "core/archive/SqliteArchiveRepository.h"
#include "core/task/RadarControlSettingsJson.h"

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
#include <cmath>
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
    if (!insert.prepare(QStringLiteral(
        "INSERT INTO devices("
        "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
        "array_type,max_range_m,faces_json,"
        "longitude_deg,latitude_deg,altitude_m,endpoints_json,protocol_config_json,"
        "changed_utc_ms,changed_by,change_reason,is_deleted) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
        return rollbackWithError(insert.lastError().text());
    }
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
    insert.addBindValue(configuration.endpointsJson);
    insert.addBindValue(configuration.protocolConfigJson);
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
    insert.addBindValue(normalizedText(configuration.changedBy));
    insert.addBindValue(normalizedText(configuration.changeReason));
    insert.addBindValue(0);
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
            "d.longitude_deg,d.latitude_deg,d.altitude_m,d.endpoints_json,"
            "d.protocol_config_json,d.changed_by,d.change_reason "
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
 * @brief 按数据库创建顺序解析模板；任一损坏JSON会拒绝整批，避免UI展示部分真源。
 */
bool SqliteRuntimeRepository::loadControlTemplates(
    QVector<QUuid> &templateUuids,
    QVector<QString> &names,
    QVector<core::task::RadarControlSettings> &settings)
{
    if (!requireDatabase("loadControlTemplates")) return false;
    QVector<QUuid> loadedUuids;
    QVector<QString> loadedNames;
    QVector<core::task::RadarControlSettings> loadedSettings;
    QSqlQuery query(m_databaseOwner.m_database);
    if (!query.exec(QStringLiteral(
            "SELECT template_uuid,template_name,settings_json FROM control_templates "
            "WHERE is_deleted=0 ORDER BY created_utc_ms,template_id"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    while (query.next()) {
        const QUuid uuid = QUuid::fromRfc4122(query.value(0).toByteArray());
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            query.value(2).toString().toUtf8(), &parseError);
        core::task::RadarControlSettings value;
        QString validationError;
        if (uuid.isNull() || parseError.error != QJsonParseError::NoError || !document.isObject()
            || !core::task::radarControlSettingsFromJson(
                document.object(), value, validationError)) {
            m_lastError = validationError.isEmpty()
                ? QStringLiteral("任务模板JSON无效") : validationError;
            return false;
        }
        loadedUuids.push_back(uuid);
        loadedNames.push_back(query.value(1).toString());
        loadedSettings.push_back(value);
    }
    templateUuids = std::move(loadedUuids);
    names = std::move(loadedNames);
    settings = std::move(loadedSettings);
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
        "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
        "endpoints_json,protocol_config_json,changed_utc_ms,changed_by,change_reason,is_deleted) "
        "SELECT device_uuid,revision+1,display_name,manufacturer,model,protocol_adapter_key,array_type,"
        "max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
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
        "device_bindings_json,source_template_uuid,control_settings_json,created_utc_ms,updated_utc_ms) "
        "VALUES(?,?,?,?,?,?,?,?,?,?)"));
    insert.addBindValue(task.taskUuid.toRfc4122());
    insert.addBindValue(task.taskName);
    insert.addBindValue(normalizedText(task.operatorName));
    insert.addBindValue(normalizedText(task.description));
    insert.addBindValue(static_cast<int>(task.radarArrayType));
    insert.addBindValue(taskBindingsJson(task.deviceBindings));
    insert.addBindValue(task.sourceTemplateUuid.toRfc4122());
    insert.addBindValue(QString::fromUtf8(QJsonDocument(
        core::task::radarControlSettingsToJson(task.controlSettings)).toJson(QJsonDocument::Compact)));
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
            "device_bindings_json,source_template_uuid,control_settings_json,created_utc_ms,closed_utc_ms,archived_utc_ms "
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
        task.sourceTemplateUuid = QUuid::fromRfc4122(query.value(7).toByteArray());
        QJsonParseError parseError;
        const QJsonDocument settingsDocument = QJsonDocument::fromJson(
            query.value(8).toString().toUtf8(), &parseError);
        QString settingsError;
        if (task.sourceTemplateUuid.isNull() || parseError.error != QJsonParseError::NoError
            || !settingsDocument.isObject()
            || !core::task::radarControlSettingsFromJson(
                settingsDocument.object(), task.controlSettings, settingsError)) {
            m_lastError = settingsError.isEmpty()
                ? QStringLiteral("数据库任务参数快照无效") : settingsError;
            return false;
        }
        task.createdUtcMs = query.value(9).toLongLong();
        task.completedUtcMs = query.value(10).isNull() ? 0 : query.value(10).toLongLong();
        task.archivedUtcMs = query.value(11).isNull() ? 0 : query.value(11).toLongLong();
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
 * @brief 用 SQLite 条件更新保护归档一次性；已完成时间必须存在，采集会话和历史事实保持不变。
 */
bool SqliteRuntimeRepository::archiveTask(qint64 databaseTaskId, qint64 archivedUtcMs)
{
    if (!requireDatabase("archiveTask") || databaseTaskId <= 0 || archivedUtcMs <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("归档任务参数无效");
        return false;
    }
    QSqlQuery update(m_databaseOwner.m_database);
    update.prepare(QStringLiteral(
        "UPDATE tasks SET archived_utc_ms=?,updated_utc_ms=? "
        "WHERE task_id=? AND is_deleted=0 AND closed_utc_ms IS NOT NULL "
        "AND archived_utc_ms IS NULL AND NOT EXISTS("
        "SELECT 1 FROM collection_sessions WHERE task_id=? AND stop_reason=0)"));
    update.addBindValue(archivedUtcMs);
    update.addBindValue(archivedUtcMs);
    update.addBindValue(databaseTaskId);
    update.addBindValue(databaseTaskId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        m_lastError = update.lastError().text().isEmpty()
            ? QStringLiteral("任务不存在、未完成、已归档或仍有活动采集") : update.lastError().text();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 用单条条件更新覆盖控制参数快照；未完成、已归档或不存在时由 SQL 条件拒绝。
 * @details 任务元数据、模板、采集会话和事件表都不改动；写入的是任务自己的独立参数快照。
 */
bool SqliteRuntimeRepository::updateTaskControlSettings(
    qint64 databaseTaskId, const core::task::RadarControlSettings &settings, qint64 updatedUtcMs)
{
    if (!requireDatabase("updateTaskControlSettings") || databaseTaskId <= 0 || updatedUtcMs <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("更新任务控制参数参数无效");
        return false;
    }
    const QString settingsJson = QString::fromUtf8(QJsonDocument(
        core::task::radarControlSettingsToJson(settings)).toJson(QJsonDocument::Compact));
    QSqlQuery update(m_databaseOwner.m_database);
    update.prepare(QStringLiteral(
        "UPDATE tasks SET control_settings_json=?,updated_utc_ms=? "
        "WHERE task_id=? AND is_deleted=0 AND closed_utc_ms IS NULL AND archived_utc_ms IS NULL"));
    update.addBindValue(settingsJson);
    update.addBindValue(updatedUtcMs);
    update.addBindValue(databaseTaskId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        m_lastError = update.lastError().text().isEmpty()
            ? QStringLiteral("任务不存在、已完成或已归档，无法更新控制参数")
            : update.lastError().text();
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
 * @brief 使用同一启动时刻，在一个事务内插入当前 run 全部启用阵面的 Running 会话；提交前不交付部分身份。
 */
bool SqliteRuntimeRepository::createCollectionSessions(
    qint64 databaseTaskId,
    qint64 deviceRevisionId,
    const QString &endpointKey,
    quint8 faceMask,
    std::array<QUuid, 4> &sessionUuids,
    std::array<qint64, 4> &collectionSessionIds)
{
    sessionUuids = {}; collectionSessionIds = {};
    if (!requireDatabase("createCollectionSessions") || databaseTaskId <= 0
        || deviceRevisionId <= 0 || endpointKey.trimmed().isEmpty() || faceMask == 0 || (faceMask & 0xF0U) != 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("采集会话参数无效");
        return false;
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = m_databaseOwner.lastError();
        return false;
    }
    std::array<QUuid, 4> uuids {};
    std::array<qint64, 4> ids {};
    const qint64 startedUtcMs = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery insert(m_databaseOwner.m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO collection_sessions("
        "session_uuid,run_id,task_id,device_revision_id,endpoint_key,face_index,started_utc_ms,stop_reason) "
        "VALUES(?,?,?,?,?,?,?,0)"));
    for (quint8 face = 0; face < 4; ++face) {
        if ((faceMask & (1U << face)) == 0) continue;
        uuids[face] = QUuid::createUuid();
        insert.bindValue(0, uuids[face].toRfc4122());
        insert.bindValue(1, m_databaseOwner.m_runId);
        insert.bindValue(2, databaseTaskId); insert.bindValue(3, deviceRevisionId);
        insert.bindValue(4, endpointKey); insert.bindValue(5, face); insert.bindValue(6, startedUtcMs);
        if (!insert.exec()) return rollbackWithError(insert.lastError().text());
        ids[face] = insert.lastInsertId().toLongLong();
        if (ids[face] <= 0) return rollbackWithError(QStringLiteral("SQLite 未返回有效采集会话主键"));
        insert.finish();
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("COMMIT"))) {
        return rollbackWithError(m_databaseOwner.lastError());
    }
    sessionUuids = uuids;
    collectionSessionIds = ids;
    m_lastError.clear();
    return true;
}

/**
 * @brief 整组会话在一个事务内停止；任何行失败都回滚，避免四面阵部分停止后无法重试。
 */
bool SqliteRuntimeRepository::stopCollectionSessions(
    const QVector<qint64> &collectionSessionIds,
    quint8 stopReason,
    qint64 stoppedUtcMs)
{
    if (!requireDatabase("stopCollectionSessions") || collectionSessionIds.isEmpty()
        || stopReason < 1 || stopReason > 4 || stoppedUtcMs <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("停止采集会话参数无效");
        return false;
    }
    QSet<qint64> seen;
    for (const auto id : collectionSessionIds) {
        if (id <= 0 || seen.contains(id)) { m_lastError = QStringLiteral("会话主键无效或重复"); return false; }
        seen.insert(id);
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = m_databaseOwner.lastError(); return false;
    }
    QSqlQuery update(m_databaseOwner.m_database);
    update.prepare(QStringLiteral(
        "UPDATE collection_sessions SET stopped_utc_ms=?,stop_reason=? "
        "WHERE collection_session_id=? AND run_id=? AND stop_reason=0"));
    for (const auto id : collectionSessionIds) {
        update.bindValue(0, stoppedUtcMs); update.bindValue(1, stopReason);
        update.bindValue(2, id); update.bindValue(3, m_databaseOwner.m_runId);
        if (!update.exec() || update.numRowsAffected() != 1)
            return rollbackWithError(update.lastError().text().isEmpty()
                ? QStringLiteral("采集会话不存在、已停止或不属于当前 run") : update.lastError().text());
        update.finish();
    }
    if (!m_databaseOwner.executeSql(QStringLiteral("COMMIT"))) return rollbackWithError(m_databaseOwner.lastError());
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
 * @brief 按实际指定的任务、设备、域、级别和时间做 keyset 读取，最多复制 200 条短期记录。
 * @details 主键打破同毫秒时间冲突；跨请求不持有事务或 SQL cursor，查询不会等待任何 Topic Reader。
 */
bool SqliteRuntimeRepository::readEventPage(const QUuid &taskUuid, const QUuid &deviceUuid,
    int domain, int severity, qint64 startUtcMs, qint64 endUtcMs, qint64 beforeUtcMs,
    qint64 beforeEventId, int limit, QJsonArray &events, bool &hasMore)
{
    if (!requireDatabase("readEventPage")) return false;
    if (domain < 0 || domain > 4 || severity < -1 || severity > 4 || startUtcMs <= 0
        || endUtcMs < startUtcMs || endUtcMs > 9007199254740991LL
        || (beforeUtcMs > 0 && (beforeUtcMs < startUtcMs || beforeUtcMs > endUtcMs))
        || limit < 1 || limit > 200 || beforeUtcMs < 0 || beforeEventId < 0
        || ((beforeUtcMs == 0) != (beforeEventId == 0))) {
        m_lastError = QStringLiteral("历史事件分页参数无效");
        return false;
    }
    QSqlQuery query(m_databaseOwner.m_database);
    query.setForwardOnly(true);
    QString sql = QStringLiteral("SELECT e.event_id,e.receive_time_utc_ms,e.event_code,e.severity,"
        "e.argument0,e.argument1,d.device_uuid,e.event_domain,t.task_uuid FROM system_events e "
        "LEFT JOIN devices d ON d.device_revision_id=e.device_revision_id "
        "LEFT JOIN tasks t ON t.task_id=e.task_id "
        "WHERE e.receive_time_utc_ms>=? AND e.receive_time_utc_ms<=? ");
    if (!taskUuid.isNull()) sql += QStringLiteral("AND e.task_id=(SELECT task_id FROM tasks WHERE task_uuid=?) ");
    if (!deviceUuid.isNull()) sql += QStringLiteral("AND d.device_uuid=? ");
    if (domain != 0) sql += QStringLiteral("AND e.event_domain=? ");
    if (severity != -1) sql += QStringLiteral("AND e.severity=? ");
    if (beforeUtcMs > 0) sql += QStringLiteral("AND (e.receive_time_utc_ms<? OR "
        "(e.receive_time_utc_ms=? AND e.event_id<?)) ");
    sql += QStringLiteral("ORDER BY e.receive_time_utc_ms DESC,e.event_id DESC LIMIT ?");
    if (!query.prepare(sql)) { m_lastError = query.lastError().text(); return false; }
    query.addBindValue(startUtcMs);
    query.addBindValue(endUtcMs);
    if (!taskUuid.isNull()) query.addBindValue(taskUuid.toRfc4122());
    if (!deviceUuid.isNull()) query.addBindValue(deviceUuid.toRfc4122());
    if (domain != 0) query.addBindValue(domain);
    if (severity != -1) query.addBindValue(severity);
    if (beforeUtcMs > 0) {
        query.addBindValue(beforeUtcMs);
        query.addBindValue(beforeUtcMs);
        query.addBindValue(beforeEventId);
    }
    query.addBindValue(limit + 1);
    if (!query.exec()) { m_lastError = query.lastError().text(); return false; }
    QJsonArray page;
    bool more = false;
    while (query.next()) {
        if (page.size() == limit) { more = true; break; }
        QJsonObject row{{"eventId", QString::number(query.value(0).toLongLong())},
            {"timestamp", query.value(1).toLongLong()}, {"eventCode", query.value(2).toLongLong()},
            {"severity", query.value(3).toInt()}};
        if (!query.value(4).isNull()) row.insert("argument0", QString::number(query.value(4).toLongLong()));
        if (!query.value(5).isNull()) row.insert("argument1", QString::number(query.value(5).toLongLong()));
        if (!query.value(6).isNull()) row.insert("deviceId",
            QUuid::fromRfc4122(query.value(6).toByteArray()).toString(QUuid::WithoutBraces));
        row.insert("domain", query.value(7).toInt());
        if (!query.value(8).isNull()) row.insert("taskId",
            QUuid::fromRfc4122(query.value(8).toByteArray()).toString(QUuid::WithoutBraces));
        page.append(row);
    }
    if (query.lastError().isValid()) { m_lastError = query.lastError().text(); return false; }
    events = page;
    hasMore = more;
    m_lastError.clear();
    return true;
}

/**
 * @brief 只读现有 application_runs，以启动时刻和主键分页；可空退出信息原样保留，不猜测异常发生时刻。
 */
bool SqliteRuntimeRepository::readRunPage(qint64 startUtcMs, qint64 endUtcMs, int shutdownState,
    qint64 beforeUtcMs, qint64 beforeRunId, int limit, QJsonArray &runs, bool &hasMore)
{
    if (!requireDatabase("readRunPage")) return false;
    if (startUtcMs <= 0 || endUtcMs < startUtcMs || endUtcMs > 9007199254740991LL
        || shutdownState < -1 || shutdownState > 2 || limit < 1 || limit > 200
        || beforeUtcMs < 0 || beforeRunId < 0 || ((beforeUtcMs == 0) != (beforeRunId == 0))
        || (beforeUtcMs > 0 && (beforeUtcMs < startUtcMs || beforeUtcMs > endUtcMs))) {
        m_lastError = QStringLiteral("运行记录分页参数无效"); return false;
    }
    QSqlQuery query(m_databaseOwner.m_database);
    query.setForwardOnly(true);
    QString sql = QStringLiteral("SELECT run_id,run_uuid,started_utc_ms,ended_utc_ms,abnormal_detected_utc_ms,"
        "application_version,schema_version,process_id,shutdown_state,exit_code FROM application_runs "
        "WHERE started_utc_ms>=? AND started_utc_ms<=? ");
    if (shutdownState != -1) sql += QStringLiteral("AND shutdown_state=? ");
    if (beforeUtcMs > 0) sql += QStringLiteral("AND (started_utc_ms<? OR (started_utc_ms=? AND run_id<?)) ");
    sql += QStringLiteral("ORDER BY started_utc_ms DESC,run_id DESC LIMIT ?");
    if (!query.prepare(sql)) { m_lastError = query.lastError().text(); return false; }
    query.addBindValue(startUtcMs); query.addBindValue(endUtcMs);
    if (shutdownState != -1) query.addBindValue(shutdownState);
    if (beforeUtcMs > 0) { query.addBindValue(beforeUtcMs); query.addBindValue(beforeUtcMs); query.addBindValue(beforeRunId); }
    query.addBindValue(limit + 1);
    if (!query.exec()) { m_lastError = query.lastError().text(); return false; }
    QJsonArray page;
    bool more = false;
    while (query.next()) {
        if (page.size() == limit) { more = true; break; }
        QJsonObject row{{"runId", QString::number(query.value(0).toLongLong())},
            {"runUuid", QUuid::fromRfc4122(query.value(1).toByteArray()).toString(QUuid::WithoutBraces)},
            {"startedUtcMs", query.value(2).toLongLong()}, {"applicationVersion", query.value(5).toString()},
            {"schemaVersion", query.value(6).toInt()}, {"shutdownState", query.value(8).toInt()}};
        if (!query.value(3).isNull()) row.insert("endedUtcMs", query.value(3).toLongLong());
        if (!query.value(4).isNull()) row.insert("abnormalDetectedUtcMs", query.value(4).toLongLong());
        if (!query.value(7).isNull()) row.insert("processId", QString::number(query.value(7).toLongLong()));
        if (!query.value(9).isNull()) row.insert("exitCode", query.value(9).toLongLong());
        page.append(row);
    }
    if (query.lastError().isValid()) { m_lastError = query.lastError().text(); return false; }
    runs = page; hasMore = more; m_lastError.clear(); return true;
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
 * @brief 按任务和接收时刻进行 keyset 查询，从会话外键读取采集时的设备 revision 与逐面安装参数。
 * @details 一页最多 1000 条，临时 JSON 不进入 Topic；缺失真实姿态时失败，不让历史绘制回退到当前台账。
 */
bool SqliteRuntimeRepository::readTaskTrackPage(const QUuid &taskUuid, qint64 startUtcMs, qint64 endUtcMs,
    qint64 afterUtcMs, qint64 afterId, int limit, QJsonArray &tracks, bool &hasMore)
{
    if (!requireDatabase("readTaskTrackPage")) return false;
    if (taskUuid.isNull() || startUtcMs <= 0 || endUtcMs < startUtcMs || limit < 1 || limit > 1000
        || afterUtcMs < 0 || afterId < 0 || ((afterUtcMs == 0) != (afterId == 0))) {
        m_lastError = QStringLiteral("历史航迹查询参数无效"); return false;
    }
    QSqlQuery query(m_databaseOwner.m_database);
    query.setForwardOnly(true);
    QString sql = QStringLiteral("SELECT o.observation_id,o.receive_time_utc_ms,s.session_uuid,d.device_uuid,"
        "o.track_id,o.face_index,o.range_m,o.azimuth_rad,o.elevation_rad,o.radial_velocity_mps,"
        "o.amplitude,o.pack_flag,o.scan_sequence,d.longitude_deg,d.latitude_deg,d.altitude_m,d.faces_json "
        "FROM collection_sessions s JOIN track_observations o ON o.collection_session_id=s.collection_session_id AND o.run_id=s.run_id "
        "JOIN devices d ON d.device_revision_id=s.device_revision_id "
        "WHERE s.task_id=(SELECT task_id FROM tasks WHERE task_uuid=?) "
        "AND o.receive_time_utc_ms>=? AND o.receive_time_utc_ms<=? ");
    if (afterUtcMs > 0) sql += QStringLiteral("AND (o.receive_time_utc_ms>? OR (o.receive_time_utc_ms=? AND o.observation_id>?)) ");
    sql += QStringLiteral("ORDER BY o.receive_time_utc_ms,o.observation_id LIMIT ?");
    if (!query.prepare(sql)) { m_lastError = query.lastError().text(); return false; }
    query.addBindValue(taskUuid.toRfc4122()); query.addBindValue(startUtcMs); query.addBindValue(endUtcMs);
    if (afterUtcMs > 0) { query.addBindValue(afterUtcMs); query.addBindValue(afterUtcMs); query.addBindValue(afterId); }
    query.addBindValue(limit + 1);
    if (!query.exec()) { m_lastError = query.lastError().text(); return false; }
    QJsonArray page;
    bool more = false;
    while (query.next()) {
        if (page.size() == limit) { more = true; break; }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(query.value(16).toString().toUtf8(), &parseError);
        const int faceIndex = query.value(5).toInt();
        QJsonObject face;
        for (const auto &item : document.array()) {
            const auto candidate = item.toObject();
            if (candidate.value("faceIndex").toDouble(-1) == faceIndex) {
                if (!face.isEmpty()) { m_lastError = QStringLiteral("归档安装配置阵面重复"); return false; }
                face = candidate;
            }
        }
        if (parseError.error != QJsonParseError::NoError || !document.isArray() || face.isEmpty()
            || !face.value("headingDeg").isDouble() || !face.value("pitchDeg").isDouble() || !face.value("rollDeg").isDouble()) {
            m_lastError = QStringLiteral("归档缺少对应阵面的真实安装姿态"); return false;
        }
        const double longitude = query.value(13).toDouble(), latitude = query.value(14).toDouble();
        const double altitude = query.value(15).toDouble();
        const double heading = face.value("headingDeg").toDouble(), pitch = face.value("pitchDeg").toDouble();
        const double roll = face.value("rollDeg").toDouble();
        if (!std::isfinite(longitude) || std::abs(longitude) > 180 || !std::isfinite(latitude) || std::abs(latitude) >= 90
            || !std::isfinite(altitude) || !std::isfinite(heading) || std::abs(heading) > 360
            || !std::isfinite(pitch) || std::abs(pitch) > 90 || !std::isfinite(roll) || std::abs(roll) > 180) {
            m_lastError = QStringLiteral("归档安装位置或姿态越界，不能进行历史投影"); return false;
        }
        QJsonObject pose{{"lng", query.value(13).toDouble()}, {"lat", query.value(14).toDouble()},
            {"altitude", query.value(15).toDouble()}, {"heading", face.value("headingDeg")},
            {"pitch", face.value("pitchDeg")}, {"roll", face.value("rollDeg")}};
        QJsonObject row{{"observationId", QString::number(query.value(0).toLongLong())},
            {"tsMs", query.value(1).toLongLong()}, {"acquisitionId", QUuid::fromRfc4122(query.value(2).toByteArray()).toString(QUuid::WithoutBraces)},
            {"deviceId", QUuid::fromRfc4122(query.value(3).toByteArray()).toString(QUuid::WithoutBraces)},
            {"targetId", query.value(4).toLongLong()}, {"faceIndex", faceIndex}, {"range", query.value(6).toDouble()},
            {"azimuth", query.value(7).toDouble()}, {"elevation", query.value(8).toDouble()},
            {"radialSpeed", query.value(9).toDouble()}, {"amplitude", query.value(10).toDouble()},
            {"packFlag", query.value(11).toInt()}, {"pose", pose}};
        if (!query.value(12).isNull()) row.insert("scanSequence", query.value(12).toLongLong());
        page.append(row);
    }
    if (query.lastError().isValid()) { m_lastError = query.lastError().text(); return false; }
    tracks = page; hasMore = more; m_lastError.clear(); return true;
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
