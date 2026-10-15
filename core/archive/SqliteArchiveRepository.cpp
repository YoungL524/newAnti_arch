#include "core/archive/SqliteArchiveRepository.h"

#include "core/model/Sample.h"
#include "core/model/SystemEvent.h"
#include "core/model/TrackPacket.h"
#include "core/task/RadarControlSettingsJson.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <QtEndian>

#include <cmath>
#include <limits>
#include <utility>

namespace core::archive {

namespace {

constexpr int kApplicationId = 1312900178;
constexpr int kSchemaVersion = 12;
/** @brief 内置默认控制模板的跨重启稳定UUID；迁移和任务来源追溯共同使用。 */
const QByteArray kDefaultControlTemplateUuid = QByteArray::fromHex("6f4c9e1a1f7d4a2395f7a80000000010");
constexpr double kQuarterPi = 0.7853981633974483;

/**
 * @brief 创建 Qt 5 可识别的 NULL qint64 参数，避免把无效可选字段写成业务零值。
 */
QVariant nullInteger()
{
    return QVariant(QVariant::LongLong);
}

/**
 * @brief 创建 Qt 5 可识别的 NULL BLOB 参数，保持 quint64 缺失与合法零值可区分。
 */
QVariant nullBlob()
{
    return QVariant(QVariant::ByteArray);
}

/**
 * @brief 创建 Qt 5 可识别的 NULL 文本参数，供无端点关联的内部事件使用。
 */
QVariant nullString()
{
    return QVariant(QVariant::String);
}

/**
 * @brief 把Schema 8整机姿态迁入每个阵面，并将旧波束中心字段收敛为唯一安装航向。
 * @param legacyJson Schema 8的faces_json。
 * @param fallbackHeadingDeg 旧revision整机航向，仅在阵面没有任何航向字段时兜底。
 * @param fallbackPitchDeg 旧revision整机俯仰，迁入每个阵面。
 * @param fallbackRollDeg 旧revision整机横滚，迁入每个阵面。
 * @param error 失败时返回不能静默修复的数据原因。
 * @return 可写入Schema 9的紧凑JSON；失败返回空字符串。
 */
QString migrateFacePoseJson(const QString &legacyJson,
                            double fallbackHeadingDeg,
                            double fallbackPitchDeg,
                            double fallbackRollDeg,
                            QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(legacyJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        error = QStringLiteral("Schema 9无法解析旧设备阵面JSON");
        return {};
    }
    QJsonArray migrated;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) {
            error = QStringLiteral("Schema 9发现非对象阵面配置");
            return {};
        }
        QJsonObject face = value.toObject();
        const double heading = face.contains(QStringLiteral("headingDeg"))
            ? face.value(QStringLiteral("headingDeg")).toDouble(std::numeric_limits<double>::quiet_NaN())
            : face.value(QStringLiteral("boresightAzimuthDeg")).toDouble(fallbackHeadingDeg);
        const double pitch = face.value(QStringLiteral("pitchDeg")).toDouble(fallbackPitchDeg);
        const double roll = face.value(QStringLiteral("rollDeg")).toDouble(fallbackRollDeg);
        if (!std::isfinite(heading) || heading < 0.0 || heading >= 360.0
            || !std::isfinite(pitch) || pitch < -90.0 || pitch > 90.0
            || !std::isfinite(roll) || roll < -90.0 || roll > 90.0) {
            error = QStringLiteral("Schema 9拒绝超出协议范围的旧阵面姿态");
            return {};
        }
        face.remove(QStringLiteral("boresightAzimuthDeg"));
        face.insert(QStringLiteral("headingDeg"), heading);
        face.insert(QStringLiteral("pitchDeg"), pitch);
        face.insert(QStringLiteral("rollDeg"), roll);
        migrated.append(face);
    }
    return QString::fromUtf8(QJsonDocument(migrated).toJson(QJsonDocument::Compact));
}

} // namespace

/**
 * @brief 只保存组合根传入的路径和版本，连接名延迟到 open() 才按对象地址生成。
 */
SqliteArchiveRepository::SqliteArchiveRepository(QString databasePath, QString applicationVersion)
    : m_databasePath(std::move(databasePath))
    , m_applicationVersion(std::move(applicationVersion))
{
}

/**
 * @brief 在未显式正常关闭时仅释放连接，让当前 run 保持 Running 供下次启动识别异常退出。
 */
SqliteArchiveRepository::~SqliteArchiveRepository()
{
    closeConnection();
}

/**
 * @brief 规范化路径并取得数据库独占进程锁，校验驱动后初始化和恢复运行记录，防止第二个 Core 误改活跃 run。
 * @details 任一步失败都释放 prepared query 和 Qt 连接；不会留下可被 ArchiveReader 误用的半打开状态。
 */
bool SqliteArchiveRepository::open()
{
    if (isOpen() || m_database.isValid()) {
        m_lastError = QStringLiteral("SQLite Repository 已经打开或持有连接");
        return false;
    }
    if (m_databasePath.trimmed().isEmpty() || m_applicationVersion.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("数据库路径和应用版本均不能为空");
        return false;
    }
    const QFileInfo databaseFile(m_databasePath);
    if (!databaseFile.absoluteDir().exists()) {
        m_lastError = QStringLiteral("数据库父目录不存在：%1").arg(databaseFile.absolutePath());
        return false;
    }
    const QString canonical = databaseFile.exists() ? databaseFile.canonicalFilePath()
        : QDir(databaseFile.absoluteDir().canonicalPath()).filePath(databaseFile.fileName());
    if (canonical.isEmpty()) { m_lastError = QStringLiteral("无法规范化数据库路径"); return false; }
    m_databasePath = canonical;
    m_databaseLock = std::make_unique<QLockFile>(canonical + QStringLiteral(".core.lock"));
    m_databaseLock->setStaleLockTime(0);
    if (!m_databaseLock->tryLock(0)) {
        m_lastError = QStringLiteral("数据库已被其他 Core 占用或无法创建独占锁");
        m_databaseLock.reset(); return false;
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = QStringLiteral("Qt 运行环境未提供 QSQLITE 驱动");
        m_databaseLock.reset();
        return false;
    }

    m_ownerThread = QThread::currentThread();
    m_connectionName = QStringLiteral("newanti.archive.%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        closeConnection();
        return false;
    }
    if (!initializeDatabase() || !beginApplicationRun() || !prepareInsertQueries()) {
        closeConnection();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 只在 owner 线程把当前 run 从 Running 转成 Clean，随后无条件释放数据库连接。
 * @details 时间非法或 UPDATE 失败时保留未闭合 run，防止把不完整的停机过程伪装为正常退出。
 */
bool SqliteArchiveRepository::closeCleanly(qint64 endedUtcMs)
{
    if (!requireOwnerThread("closeCleanly")) {
        return false;
    }
    if (endedUtcMs < m_runStartedUtcMs) {
        m_lastError = QStringLiteral("正常退出时间早于运行开始时间");
        closeConnection();
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE application_runs "
        "SET ended_utc_ms = ?, shutdown_state = 1, exit_code = 0 "
        "WHERE run_id = ? AND shutdown_state = 0 AND NOT EXISTS("
        "SELECT 1 FROM collection_sessions WHERE run_id=application_runs.run_id AND stop_reason=0)"));
    query.addBindValue(endedUtcMs);
    query.addBindValue(m_runId);
    const bool updated = query.exec() && query.numRowsAffected() == 1;
    if (!updated) {
        m_lastError = query.lastError().text().isEmpty()
            ? QStringLiteral("当前运行实例未能唯一标记为正常退出")
            : query.lastError().text();
    }
    query = QSqlQuery();
    closeConnection();
    if (updated) {
        m_lastError.clear();
    }
    return updated;
}

/**
 * @brief 以数据库连接、run 主键和线程 owner 同时有效作为可写状态。
 */
bool SqliteArchiveRepository::isOpen() const noexcept
{
    return m_database.isValid() && m_database.isOpen() && m_runId > 0 && m_ownerThread != nullptr;
}

/**
 * @brief 返回当前运行实例数据库主键，供同一存储组合阶段创建 collection session。
 */
qint64 SqliteArchiveRepository::currentRunId() const noexcept
{
    return m_runId;
}

/**
 * @brief 返回最近错误的值副本，避免暴露内部可变字符串引用。
 */
QString SqliteArchiveRepository::lastError() const
{
    return m_lastError;
}

/**
 * @brief 登记非零 EndpointId；相同映射可重复调用，不允许同一句柄改绑到另一端点。
 */
bool SqliteArchiveRepository::bindEndpoint(core::model::EndpointId endpointId,
                                           const QString &endpointKey)
{
    if (!requireOwnerThread("bindEndpoint") || !endpointId.isSet()
        || endpointKey.trimmed().isEmpty()) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("Endpoint 绑定参数无效");
        return false;
    }
    const auto existing = m_endpointKeys.constFind(endpointId.value());
    if (existing != m_endpointKeys.cend() && *existing != endpointKey) {
        m_lastError = QStringLiteral("EndpointId 已绑定到不同 endpointKey");
        return false;
    }
    m_endpointKeys.insert(endpointId.value(), endpointKey);
    m_lastError.clear();
    return true;
}

/**
 * @brief 登记非零 DeviceId；运行中的句柄不能悄悄切换设备配置 revision。
 */
bool SqliteArchiveRepository::bindDevice(core::model::DeviceId deviceId,
                                         qint64 deviceRevisionId)
{
    if (!requireOwnerThread("bindDevice") || !deviceId.isSet() || deviceRevisionId <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("Device 绑定参数无效");
        return false;
    }
    const auto existing = m_deviceRevisionIds.constFind(deviceId.value());
    if (existing != m_deviceRevisionIds.cend() && *existing != deviceRevisionId) {
        m_lastError = QStringLiteral("DeviceId 已绑定到不同设备 revision");
        return false;
    }
    m_deviceRevisionIds.insert(deviceId.value(), deviceRevisionId);
    m_lastError.clear();
    return true;
}

/**
 * @brief 登记非零 TaskId；同一进程句柄一旦发布事实便不得改指另一任务。
 */
bool SqliteArchiveRepository::bindTask(core::model::TaskId taskId, qint64 databaseTaskId)
{
    if (!requireOwnerThread("bindTask") || !taskId.isSet() || databaseTaskId <= 0) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("Task 绑定参数无效");
        return false;
    }
    const auto existing = m_taskIds.constFind(taskId.value());
    if (existing != m_taskIds.cend() && *existing != databaseTaskId) {
        m_lastError = QStringLiteral("TaskId 已绑定到不同数据库任务");
        return false;
    }
    m_taskIds.insert(taskId.value(), databaseTaskId);
    m_lastError.clear();
    return true;
}

/**
 * @brief 把一次运行时采集会话冻结到唯一数据库会话及其身份上下文。
 * @details Track 热写只需一次 QHash 查找即可同时验证 endpoint/device/task，不逐行查询 SQLite 注册表。
 */
bool SqliteArchiveRepository::bindAcquisition(
    core::model::AcquisitionId acquisitionId,
    qint64 collectionSessionId,
    core::model::DeviceId deviceId,
    core::model::TaskId taskId,
    core::model::EndpointId endpointId)
{
    if (!requireOwnerThread("bindAcquisition") || !acquisitionId.isSet()
        || collectionSessionId <= 0 || !deviceId.isSet() || !taskId.isSet()
        || !endpointId.isSet()) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("Acquisition 绑定参数无效");
        return false;
    }
    if (!m_deviceRevisionIds.contains(deviceId.value()) || !m_taskIds.contains(taskId.value())
        || !m_endpointKeys.contains(endpointId.value())) {
        m_lastError = QStringLiteral("Acquisition 依赖的 device/task/endpoint 尚未绑定");
        return false;
    }
    const CollectionBinding candidate {collectionSessionId, deviceId, taskId, endpointId};
    const auto existing = m_collectionBindings.constFind(acquisitionId.value());
    if (existing != m_collectionBindings.cend()
        && (existing->collectionSessionId != candidate.collectionSessionId
            || existing->deviceId != candidate.deviceId
            || existing->taskId != candidate.taskId
            || existing->endpointId != candidate.endpointId)) {
        m_lastError = QStringLiteral("AcquisitionId 已绑定到不同采集上下文");
        return false;
    }
    m_collectionBindings.insert(acquisitionId.value(), candidate);
    m_lastError.clear();
    return true;
}

/**
 * @brief 校验批次和会话映射后复用 prepared INSERT，并用一个立即事务原子提交。
 * @details 输入仍属于 ArchiveReader ScratchBuffer；函数返回前不会保存 records 地址或形成第二级缓存。
 */
bool SqliteArchiveRepository::appendTrackBatch(
    const core::domain::TopicRecord<core::model::TrackPacket> *records,
    qsizetype count)
{
    if (!requireOwnerThread("appendTrackBatch") || records == nullptr || count <= 0
        || !m_trackInsert) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("航迹批次参数或 prepared query 无效");
        return false;
    }
    if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        return false;
    }

    const auto maxSigned = static_cast<quint64>(std::numeric_limits<qint64>::max());
    for (qsizetype index = 0; index < count; ++index) {
        const auto &record = records[index];
        const auto &meta = record.sample.meta;
        const auto &track = record.sample.value;
        const auto binding = m_collectionBindings.constFind(track.acquisitionId.value());
        const bool hasSourceSequence = core::model::hasSampleField(
            meta.validity, core::model::SampleFieldFlag::HasSourceSequence);
        const bool hasSourceTime = core::model::hasSampleField(
            meta.validity, core::model::SampleFieldFlag::HasSourceTimeUtcMs);
        const bool hasScan = core::model::hasTrackField(
            track.validity, core::model::TrackFieldFlag::HasScanSequence);
        const auto knownMetaFlags = core::model::sampleFieldBit(
                                        core::model::SampleFieldFlag::HasSourceSequence)
            | core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceTimeUtcMs);
        const auto knownTrackFlags = core::model::trackFieldBit(
            core::model::TrackFieldFlag::HasScanSequence);

        const bool valid = record.sequence > 0 && record.sequence <= maxSigned
            && binding != m_collectionBindings.cend()
            && binding->deviceId == track.deviceId && binding->taskId == track.taskId
            && binding->endpointId == meta.endpointId && meta.receiveTimeUtcMs > 0
            && meta.receiveTimeSteadyNs > 0 && meta.traceId != 0
            && (meta.validity & ~knownMetaFlags) == 0
            && (hasSourceTime ? meta.sourceTimeUtcMs > 0 : meta.sourceTimeUtcMs == 0)
            && (hasSourceSequence || meta.sourceSequence == 0)
            && (static_cast<quint8>(meta.quality) <= 1)
            && (track.validity & ~knownTrackFlags) == 0
            && (hasScan || track.scanSequence == 0)
            && std::isfinite(track.rangeM) && track.rangeM >= 0.0
            && std::isfinite(track.azimuthRad) && track.azimuthRad >= -kQuarterPi
            && track.azimuthRad <= kQuarterPi && std::isfinite(track.elevationRad)
            && track.elevationRad >= 0.0 && track.elevationRad <= kQuarterPi
            && std::isfinite(track.radialVelocityMps) && std::isfinite(track.amplitude)
            && track.faceIndex <= 3U
            && track.recognitionStatus == core::model::RecognitionStatus::Completed
            && track.recognitionType == core::model::RecognitionType::Unknown;
        if (!valid) {
            return rollbackWithError(
                QStringLiteral("航迹批次第 %1 条违反身份或字段合同").arg(index));
        }

        QSqlQuery &query = *m_trackInsert;
        query.bindValue(0, m_runId);
        query.bindValue(1, binding->collectionSessionId);
        query.bindValue(2, static_cast<qint64>(record.sequence));
        query.bindValue(3, meta.receiveTimeUtcMs);
        query.bindValue(4, hasSourceTime ? QVariant(meta.sourceTimeUtcMs) : nullInteger());
        query.bindValue(5, meta.receiveTimeSteadyNs);
        query.bindValue(6, hasSourceSequence
                               ? QVariant(encodeUnsigned64(meta.sourceSequence))
                               : nullBlob());
        query.bindValue(7, encodeUnsigned64(meta.traceId));
        query.bindValue(8, static_cast<int>(meta.quality));
        query.bindValue(9, static_cast<qint64>(track.trackId));
        query.bindValue(10, track.rangeM);
        query.bindValue(11, track.azimuthRad);
        query.bindValue(12, track.elevationRad);
        query.bindValue(13, track.radialVelocityMps);
        query.bindValue(14, track.amplitude);
        query.bindValue(15, static_cast<int>(track.packFlag));
        query.bindValue(16, static_cast<int>(track.faceIndex));
        query.bindValue(17, hasScan ? QVariant(static_cast<qint64>(track.scanSequence))
                                    : nullInteger());
        query.bindValue(18, static_cast<int>(track.recognitionStatus));
        query.bindValue(19, static_cast<int>(track.recognitionType));
        if (!query.exec()) {
            const QString error = query.lastError().text();
            query.finish();
            return rollbackWithError(error);
        }
        query.finish();
    }
    if (!executeSql(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(m_database);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 将 SystemEvent flags 映射为 NULL/非 NULL 关系列并原子写入一个事件批次。
 * @details 事件可以没有 endpoint/device/task/session；一旦声明关联，句柄必须已绑定且与 acquisition 上下文一致。
 */
bool SqliteArchiveRepository::appendEventBatch(
    const core::domain::TopicRecord<core::model::SystemEvent> *records,
    qsizetype count)
{
    if (!requireOwnerThread("appendEventBatch") || records == nullptr || count <= 0
        || !m_eventInsert) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("事件批次参数或 prepared query 无效");
        return false;
    }
    if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        return false;
    }

    const auto maxSigned = static_cast<quint64>(std::numeric_limits<qint64>::max());
    constexpr core::model::EventSubjectFlags knownEventFlags =
        core::model::eventSubjectBit(core::model::EventSubjectFlag::DeviceIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::TaskIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::AcquisitionIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::TrackIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::RequestIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument0Valid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument1Valid);

    for (qsizetype index = 0; index < count; ++index) {
        const auto &record = records[index];
        const auto &meta = record.sample.meta;
        const auto &event = record.sample.value;
        const bool hasDevice = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::DeviceIdValid);
        const bool hasTask = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::TaskIdValid);
        const bool hasAcquisition = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::AcquisitionIdValid);
        const bool hasTrack = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::TrackIdValid);
        const bool hasRequest = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::RequestIdValid);
        const bool hasArgument0 = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::Argument0Valid);
        const bool hasArgument1 = core::model::hasEventSubject(
            event.flags, core::model::EventSubjectFlag::Argument1Valid);
        const bool hasSourceSequence = core::model::hasSampleField(
            meta.validity, core::model::SampleFieldFlag::HasSourceSequence);
        const bool hasSourceTime = core::model::hasSampleField(
            meta.validity, core::model::SampleFieldFlag::HasSourceTimeUtcMs);
        const auto knownMetaFlags = core::model::sampleFieldBit(
                                        core::model::SampleFieldFlag::HasSourceSequence)
            | core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceTimeUtcMs);

        const auto deviceBinding = hasDevice
            ? m_deviceRevisionIds.constFind(event.deviceId.value())
            : m_deviceRevisionIds.cend();
        const auto taskBinding = hasTask ? m_taskIds.constFind(event.taskId.value())
                                         : m_taskIds.cend();
        const auto collectionBinding = hasAcquisition
            ? m_collectionBindings.constFind(event.acquisitionId.value())
            : m_collectionBindings.cend();
        const auto endpointBinding = meta.endpointId.isSet()
            ? m_endpointKeys.constFind(meta.endpointId.value())
            : m_endpointKeys.cend();

        bool valid = record.sequence > 0 && record.sequence <= maxSigned
            && meta.receiveTimeUtcMs > 0 && meta.receiveTimeSteadyNs > 0
            && meta.traceId != 0 && (meta.validity & ~knownMetaFlags) == 0
            && (hasSourceTime ? meta.sourceTimeUtcMs > 0 : meta.sourceTimeUtcMs == 0)
            && (hasSourceSequence || meta.sourceSequence == 0)
            && static_cast<quint8>(meta.quality) <= 1
            && (event.flags & ~knownEventFlags) == 0
            && (hasDevice ? deviceBinding != m_deviceRevisionIds.cend()
                          : !event.deviceId.isSet())
            && (hasTask ? taskBinding != m_taskIds.cend() : !event.taskId.isSet())
            && (hasAcquisition ? collectionBinding != m_collectionBindings.cend()
                               : !event.acquisitionId.isSet())
            && (hasTrack || event.trackId == 0) && (!hasTrack || hasDevice)
            && (hasRequest || event.requestId == 0)
            && (hasArgument0 || event.argument0 == 0)
            && (hasArgument1 || event.argument1 == 0)
            && (!meta.endpointId.isSet() || endpointBinding != m_endpointKeys.cend())
            && core::model::isRegisteredEventCode(event.eventCode)
            && core::model::eventDomainOf(event.eventCode) == event.domain
            && static_cast<quint8>(event.severity) <= 4
            && event.messageTemplateId == 0;
        if (valid && hasAcquisition) {
            valid = (!hasDevice || collectionBinding->deviceId == event.deviceId)
                && (!hasTask || collectionBinding->taskId == event.taskId)
                && (!meta.endpointId.isSet()
                    || collectionBinding->endpointId == meta.endpointId);
        }
        if (!valid) {
            return rollbackWithError(
                QStringLiteral("事件批次第 %1 条违反身份、flags 或事件码合同").arg(index));
        }

        QSqlQuery &query = *m_eventInsert;
        query.bindValue(0, m_runId);
        query.bindValue(1, static_cast<qint64>(record.sequence));
        query.bindValue(2, meta.receiveTimeUtcMs);
        query.bindValue(3, hasSourceTime ? QVariant(meta.sourceTimeUtcMs) : nullInteger());
        query.bindValue(4, meta.receiveTimeSteadyNs);
        query.bindValue(5, hasSourceSequence
                               ? QVariant(encodeUnsigned64(meta.sourceSequence))
                               : nullBlob());
        query.bindValue(6, encodeUnsigned64(meta.traceId));
        query.bindValue(7, static_cast<int>(meta.quality));
        query.bindValue(8, hasDevice ? QVariant(*deviceBinding) : nullInteger());
        query.bindValue(9, hasTask ? QVariant(*taskBinding) : nullInteger());
        query.bindValue(10, hasAcquisition
                                ? QVariant(collectionBinding->collectionSessionId)
                                : nullInteger());
        query.bindValue(11, meta.endpointId.isSet() ? QVariant(*endpointBinding) : nullString());
        query.bindValue(12, hasTrack ? QVariant(static_cast<qint64>(event.trackId))
                                     : nullInteger());
        query.bindValue(13, hasRequest ? QVariant(encodeUnsigned64(event.requestId))
                                       : nullBlob());
        query.bindValue(14, static_cast<qint64>(static_cast<quint32>(event.eventCode)));
        query.bindValue(15, static_cast<int>(event.domain));
        query.bindValue(16, static_cast<int>(event.severity));
        query.bindValue(17, static_cast<int>(event.flags));
        query.bindValue(18, hasArgument0 ? QVariant(event.argument0) : nullInteger());
        query.bindValue(19, hasArgument1 ? QVariant(event.argument1) : nullInteger());
        query.bindValue(20, static_cast<qint64>(event.messageTemplateId));
        if (!query.exec()) {
            const QString error = query.lastError().text();
            query.finish();
            return rollbackWithError(error);
        }
        query.finish();
    }
    if (!executeSql(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(m_database);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 拒绝跨线程数据库访问，并阻止关闭或未完成 run 登记的 Repository 被调用。
 */
bool SqliteArchiveRepository::requireOwnerThread(const char *operation)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("%1 要求已打开的 SQLite Repository")
                          .arg(QString::fromLatin1(operation));
        return false;
    }
    if (QThread::currentThread() != m_ownerThread) {
        m_lastError = QStringLiteral("%1 只能在 SQLite 写连接所属线程执行")
                          .arg(QString::fromLatin1(operation));
        return false;
    }
    return true;
}

/**
 * @brief 配置 WAL/外键等运行参数，并以 application_id 与 user_version 守住文件类型和版本边界。
 * @details version 0 创建当前六表 Schema；旧版本只按顺序增加已冻结字段，未知新版本直接拒绝。
 */
bool SqliteArchiveRepository::initializeDatabase()
{
    if (!executeSql(QStringLiteral("PRAGMA foreign_keys = ON"))
        || !executeSql(QStringLiteral("PRAGMA busy_timeout = 1000"))
        || !executeSql(QStringLiteral("PRAGMA synchronous = NORMAL"))
        || !executeSql(QStringLiteral("PRAGMA temp_store = MEMORY"))
        || !executeSql(QStringLiteral("PRAGMA auto_vacuum = INCREMENTAL"))) {
        return false;
    }

    QSqlQuery pragma(m_database);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !pragma.next()
        || pragma.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0) {
        m_lastError = pragma.lastError().text().isEmpty()
            ? QStringLiteral("SQLite 无法启用 WAL")
            : pragma.lastError().text();
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA application_id")) || !pragma.next()) {
        m_lastError = pragma.lastError().text();
        return false;
    }
    const int applicationId = pragma.value(0).toInt();
    if (applicationId != 0 && applicationId != kApplicationId) {
        m_lastError = QStringLiteral("SQLite application_id 不属于 NewAntiDrone");
        return false;
    }
    if (applicationId == 0
        && !executeSql(QStringLiteral("PRAGMA application_id = %1").arg(kApplicationId))) {
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA user_version")) || !pragma.next()) {
        m_lastError = pragma.lastError().text();
        return false;
    }
    const int version = pragma.value(0).toInt();
    pragma.finish();
    pragma.clear();
    if (version > kSchemaVersion) {
        m_lastError = QStringLiteral("数据库 Schema 版本高于当前程序支持范围");
        return false;
    }
    if (version == kSchemaVersion) {
        return verifyRequiredTables();
    }
    if (version == 11) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))) return false;
        /** @brief 兼容旧初始化提前创建归档列的数据库；保留已有值，只为缺列的正常 v11 文件补列。 */
        QSqlQuery taskColumns(m_database);
        if (!taskColumns.exec(QStringLiteral("PRAGMA table_info(tasks)"))) {
            return rollbackWithError(taskColumns.lastError().text());
        }
        bool hasArchivedUtcMs = false;
        while (taskColumns.next()) {
            if (taskColumns.value(1).toString() == QStringLiteral("archived_utc_ms")) {
                hasArchivedUtcMs = true;
            }
        }
        taskColumns.finish();
        taskColumns.clear();
        if ((!hasArchivedUtcMs && !executeSql(QStringLiteral(
                "ALTER TABLE tasks ADD COLUMN archived_utc_ms INTEGER "
                "CHECK(archived_utc_ms IS NULL OR archived_utc_ms>=closed_utc_ms)")))
            || !executeSql(QStringLiteral("PRAGMA user_version = 12"))
            || !executeSql(QStringLiteral("COMMIT"))) {
            return rollbackWithError(m_lastError);
        }
        return initializeDatabase();
    }
    if (version == 10) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE collection_sessions ADD COLUMN face_index INTEGER NOT NULL DEFAULT 0 "
                "CHECK(face_index BETWEEN 0 AND 3)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 11"))
            || !executeSql(QStringLiteral("COMMIT"))) {
            return rollbackWithError(m_lastError);
        }
        return initializeDatabase();
    }
    if (version == 9) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "CREATE TABLE control_templates("
                "template_id INTEGER PRIMARY KEY,template_uuid BLOB NOT NULL UNIQUE CHECK(length(template_uuid)=16),"
                "template_name TEXT NOT NULL UNIQUE CHECK(length(template_name)>0),settings_json TEXT NOT NULL "
                "CHECK(length(settings_json)>0),created_utc_ms INTEGER NOT NULL CHECK(created_utc_ms>0),"
                "updated_utc_ms INTEGER NOT NULL CHECK(updated_utc_ms>=created_utc_ms),"
                "is_deleted INTEGER NOT NULL DEFAULT 0 CHECK(is_deleted IN(0,1)))"))
            || !executeSql(QStringLiteral("ALTER TABLE tasks ADD COLUMN source_template_uuid BLOB"))
            || !executeSql(QStringLiteral("ALTER TABLE tasks ADD COLUMN control_settings_json TEXT"))) {
            return rollbackWithError(m_lastError);
        }
        const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();
        QSqlQuery insertTemplate(m_database);
        insertTemplate.prepare(QStringLiteral(
            "INSERT INTO control_templates(template_uuid,template_name,settings_json,created_utc_ms,updated_utc_ms) "
            "VALUES(?,?,?,?,?)"));
        insertTemplate.addBindValue(kDefaultControlTemplateUuid);
        insertTemplate.addBindValue(QStringLiteral("默认探测模板"));
        insertTemplate.addBindValue(core::task::defaultRadarControlSettingsJson());
        insertTemplate.addBindValue(nowUtcMs);
        insertTemplate.addBindValue(nowUtcMs);
        if (!insertTemplate.exec()) return rollbackWithError(insertTemplate.lastError().text());
        QSqlQuery migrateTasks(m_database);
        migrateTasks.prepare(QStringLiteral(
            "UPDATE tasks SET source_template_uuid=?,control_settings_json=? "
            "WHERE source_template_uuid IS NULL OR control_settings_json IS NULL"));
        migrateTasks.addBindValue(kDefaultControlTemplateUuid);
        migrateTasks.addBindValue(core::task::defaultRadarControlSettingsJson());
        if (!migrateTasks.exec()
            || !executeSql(QStringLiteral("PRAGMA user_version = 10"))
            || !executeSql(QStringLiteral("COMMIT"))) {
            return rollbackWithError(migrateTasks.lastError().isValid()
                ? migrateTasks.lastError().text() : m_lastError);
        }
        return initializeDatabase();
    }
    if (version == 8) {
        if (!executeSql(QStringLiteral("PRAGMA foreign_keys = OFF"))
            || !executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "CREATE TABLE devices_v9 ("
                "device_revision_id INTEGER PRIMARY KEY, device_uuid BLOB NOT NULL CHECK(length(device_uuid)=16), "
                "revision INTEGER NOT NULL CHECK(revision>0), display_name TEXT NOT NULL CHECK(length(display_name)>0), "
                "manufacturer TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '', "
                "protocol_adapter_key TEXT NOT NULL DEFAULT '', "
                "array_type INTEGER NOT NULL DEFAULT 1 CHECK(array_type IN(1,4)), "
                "max_range_m REAL NOT NULL DEFAULT 5000.0 CHECK(max_range_m>0.0), "
                "faces_json TEXT NOT NULL CHECK(length(faces_json)>0), "
                "longitude_deg REAL NOT NULL CHECK(longitude_deg BETWEEN -180.0 AND 180.0), "
                "latitude_deg REAL NOT NULL CHECK(latitude_deg BETWEEN -90.0 AND 90.0), altitude_m REAL NOT NULL, "
                "endpoints_json TEXT NOT NULL CHECK(length(endpoints_json)>0), "
                "protocol_config_json TEXT NOT NULL DEFAULT '{}', "
                "changed_utc_ms INTEGER NOT NULL CHECK(changed_utc_ms>0), changed_by TEXT NOT NULL DEFAULT '', "
                "change_reason TEXT NOT NULL DEFAULT '', is_deleted INTEGER NOT NULL DEFAULT 0 CHECK(is_deleted IN(0,1)), "
                "UNIQUE(device_uuid,revision))"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            executeSql(QStringLiteral("PRAGMA foreign_keys = ON"));
            return false;
        }

        QSqlQuery source(m_database);
        QSqlQuery insert(m_database);
        const bool prepared = source.exec(QStringLiteral(
            "SELECT device_revision_id,device_uuid,revision,display_name,manufacturer,model,"
            "protocol_adapter_key,array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
            "absolute_heading_deg,pitch_deg,roll_deg,endpoints_json,protocol_config_json,"
            "changed_utc_ms,changed_by,change_reason,is_deleted FROM devices ORDER BY device_revision_id"))
            && insert.prepare(QStringLiteral(
                "INSERT INTO devices_v9(device_revision_id,device_uuid,revision,display_name,manufacturer,model,"
                "protocol_adapter_key,array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
                "endpoints_json,protocol_config_json,changed_utc_ms,changed_by,change_reason,is_deleted) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        if (!prepared) {
            m_lastError = source.lastError().isValid() ? source.lastError().text() : insert.lastError().text();
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            executeSql(QStringLiteral("PRAGMA foreign_keys = ON"));
            return false;
        }
        while (source.next()) {
            QString migrationError;
            const QString facesJson = migrateFacePoseJson(
                source.value(9).toString(), source.value(13).toDouble(),
                source.value(14).toDouble(), source.value(15).toDouble(), migrationError);
            if (facesJson.isEmpty()) {
                m_lastError = migrationError;
                QSqlQuery rollback(m_database);
                rollback.exec(QStringLiteral("ROLLBACK"));
                executeSql(QStringLiteral("PRAGMA foreign_keys = ON"));
                return false;
            }
            insert.bindValue(0, source.value(0));
            insert.bindValue(1, source.value(1));
            insert.bindValue(2, source.value(2));
            insert.bindValue(3, source.value(3));
            insert.bindValue(4, source.value(4));
            insert.bindValue(5, source.value(5));
            insert.bindValue(6, source.value(6));
            insert.bindValue(7, source.value(7));
            insert.bindValue(8, source.value(8));
            insert.bindValue(9, facesJson);
            insert.bindValue(10, source.value(10));
            insert.bindValue(11, source.value(11));
            insert.bindValue(12, source.value(12));
            insert.bindValue(13, source.value(16));
            insert.bindValue(14, source.value(17));
            insert.bindValue(15, source.value(18));
            insert.bindValue(16, source.value(19));
            insert.bindValue(17, source.value(20));
            insert.bindValue(18, source.value(21));
            if (!insert.exec()) {
                m_lastError = insert.lastError().text();
                QSqlQuery rollback(m_database);
                rollback.exec(QStringLiteral("ROLLBACK"));
                executeSql(QStringLiteral("PRAGMA foreign_keys = ON"));
                return false;
            }
        }
        source.finish();
        insert.finish();
        source.clear();
        insert.clear();
        if (!executeSql(QStringLiteral("DROP TABLE devices"))
            || !executeSql(QStringLiteral("ALTER TABLE devices_v9 RENAME TO devices"))
            || !executeSql(QStringLiteral(
                "CREATE INDEX idx_device_latest_revision ON devices(device_uuid,revision DESC)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 9"))
            || !executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            executeSql(QStringLiteral("PRAGMA foreign_keys = ON"));
            return false;
        }
        if (!executeSql(QStringLiteral("PRAGMA foreign_keys = ON"))) {
            return false;
        }
        QSqlQuery foreignKeyCheck(m_database);
        if (!foreignKeyCheck.exec(QStringLiteral("PRAGMA foreign_key_check"))
            || foreignKeyCheck.next()) {
            m_lastError = foreignKeyCheck.lastError().text().isEmpty()
                ? QStringLiteral("Schema 9迁移后外键校验失败") : foreignKeyCheck.lastError().text();
            return false;
        }
        return initializeDatabase();
    }
    if (version == 7) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "INSERT INTO devices("
                "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
                "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
                "absolute_heading_deg,pitch_deg,roll_deg,endpoints_json,protocol_config_json,"
                "changed_utc_ms,changed_by,change_reason,is_deleted) "
                "SELECT d.device_uuid,d.revision+1,d.display_name,d.manufacturer,d.model,d.protocol_adapter_key,"
                "d.array_type,d.max_range_m,d.faces_json,118.65303,31.93494,63.9288,"
                "d.absolute_heading_deg,d.pitch_deg,d.roll_deg,d.endpoints_json,d.protocol_config_json,"
                "CAST(strftime('%s','now') AS INTEGER)*1000,'system-migration',"
                "'Schema 8 补齐内置雷达默认部署位置',0 FROM devices d "
                "WHERE d.device_uuid IN(X'6F4C9E1A1F7D4A2395F7A80000000001',"
                "X'6F4C9E1A1F7D4A2395F7A80000000002') "
                "AND d.is_deleted=0 AND d.longitude_deg=0.0 AND d.latitude_deg=0.0 AND d.altitude_m=0.0 "
                "AND d.revision=(SELECT MAX(latest.revision) FROM devices latest "
                "WHERE latest.device_uuid=d.device_uuid)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 8"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version == 6) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "INSERT INTO devices("
                "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
                "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
                "absolute_heading_deg,pitch_deg,roll_deg,endpoints_json,protocol_config_json,"
                "changed_utc_ms,changed_by,change_reason,is_deleted) "
                "SELECT d.device_uuid,d.revision+1,d.display_name,d.manufacturer,d.model,d.protocol_adapter_key,"
                "d.array_type,d.max_range_m,d.faces_json,d.longitude_deg,d.latitude_deg,d.altitude_m,"
                "d.absolute_heading_deg,d.pitch_deg,d.roll_deg,d.endpoints_json,"
                "'{\"communication\":{\"description\":\"DCMP内部DP/SP通信\",\"targets\":{"
                "\"dph\":{\"address\":\"127.0.0.1\",\"port\":3456},"
                "\"workmode\":{\"address\":\"127.0.0.1\",\"port\":1234}}}}',"
                "CAST(strftime('%s','now') AS INTEGER)*1000,'system-migration',"
                "'Schema 7 补齐DCMP具名发送目标',0 FROM devices d "
                "WHERE d.is_deleted=0 AND d.protocol_adapter_key='dcmp.dp.v1' "
                "AND d.protocol_config_json='{}' AND d.revision=("
                "SELECT MAX(latest.revision) FROM devices latest WHERE latest.device_uuid=d.device_uuid)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 7"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version == 1) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE track_observations ADD COLUMN pack_flag INTEGER NOT NULL DEFAULT 1 "
                "CHECK(pack_flag IN(0,1))"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 2"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version == 2) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE track_observations ADD COLUMN face_index INTEGER NOT NULL DEFAULT 0 "
                "CHECK(face_index BETWEEN 0 AND 3)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 3"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version == 3) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE devices ADD COLUMN array_type INTEGER NOT NULL DEFAULT 1 "
                "CHECK(array_type IN(1,4))"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE devices ADD COLUMN max_range_m REAL NOT NULL DEFAULT 5000.0 "
                "CHECK(max_range_m>0.0)"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE devices ADD COLUMN faces_json TEXT NOT NULL DEFAULT "
                "'[{\"faceIndex\":0,\"boresightAzimuthDeg\":0,\"scanHalfAngleDeg\":45,\"enabled\":true}]' "
                "CHECK(length(faces_json)>0)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 4"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version == 4) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "INSERT INTO devices("
                "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
                "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
                "absolute_heading_deg,pitch_deg,roll_deg,endpoints_json,protocol_config_json,"
                "changed_utc_ms,changed_by,change_reason,is_deleted) VALUES("
                "X'6F4C9E1A1F7D4A2395F7A80000000001',1,'雷达 1','YHF','YHF-YZ-KU',"
                "'20260915',1,5000.0,"
                "'[{\"faceIndex\":0,\"boresightAzimuthDeg\":0,\"scanHalfAngleDeg\":45,\"enabled\":true}]',"
                "118.65303,31.93494,63.9288,0.0,0.0,0.0,"
                "'[{\"endpointKey\":\"radar_cpu\",\"role\":\"track_receive\",\"transport\":\"udp\","
                "\"localAddress\":\"127.0.0.1\",\"localPort\":7788,\"remoteAddress\":\"\",\"remotePort\":0}]',"
                "'{}',CAST(strftime('%s','now') AS INTEGER)*1000,'system-bootstrap',"
                "'Schema 5 内置单面阵设备台账',0)"))
            || !executeSql(QStringLiteral(
                "INSERT INTO devices("
                "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
                "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
                "absolute_heading_deg,pitch_deg,roll_deg,endpoints_json,protocol_config_json,"
                "changed_utc_ms,changed_by,change_reason,is_deleted) VALUES("
                "X'6F4C9E1A1F7D4A2395F7A80000000002',1,'雷达 2','YHF','YHF-YZ-KU',"
                "'20260915',4,5000.0,"
                "'[{\"faceIndex\":0,\"boresightAzimuthDeg\":0,\"scanHalfAngleDeg\":45,\"enabled\":true},"
                "{\"faceIndex\":1,\"boresightAzimuthDeg\":90,\"scanHalfAngleDeg\":45,\"enabled\":true},"
                "{\"faceIndex\":2,\"boresightAzimuthDeg\":180,\"scanHalfAngleDeg\":45,\"enabled\":true},"
                "{\"faceIndex\":3,\"boresightAzimuthDeg\":270,\"scanHalfAngleDeg\":45,\"enabled\":true}]',"
                "118.65303,31.93494,63.9288,0.0,0.0,0.0,"
                "'[{\"endpointKey\":\"radar_cpu\",\"role\":\"track_receive\",\"transport\":\"udp\","
                "\"localAddress\":\"127.0.0.1\",\"localPort\":7788,\"remoteAddress\":\"\",\"remotePort\":0}]',"
                "'{}',CAST(strftime('%s','now') AS INTEGER)*1000,'system-bootstrap',"
                "'Schema 5 内置四面阵设备台账',0)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 5"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version == 5) {
        if (!executeSql(QStringLiteral("BEGIN IMMEDIATE"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE tasks ADD COLUMN radar_array_type INTEGER NOT NULL DEFAULT 1 "
                "CHECK(radar_array_type IN(1,4))"))
            || !executeSql(QStringLiteral(
                "ALTER TABLE tasks ADD COLUMN device_bindings_json TEXT NOT NULL DEFAULT '[]' "
                "CHECK(length(device_bindings_json)>0)"))
            || !executeSql(QStringLiteral("PRAGMA user_version = 6"))) {
            return rollbackWithError(m_lastError);
        }
        if (!executeSql(QStringLiteral("COMMIT"))) {
            QSqlQuery rollback(m_database);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return false;
        }
        return initializeDatabase();
    }
    if (version != 0 || !executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        if (version != 0) m_lastError = QStringLiteral("当前程序没有对应数据库升级路径");
        return false;
    }

    const QStringList schemaStatements {
        QStringLiteral(
            "CREATE TABLE application_runs ("
            "run_id INTEGER PRIMARY KEY, run_uuid BLOB NOT NULL UNIQUE CHECK(length(run_uuid)=16), "
            "started_utc_ms INTEGER NOT NULL CHECK(started_utc_ms>0), ended_utc_ms INTEGER, "
            "abnormal_detected_utc_ms INTEGER, application_version TEXT NOT NULL CHECK(length(application_version)>0), "
            "schema_version INTEGER NOT NULL CHECK(schema_version>0), process_id INTEGER, "
            "shutdown_state INTEGER NOT NULL DEFAULT 0 CHECK(shutdown_state IN(0,1,2)), exit_code INTEGER, "
            "CHECK(ended_utc_ms IS NULL OR ended_utc_ms>=started_utc_ms), "
            "CHECK(abnormal_detected_utc_ms IS NULL OR abnormal_detected_utc_ms>=started_utc_ms), "
            "CHECK((shutdown_state=0 AND ended_utc_ms IS NULL AND abnormal_detected_utc_ms IS NULL) OR "
            "(shutdown_state=1 AND ended_utc_ms IS NOT NULL AND abnormal_detected_utc_ms IS NULL) OR "
            "(shutdown_state=2 AND ended_utc_ms IS NULL AND abnormal_detected_utc_ms IS NOT NULL)))"),
        QStringLiteral(
            "CREATE TABLE devices ("
            "device_revision_id INTEGER PRIMARY KEY, device_uuid BLOB NOT NULL CHECK(length(device_uuid)=16), "
            "revision INTEGER NOT NULL CHECK(revision>0), display_name TEXT NOT NULL CHECK(length(display_name)>0), "
            "manufacturer TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '', "
            "protocol_adapter_key TEXT NOT NULL DEFAULT '', "
            "array_type INTEGER NOT NULL DEFAULT 1 CHECK(array_type IN(1,4)), "
            "max_range_m REAL NOT NULL DEFAULT 5000.0 CHECK(max_range_m>0.0), "
            "faces_json TEXT NOT NULL DEFAULT "
            "'[{\"faceIndex\":0,\"boresightAzimuthDeg\":0,\"scanHalfAngleDeg\":45,\"enabled\":true}]' "
            "CHECK(length(faces_json)>0), "
            "longitude_deg REAL NOT NULL CHECK(longitude_deg BETWEEN -180.0 AND 180.0), "
            "latitude_deg REAL NOT NULL CHECK(latitude_deg BETWEEN -90.0 AND 90.0), altitude_m REAL NOT NULL, "
            "absolute_heading_deg REAL NOT NULL CHECK(absolute_heading_deg>=0.0 AND absolute_heading_deg<360.0), "
            "pitch_deg REAL NOT NULL CHECK(pitch_deg BETWEEN -90.0 AND 90.0), "
            "roll_deg REAL NOT NULL CHECK(roll_deg>=-180.0 AND roll_deg<180.0), "
            "endpoints_json TEXT NOT NULL CHECK(length(endpoints_json)>0), protocol_config_json TEXT NOT NULL DEFAULT '{}', "
            "changed_utc_ms INTEGER NOT NULL CHECK(changed_utc_ms>0), changed_by TEXT NOT NULL DEFAULT '', "
            "change_reason TEXT NOT NULL DEFAULT '', is_deleted INTEGER NOT NULL DEFAULT 0 CHECK(is_deleted IN(0,1)), "
            "UNIQUE(device_uuid,revision))"),
        QStringLiteral(
            "CREATE TABLE tasks ("
            "task_id INTEGER PRIMARY KEY, task_uuid BLOB NOT NULL UNIQUE CHECK(length(task_uuid)=16), "
            "task_name TEXT NOT NULL CHECK(length(task_name)>0), operator_name TEXT NOT NULL DEFAULT '', "
            "description TEXT NOT NULL DEFAULT '', created_utc_ms INTEGER NOT NULL CHECK(created_utc_ms>0), "
            "updated_utc_ms INTEGER NOT NULL CHECK(updated_utc_ms>=created_utc_ms), closed_utc_ms INTEGER, "
            "is_deleted INTEGER NOT NULL DEFAULT 0 CHECK(is_deleted IN(0,1)), deleted_utc_ms INTEGER, "
            "deleted_by TEXT NOT NULL DEFAULT '', CHECK(closed_utc_ms IS NULL OR closed_utc_ms>=created_utc_ms), "
            "CHECK((is_deleted=0 AND deleted_utc_ms IS NULL) OR (is_deleted=1 AND deleted_utc_ms IS NOT NULL)), "
            "CHECK(deleted_utc_ms IS NULL OR deleted_utc_ms>=created_utc_ms))"),
        QStringLiteral(
            "CREATE TABLE collection_sessions ("
            "collection_session_id INTEGER PRIMARY KEY, session_uuid BLOB NOT NULL UNIQUE CHECK(length(session_uuid)=16), "
            "run_id INTEGER NOT NULL REFERENCES application_runs(run_id), task_id INTEGER NOT NULL REFERENCES tasks(task_id), "
            "device_revision_id INTEGER NOT NULL REFERENCES devices(device_revision_id), "
            "endpoint_key TEXT NOT NULL CHECK(length(endpoint_key)>0), started_utc_ms INTEGER NOT NULL CHECK(started_utc_ms>0), "
            "stopped_utc_ms INTEGER, stop_reason INTEGER NOT NULL DEFAULT 0 CHECK(stop_reason IN(0,1,2,3,4,5)), "
            "UNIQUE(collection_session_id,run_id), CHECK(stopped_utc_ms IS NULL OR stopped_utc_ms>=started_utc_ms), "
            "CHECK((stop_reason=0 AND stopped_utc_ms IS NULL) OR "
            "(stop_reason IN(1,2,3,4) AND stopped_utc_ms IS NOT NULL) OR "
            "(stop_reason=5 AND stopped_utc_ms IS NULL)))"),
        QStringLiteral(
            "CREATE TABLE track_observations ("
            "observation_id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL, collection_session_id INTEGER NOT NULL, "
            "topic_sequence INTEGER NOT NULL CHECK(topic_sequence>0), receive_time_utc_ms INTEGER NOT NULL CHECK(receive_time_utc_ms>0), "
            "source_time_utc_ms INTEGER, receive_time_steady_ns INTEGER NOT NULL CHECK(receive_time_steady_ns>0), "
            "source_sequence_u64 BLOB CHECK(source_sequence_u64 IS NULL OR length(source_sequence_u64)=8), "
            "trace_id_u64 BLOB NOT NULL CHECK(length(trace_id_u64)=8), quality INTEGER NOT NULL CHECK(quality IN(0,1)), "
            "track_id INTEGER NOT NULL CHECK(track_id BETWEEN 0 AND 4294967295), range_m REAL NOT NULL CHECK(range_m>=0.0), "
            "azimuth_rad REAL NOT NULL CHECK(azimuth_rad BETWEEN -0.7853981633974483 AND 0.7853981633974483), "
            "elevation_rad REAL NOT NULL CHECK(elevation_rad BETWEEN 0.0 AND 0.7853981633974483), "
            "radial_velocity_mps REAL NOT NULL, amplitude REAL NOT NULL, "
            "pack_flag INTEGER NOT NULL CHECK(pack_flag IN(0,1)), "
            "face_index INTEGER NOT NULL CHECK(face_index BETWEEN 0 AND 3), "
            "scan_sequence INTEGER CHECK(scan_sequence IS NULL OR scan_sequence BETWEEN 0 AND 4294967295), "
            "recognition_status INTEGER NOT NULL CHECK(recognition_status=1), "
            "recognition_type INTEGER NOT NULL CHECK(recognition_type=0), "
            "FOREIGN KEY(collection_session_id,run_id) REFERENCES collection_sessions(collection_session_id,run_id), "
            "UNIQUE(run_id,topic_sequence))"),
        QStringLiteral(
            "CREATE TABLE system_events ("
            "event_id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL REFERENCES application_runs(run_id), "
            "topic_sequence INTEGER NOT NULL CHECK(topic_sequence>0), receive_time_utc_ms INTEGER NOT NULL CHECK(receive_time_utc_ms>0), "
            "source_time_utc_ms INTEGER, receive_time_steady_ns INTEGER NOT NULL CHECK(receive_time_steady_ns>0), "
            "source_sequence_u64 BLOB CHECK(source_sequence_u64 IS NULL OR length(source_sequence_u64)=8), "
            "trace_id_u64 BLOB NOT NULL CHECK(length(trace_id_u64)=8), quality INTEGER NOT NULL CHECK(quality IN(0,1)), "
            "device_revision_id INTEGER REFERENCES devices(device_revision_id), task_id INTEGER REFERENCES tasks(task_id), "
            "collection_session_id INTEGER REFERENCES collection_sessions(collection_session_id), endpoint_key TEXT, "
            "track_id INTEGER CHECK(track_id IS NULL OR track_id BETWEEN 0 AND 4294967295), "
            "request_id_u64 BLOB CHECK(request_id_u64 IS NULL OR length(request_id_u64)=8), "
            "event_code INTEGER NOT NULL CHECK(event_code BETWEEN 0 AND 4294967295), "
            "event_domain INTEGER NOT NULL CHECK(event_domain IN(1,2,3,4)), severity INTEGER NOT NULL CHECK(severity IN(0,1,2,3,4)), "
            "subject_flags INTEGER NOT NULL CHECK(subject_flags BETWEEN 0 AND 127), argument0 INTEGER, argument1 INTEGER, "
            "message_template_id INTEGER NOT NULL DEFAULT 0 CHECK(message_template_id BETWEEN 0 AND 4294967295), "
            "UNIQUE(run_id,topic_sequence))"),
        QStringLiteral("CREATE INDEX idx_device_latest_revision ON devices(device_uuid,revision DESC)"),
        QStringLiteral("CREATE INDEX idx_task_created ON tasks(created_utc_ms DESC,task_id DESC)"),
        QStringLiteral("CREATE INDEX idx_task_deleted ON tasks(is_deleted,created_utc_ms DESC,task_id DESC)"),
        QStringLiteral("CREATE INDEX idx_session_task_time ON collection_sessions(task_id,started_utc_ms,collection_session_id)"),
        QStringLiteral("CREATE INDEX idx_session_device_time ON collection_sessions(device_revision_id,started_utc_ms,collection_session_id)"),
        QStringLiteral("CREATE INDEX idx_session_run ON collection_sessions(run_id,collection_session_id)"),
        QStringLiteral("CREATE INDEX idx_track_time ON track_observations(receive_time_utc_ms,observation_id)"),
        QStringLiteral("CREATE INDEX idx_track_session_time ON track_observations(collection_session_id,receive_time_utc_ms,observation_id)"),
        QStringLiteral("CREATE INDEX idx_track_identity_time ON track_observations(collection_session_id,track_id,receive_time_utc_ms,observation_id)"),
        QStringLiteral("CREATE INDEX idx_event_time ON system_events(receive_time_utc_ms,event_id)"),
        QStringLiteral("CREATE INDEX idx_event_domain_code_time ON system_events(event_domain,event_code,receive_time_utc_ms,event_id)"),
        QStringLiteral("CREATE INDEX idx_event_severity_time ON system_events(severity,receive_time_utc_ms,event_id)"),
        QStringLiteral("CREATE INDEX idx_event_task_time ON system_events(task_id,receive_time_utc_ms,event_id)"),
        QStringLiteral("CREATE INDEX idx_event_device_time ON system_events(device_revision_id,receive_time_utc_ms,event_id)")
    };

    for (const QString &statement : schemaStatements) {
        if (!executeSql(statement)) {
            return rollbackWithError(m_lastError);
        }
    }
    if (!executeSql(QStringLiteral("PRAGMA user_version = 4"))) {
        return rollbackWithError(m_lastError);
    }
    if (!executeSql(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(m_database);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return false;
    }
    return initializeDatabase();
}

/**
 * @brief 查询sqlite_master、航迹/会话列和任务绑定列，确保当前版本不是只改版本号的伪Schema。
 */
bool SqliteArchiveRepository::verifyRequiredTables()
{
    const QSet<QString> required {
        QStringLiteral("application_runs"),
        QStringLiteral("devices"),
        QStringLiteral("tasks"),
        QStringLiteral("collection_sessions"),
        QStringLiteral("track_observations"),
        QStringLiteral("system_events"),
        QStringLiteral("control_templates")
    };
    QSet<QString> found;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    while (query.next()) {
        found.insert(query.value(0).toString());
    }
    if (found != required) {
        m_lastError = QStringLiteral("当前 user_version 的业务表集合不等于冻结七表 Schema");
        return false;
    }
    bool hasPackFlag = false;
    bool hasFaceIndex = false;
    QSqlQuery columns(m_database);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(track_observations)"))) {
        m_lastError = columns.lastError().text();
        return false;
    }
    while (columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("pack_flag")) {
            hasPackFlag = true;
        } else if (columns.value(1).toString() == QStringLiteral("face_index")) {
            hasFaceIndex = true;
        }
    }
    if (!hasPackFlag) {
        m_lastError = QStringLiteral("track_observations 缺少 pack_flag 列");
        return false;
    }
    if (!hasFaceIndex) {
        m_lastError = QStringLiteral("track_observations 缺少 face_index 列");
        return false;
    }
    QSqlQuery sessionColumns(m_database);
    bool hasSessionFaceIndex = false;
    if (!sessionColumns.exec(QStringLiteral("PRAGMA table_info(collection_sessions)"))) {
        m_lastError = sessionColumns.lastError().text();
        return false;
    }
    while (sessionColumns.next()) {
        if (sessionColumns.value(1).toString() == QStringLiteral("face_index")) {
            hasSessionFaceIndex = true;
            break;
        }
    }
    if (!hasSessionFaceIndex) {
        m_lastError = QStringLiteral("collection_sessions 缺少 face_index 列");
        return false;
    }
    bool hasRadarArrayType = false;
    bool hasDeviceBindings = false;
    bool hasSourceTemplate = false;
    bool hasControlSettings = false;
    bool hasArchivedUtcMs = false;
    if (!columns.exec(QStringLiteral("PRAGMA table_info(tasks)"))) {
        m_lastError = columns.lastError().text();
        return false;
    }
    while (columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("radar_array_type")) {
            hasRadarArrayType = true;
        } else if (columns.value(1).toString() == QStringLiteral("device_bindings_json")) {
            hasDeviceBindings = true;
        } else if (columns.value(1).toString() == QStringLiteral("source_template_uuid")) {
            hasSourceTemplate = true;
        } else if (columns.value(1).toString() == QStringLiteral("control_settings_json")) {
            hasControlSettings = true;
        } else if (columns.value(1).toString() == QStringLiteral("archived_utc_ms")) {
            hasArchivedUtcMs = true;
        }
    }
    if (!hasRadarArrayType || !hasDeviceBindings || !hasSourceTemplate
        || !hasControlSettings || !hasArchivedUtcMs) {
        m_lastError = QStringLiteral("tasks 缺少雷达形态、设备绑定、控制参数快照或归档时间列");
        return false;
    }
    bool hasFacesJson = false;
    bool hasObsoleteWholeDevicePose = false;
    if (!columns.exec(QStringLiteral("PRAGMA table_info(devices)"))) {
        m_lastError = columns.lastError().text();
        return false;
    }
    while (columns.next()) {
        const QString name = columns.value(1).toString();
        if (name == QStringLiteral("faces_json")) {
            hasFacesJson = true;
        } else if (name == QStringLiteral("absolute_heading_deg")
                   || name == QStringLiteral("pitch_deg")
                   || name == QStringLiteral("roll_deg")) {
            hasObsoleteWholeDevicePose = true;
        }
    }
    if (!hasFacesJson || hasObsoleteWholeDevicePose) {
        m_lastError = QStringLiteral("devices未收敛为逐阵面安装姿态Schema");
        return false;
    }
    return true;
}

/**
 * @brief 在单一事务中先收口旧 Running 记录，再插入带 UUID、PID 和版本的当前 run。
 * @details 异常检测时间只表示本次发现时刻；不猜测上次断电或崩溃的实际发生时间。
 */
bool SqliteArchiveRepository::beginApplicationRun()
{
    const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();
    if (nowUtcMs <= 0 || !executeSql(QStringLiteral("BEGIN IMMEDIATE"))) {
        if (nowUtcMs <= 0) m_lastError = QStringLiteral("系统 UTC 时间无效，不能登记运行实例");
        return false;
    }

    /** @brief 修复旧版正常 run 遗留的活动会话，只使用已记录正常退出时刻，不猜测异常退出时间。 */
    QSqlQuery repairCleanSessions(m_database);
    if (!repairCleanSessions.exec(QStringLiteral(
        "UPDATE collection_sessions SET stop_reason=3,stopped_utc_ms=("
        "SELECT ended_utc_ms FROM application_runs WHERE application_runs.run_id=collection_sessions.run_id) "
        "WHERE stop_reason=0 AND run_id IN(SELECT run_id FROM application_runs WHERE shutdown_state=1)")))
        return rollbackWithError(repairCleanSessions.lastError().text());
    QSqlQuery markAbnormal(m_database);
    markAbnormal.prepare(QStringLiteral(
        "UPDATE application_runs SET abnormal_detected_utc_ms=?, shutdown_state=2 "
        "WHERE shutdown_state=0"));
    markAbnormal.addBindValue(nowUtcMs);
    if (!markAbnormal.exec()) {
        return rollbackWithError(markAbnormal.lastError().text());
    }

    QSqlQuery closeAbandonedSessions(m_database);
    closeAbandonedSessions.prepare(QStringLiteral(
        "UPDATE collection_sessions SET stop_reason=5 "
        "WHERE stop_reason=0 AND run_id IN("
        "SELECT run_id FROM application_runs WHERE shutdown_state=2)"));
    if (!closeAbandonedSessions.exec()) {
        return rollbackWithError(closeAbandonedSessions.lastError().text());
    }

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO application_runs("
        "run_uuid,started_utc_ms,application_version,schema_version,process_id,shutdown_state) "
        "VALUES(?,?,?,?,?,0)"));
    insert.addBindValue(QUuid::createUuid().toRfc4122());
    insert.addBindValue(nowUtcMs);
    insert.addBindValue(m_applicationVersion);
    insert.addBindValue(kSchemaVersion);
    insert.addBindValue(static_cast<qint64>(QCoreApplication::applicationPid()));
    if (!insert.exec()) {
        return rollbackWithError(insert.lastError().text());
    }
    const qint64 runId = insert.lastInsertId().toLongLong();
    if (runId <= 0) {
        return rollbackWithError(QStringLiteral("SQLite 未返回有效 application_runs 主键"));
    }
    if (!executeSql(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(m_database);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return false;
    }
    m_runId = runId;
    m_runStartedUtcMs = nowUtcMs;
    return true;
}

/**
 * @brief 准备两个固定列集合的 INSERT，使正常批次不重复解析 SQL 文本。
 */
bool SqliteArchiveRepository::prepareInsertQueries()
{
    m_trackInsert = std::make_unique<QSqlQuery>(m_database);
    if (!m_trackInsert->prepare(QStringLiteral(
            "INSERT INTO track_observations("
            "run_id,collection_session_id,topic_sequence,receive_time_utc_ms,source_time_utc_ms,"
            "receive_time_steady_ns,source_sequence_u64,trace_id_u64,quality,track_id,range_m,"
            "azimuth_rad,elevation_rad,radial_velocity_mps,amplitude,pack_flag,face_index,scan_sequence,recognition_status,recognition_type) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
        m_lastError = m_trackInsert->lastError().text();
        return false;
    }
    m_eventInsert = std::make_unique<QSqlQuery>(m_database);
    if (!m_eventInsert->prepare(QStringLiteral(
            "INSERT INTO system_events("
            "run_id,topic_sequence,receive_time_utc_ms,source_time_utc_ms,receive_time_steady_ns,"
            "source_sequence_u64,trace_id_u64,quality,device_revision_id,task_id,collection_session_id,"
            "endpoint_key,track_id,request_id_u64,event_code,event_domain,severity,subject_flags,argument0,"
            "argument1,message_template_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
        m_lastError = m_eventInsert->lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief 执行单条 SQL；失败时立即复制错误文本，避免 query 析构后丢失诊断信息。
 */
bool SqliteArchiveRepository::executeSql(const QString &sql)
{
    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

/**
 * @brief 保留触发回滚的首因，并在 ROLLBACK 自身失败时追加数据库错误。
 */
bool SqliteArchiveRepository::rollbackWithError(const QString &error)
{
    m_lastError = error.isEmpty() ? QStringLiteral("SQLite 批次事务失败") : error;
    QSqlQuery rollback(m_database);
    if (!rollback.exec(QStringLiteral("ROLLBACK"))) {
        m_lastError += QStringLiteral("；ROLLBACK 失败：%1").arg(rollback.lastError().text());
    }
    return false;
}

/**
 * @brief 先销毁 prepared query、移除连接，再释放数据库进程锁；第二个 Core 不能在连接释放前开始恢复。
 */
void SqliteArchiveRepository::closeConnection() noexcept
{
    const QString connectionName = m_connectionName;
    m_trackInsert.reset();
    m_eventInsert.reset();
    if (m_database.isValid()) {
        m_database.close();
    }
    m_database = QSqlDatabase();
    if (!connectionName.isEmpty() && QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
    }
    m_connectionName.clear();
    m_ownerThread = nullptr;
    m_runId = 0;
    m_runStartedUtcMs = 0;
    m_endpointKeys.clear();
    m_deviceRevisionIds.clear();
    m_taskIds.clear();
    m_collectionBindings.clear();
    m_databaseLock.reset();
}

/**
 * @brief 使用显式大端转换保存无符号 64 位位模式，避免 SQLite signed INTEGER 截断高位。
 */
QByteArray SqliteArchiveRepository::encodeUnsigned64(quint64 value)
{
    const quint64 bigEndian = qToBigEndian(value);
    return QByteArray(reinterpret_cast<const char*>(&bigEndian), sizeof(bigEndian));
}

} // namespace core::archive
