/**
 * @file tst_SqliteArchiveRepository.cpp
 * @brief 验证 QSQLITE 六表初始化、运行实例诊断和强类型批量事务边界。
 * @details 测试使用 QTemporaryDir 中的真实数据库文件，不使用内存 Fake；它防止 Schema 漂移、异常退出被误记
 * 为正常、CompactId 未解析就落库、批次部分提交以及可选字段零值/NULL 语义退化。
 */

#include "core/archive/SqliteArchiveRepository.h"
#include "core/archive/ArchiveReader.h"
#include "core/datadomain/CoreDataDomain.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>

namespace {

/** @brief 保存测试配置事务创建的三个数据库主键，不进入生产接口。 */
struct SeededContext
{
    qint64 deviceRevisionId = 0; ///< devices revision 测试主键。
    qint64 taskId = 0; ///< tasks 测试主键。
    qint64 collectionSessionId = 0; ///< collection_sessions 测试主键。
};

/**
 * @brief 使用独立短期连接模拟配置/任务服务已在同一 SQLite 文件建立实体。
 * @param databasePath Repository 已完成六表初始化的数据库文件。
 * @param runId 当前 application_runs 主键，采集会话不得跨 run。
 * @return 三个正主键；任一 SQL 失败时通过 QtTest 立即终止当前测试。
 */
SeededContext seedConfiguration(const QString &databasePath, qint64 runId)
{
    SeededContext result;
    const QString connectionName = QStringLiteral("test.seed.%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            qWarning() << database.lastError().text();
            return result;
        }
        QSqlQuery pragma(database);
        if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"))) {
            qWarning() << pragma.lastError().text();
            return result;
        }

        QSqlQuery device(database);
        device.prepare(QStringLiteral(
            "INSERT INTO devices("
            "device_uuid,revision,display_name,manufacturer,model,protocol_adapter_key,"
            "array_type,max_range_m,faces_json,longitude_deg,latitude_deg,altitude_m,"
            "endpoints_json,protocol_config_json,changed_utc_ms,changed_by,change_reason,is_deleted) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)"));
        device.addBindValue(QUuid::createUuid().toRfc4122());
        device.addBindValue(1);
        device.addBindValue(QStringLiteral("NODE_ALPHA_01"));
        device.addBindValue(QStringLiteral("云禾锋"));
        device.addBindValue(QStringLiteral("test"));
        device.addBindValue(QStringLiteral("test.radar"));
        device.addBindValue(1);
        device.addBindValue(5000.0);
        device.addBindValue(QStringLiteral(
            "[{\"faceIndex\":0,\"headingDeg\":180,\"pitchDeg\":0,\"rollDeg\":0,"
            "\"scanHalfAngleDeg\":45,\"enabled\":true}]"));
        device.addBindValue(118.65309);
        device.addBindValue(31.93502);
        device.addBindValue(10.0);
        device.addBindValue(QStringLiteral(
            "[{\"endpointKey\":\"track-rx\",\"role\":\"track_receive\","
            "\"transport\":\"udp\",\"localAddress\":\"0.0.0.0\","
            "\"localPort\":3456,\"remoteAddress\":\"\",\"remotePort\":0}]"));
        device.addBindValue(QStringLiteral("{}"));
        device.addBindValue(QDateTime::currentMSecsSinceEpoch());
        device.addBindValue(QStringLiteral("test"));
        device.addBindValue(QStringLiteral("fixture"));
        if (!device.exec()) {
            qWarning() << device.lastError().text();
            return result;
        }
        result.deviceRevisionId = device.lastInsertId().toLongLong();

        QSqlQuery task(database);
        task.prepare(QStringLiteral(
            "INSERT INTO tasks(task_uuid,task_name,operator_name,description,created_utc_ms,updated_utc_ms) "
            "VALUES(?,?,?,?,?,?)"));
        const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();
        task.addBindValue(QUuid::createUuid().toRfc4122());
        task.addBindValue(QStringLiteral("test01"));
        task.addBindValue(QStringLiteral("operator"));
        task.addBindValue(QStringLiteral("SQLite integration"));
        task.addBindValue(nowUtcMs);
        task.addBindValue(nowUtcMs);
        if (!task.exec()) {
            qWarning() << task.lastError().text();
            return result;
        }
        result.taskId = task.lastInsertId().toLongLong();

        QSqlQuery session(database);
        session.prepare(QStringLiteral(
            "INSERT INTO collection_sessions("
            "session_uuid,run_id,task_id,device_revision_id,endpoint_key,face_index,started_utc_ms,stop_reason) "
            "VALUES(?,?,?,?,?,?,?,0)"));
        session.addBindValue(QUuid::createUuid().toRfc4122());
        session.addBindValue(runId);
        session.addBindValue(result.taskId);
        session.addBindValue(result.deviceRevisionId);
        session.addBindValue(QStringLiteral("track-rx"));
        session.addBindValue(0);
        session.addBindValue(nowUtcMs);
        if (!session.exec()) {
            qWarning() << session.lastError().text();
            return result;
        }
        result.collectionSessionId = session.lastInsertId().toLongLong();
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

/**
 * @brief 构造已通过发布门禁语义的单条航迹 TopicRecord。
 * @param sequence TrackTopic 内部序号。
 * @param trackId 厂家航迹号。
 * @return 带绑定句柄、有效时间和 Completed 识别状态的记录。
 */
core::domain::TopicRecord<core::model::TrackPacket> makeTrackRecord(
    core::domain::TopicSequence sequence,
    quint32 trackId)
{
    core::domain::TopicRecord<core::model::TrackPacket> record;
    record.sequence = sequence;
    record.sample.meta.endpointId = core::model::EndpointId::fromRaw(1);
    record.sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    record.sample.meta.receiveTimeSteadyNs = 1000 + static_cast<qint64>(sequence);
    record.sample.meta.traceId = 2000 + sequence;
    record.sample.value.acquisitionId = core::model::AcquisitionId::fromRaw(1);
    record.sample.value.deviceId = core::model::DeviceId::fromRaw(1);
    record.sample.value.taskId = core::model::TaskId::fromRaw(1);
    record.sample.value.trackId = trackId;
    record.sample.value.rangeM = 1000.0;
    record.sample.value.azimuthRad = 0.1;
    record.sample.value.elevationRad = 0.2;
    record.sample.value.radialVelocityMps = 12.0;
    record.sample.value.amplitude = 48.5;
    record.sample.value.packFlag = 1;
    record.sample.value.recognitionStatus = core::model::RecognitionStatus::Completed;
    return record;
}

/**
 * @brief 构造关联当前采集会话和航迹的已登记 Track 域系统事件。
 * @return flags 与所有可选字段严格一致的事件 TopicRecord。
 */
core::domain::TopicRecord<core::model::SystemEvent> makeEventRecord()
{
    core::domain::TopicRecord<core::model::SystemEvent> record;
    record.sequence = 1;
    record.sample.meta.endpointId = core::model::EndpointId::fromRaw(1);
    record.sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    record.sample.meta.receiveTimeSteadyNs = 2000;
    record.sample.meta.traceId = 3000;
    auto &event = record.sample.value;
    event.deviceId = core::model::DeviceId::fromRaw(1);
    event.taskId = core::model::TaskId::fromRaw(1);
    event.acquisitionId = core::model::AcquisitionId::fromRaw(1);
    event.trackId = 42;
    event.argument0 = 7;
    event.eventCode = core::model::EventCode::TrackPublishRejected;
    event.domain = core::model::EventDomain::Track;
    event.severity = core::model::EventSeverity::Warning;
    event.flags = core::model::eventSubjectBit(core::model::EventSubjectFlag::DeviceIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::TaskIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::AcquisitionIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::TrackIdValid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument0Valid);
    return record;
}

/**
 * @brief 只读查询单个整数结果，并在数据库或 SQL 错误时终止当前测试。
 */
qint64 queryInteger(const QString &databasePath, const QString &sql)
{
    qint64 result = -1;
    const QString connectionName = QStringLiteral("test.query.%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            qWarning() << database.lastError().text();
            return result;
        }
        QSqlQuery query(database);
        if (!query.exec(sql) || !query.next()) {
            qWarning() << query.lastError().text();
            return result;
        }
        result = query.value(0).toLongLong();
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

} // namespace

/** @brief 汇总真实 QSQLITE Repository 的首轮持久化验收。 */
class SqliteArchiveRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收新文件创建六张业务表、两套预置台账，且重复打开不会再次补写。 */
    void createsSixTablesAndClosesRun();

    /** @brief 验收句柄解析后航迹/事件落库，非法批次不会产生部分行。 */
    void persistsStrongTypesAndRollsBackInvalidBatch();

    /** @brief 验收未正常关闭的旧 run 在下一次启动时被标记为异常。 */
    void detectsAbandonedApplicationRun();

    /** @brief 验收 Doorbell 驱动 ArchiveReader 从两个 Topic 读取并提交真实 SQLite。 */
    void drainsTopicsThroughArchiveReader();
};

/**
 * @brief 打开空数据库两次，检查 Schema、预置台账幂等性和 Clean 运行状态。
 */
void SqliteArchiveRepositoryTest::createsSixTablesAndClosesRun()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("antidrone-v2.db"));
    core::archive::SqliteArchiveRepository repository(databasePath, QStringLiteral("test-1.0"));
    QVERIFY2(repository.open(), qPrintable(repository.lastError()));
    QVERIFY(repository.currentRunId() > 0);
    QVERIFY(repository.closeCleanly(QDateTime::currentMSecsSinceEpoch()));

    core::archive::SqliteArchiveRepository reopened(databasePath, QStringLiteral("test-1.0"));
    QVERIFY2(reopened.open(), qPrintable(reopened.lastError()));
    QVERIFY(reopened.closeCleanly(QDateTime::currentMSecsSinceEpoch()));

    QCOMPARE(queryInteger(databasePath, QStringLiteral("PRAGMA user_version")), qint64(11));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM pragma_table_info('collection_sessions') "
                                "WHERE name='face_index'")),
             qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM pragma_table_info('devices') WHERE name IN("
                                "'absolute_heading_deg','pitch_deg','roll_deg')")),
             qint64(0));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM devices WHERE "
                                "instr(faces_json,'\"headingDeg\"')>0 AND "
                                "instr(faces_json,'\"pitchDeg\"')>0 AND "
                                "instr(faces_json,'\"rollDeg\"')>0")),
             qint64(4));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM sqlite_master "
                                "WHERE type='table' AND name NOT LIKE 'sqlite_%'")),
             qint64(7));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM control_templates WHERE is_deleted=0")), qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM tasks WHERE source_template_uuid IS NULL "
                                "OR control_settings_json IS NULL")),
             qint64(0));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT shutdown_state FROM application_runs LIMIT 1")),
             qint64(1));
    /** @brief Schema 10 保留既有revision数量，并把整机姿态迁入各自faces_json。 */
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM devices")), qint64(4));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM devices d WHERE array_type=1 AND is_deleted=0 "
                                "AND NOT EXISTS(SELECT 1 FROM devices newer "
                                "WHERE newer.device_uuid=d.device_uuid AND newer.revision>d.revision)")),
             qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM devices d WHERE is_deleted=0 "
                                "AND abs(longitude_deg-118.65303)<0.000001 "
                                "AND abs(latitude_deg-31.93494)<0.000001 "
                                "AND abs(altitude_m-63.9288)<0.000001 "
                                "AND NOT EXISTS(SELECT 1 FROM devices newer "
                                "WHERE newer.device_uuid=d.device_uuid AND newer.revision>d.revision)")),
             qint64(2));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM devices d WHERE array_type=4 AND is_deleted=0 "
                                "AND NOT EXISTS(SELECT 1 FROM devices newer "
                                "WHERE newer.device_uuid=d.device_uuid AND newer.revision>d.revision)")),
             qint64(1));
}

/**
 * @brief 建立配置/任务/会话绑定后提交两类事实，再用非法第二条记录验证整批回滚。
 */
void SqliteArchiveRepositoryTest::persistsStrongTypesAndRollsBackInvalidBatch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("archive.db"));
    core::archive::SqliteArchiveRepository repository(databasePath, QStringLiteral("test-1.0"));
    QVERIFY2(repository.open(), qPrintable(repository.lastError()));
    const SeededContext context = seedConfiguration(databasePath, repository.currentRunId());
    QVERIFY(context.deviceRevisionId > 0);
    QVERIFY(context.taskId > 0);
    QVERIFY(context.collectionSessionId > 0);

    QVERIFY(repository.bindEndpoint(core::model::EndpointId::fromRaw(1), QStringLiteral("track-rx")));
    QVERIFY(repository.bindDevice(core::model::DeviceId::fromRaw(1), context.deviceRevisionId));
    QVERIFY(repository.bindTask(core::model::TaskId::fromRaw(1), context.taskId));
    QVERIFY(repository.bindAcquisition(
        core::model::AcquisitionId::fromRaw(1),
        context.collectionSessionId,
        core::model::DeviceId::fromRaw(1),
        core::model::TaskId::fromRaw(1),
        core::model::EndpointId::fromRaw(1)));

    auto firstTrack = makeTrackRecord(1, 42);
    QVERIFY2(repository.appendTrackBatch(&firstTrack, 1), qPrintable(repository.lastError()));
    auto event = makeEventRecord();
    QVERIFY2(repository.appendEventBatch(&event, 1), qPrintable(repository.lastError()));

    core::domain::TopicRecord<core::model::TrackPacket> invalidBatch[2] {
        makeTrackRecord(2, 43), makeTrackRecord(3, 44)
    };
    invalidBatch[1].sample.value.rangeM = -1.0;
    QVERIFY(!repository.appendTrackBatch(invalidBatch, 2));
    QVERIFY(repository.closeCleanly(QDateTime::currentMSecsSinceEpoch()));

    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM track_observations")),
             qint64(1));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM system_events")),
             qint64(1));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT track_id FROM track_observations")),
             qint64(42));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT pack_flag FROM track_observations")),
             qint64(1));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT face_index FROM track_observations")),
             qint64(0));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT argument0 FROM system_events")),
             qint64(7));
}

/**
 * @brief 第一实例故意不调用 closeCleanly，第二实例启动后应收口旧 run 并正常关闭自身。
 */
void SqliteArchiveRepositoryTest::detectsAbandonedApplicationRun()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("runtime.db"));
    {
        core::archive::SqliteArchiveRepository abandoned(databasePath, QStringLiteral("test-1.0"));
        QVERIFY2(abandoned.open(), qPrintable(abandoned.lastError()));
        const SeededContext context = seedConfiguration(databasePath, abandoned.currentRunId());
        QVERIFY(context.collectionSessionId > 0);
    }
    {
        core::archive::SqliteArchiveRepository recovered(databasePath, QStringLiteral("test-1.1"));
        QVERIFY2(recovered.open(), qPrintable(recovered.lastError()));
        QVERIFY(recovered.closeCleanly(QDateTime::currentMSecsSinceEpoch()));
    }

    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM application_runs WHERE shutdown_state=2 "
                                "AND abnormal_detected_utc_ms IS NOT NULL")),
             qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM application_runs WHERE shutdown_state=1 "
                                "AND ended_utc_ms IS NOT NULL")),
             qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM collection_sessions WHERE stop_reason=5 "
                                "AND stopped_utc_ms IS NULL")),
             qint64(1));
}

/**
 * @brief 把有效事实发布到共享数据域，只依赖 Doorbell 和存储 Lane 追平两个 committed cursor。
 * @details 测试证明 SQLite Repository 没有绕过既定 Consumer 主动读取模型，也没有在 Reader 后增加消息队列。
 */
void SqliteArchiveRepositoryTest::drainsTopicsThroughArchiveReader()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("reader-integration.db"));
    core::archive::SqliteArchiveRepository repository(databasePath, QStringLiteral("test-1.0"));
    QVERIFY2(repository.open(), qPrintable(repository.lastError()));
    const SeededContext context = seedConfiguration(databasePath, repository.currentRunId());
    QVERIFY(context.collectionSessionId > 0);
    QVERIFY(repository.bindEndpoint(core::model::EndpointId::fromRaw(1), QStringLiteral("track-rx")));
    QVERIFY(repository.bindDevice(core::model::DeviceId::fromRaw(1), context.deviceRevisionId));
    QVERIFY(repository.bindTask(core::model::TaskId::fromRaw(1), context.taskId));
    QVERIFY(repository.bindAcquisition(
        core::model::AcquisitionId::fromRaw(1),
        context.collectionSessionId,
        core::model::DeviceId::fromRaw(1),
        core::model::TaskId::fromRaw(1),
        core::model::EndpointId::fromRaw(1)));

    core::domain::CoreDataDomain domain(16, 16);
    core::execution::ExecutionLane storageLane;
    {
        core::archive::ArchiveReader reader(
            domain.trackTopic(), domain.eventTopic(), storageLane, repository, 4, 4);
        for (quint64 sequence = 1; sequence <= 6; ++sequence) {
            const auto record = makeTrackRecord(sequence, static_cast<quint32>(100 + sequence));
            domain.trackTopic().publish(record.sample);
        }
        const auto event = makeEventRecord();
        domain.eventTopic().publish(event.sample);

        QTRY_COMPARE_WITH_TIMEOUT(
            reader.trackCommittedCursor(), core::domain::TopicSequence(6), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(
            reader.eventCommittedCursor(), core::domain::TopicSequence(1), 1000);
    }
    QVERIFY(repository.closeCleanly(QDateTime::currentMSecsSinceEpoch()));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM track_observations")),
             qint64(6));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM system_events")),
             qint64(1));
}

/**
 * @brief 使用非 GUI QtTest 入口并加载 QSQLITE 插件完成真实文件级事务测试。
 */
QTEST_GUILESS_MAIN(SqliteArchiveRepositoryTest)

#include "tst_SqliteArchiveRepository.moc"
