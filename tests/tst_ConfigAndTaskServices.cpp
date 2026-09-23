/**
 * @file tst_ConfigAndTaskServices.cpp
 * @brief 验证 SQLite 配置真源、不可变设备 revision、任务和多次采集会话的 owner 合同。
 * @details 测试不实现厂家协议或 socket；它防止空协议配置被误启动、旧 revision 句柄被热替换、数据库提交前
 * 暴露 CompactId、采集会话复用 AcquisitionId，以及重启后依赖旧进程句柄数值。
 */

#include "core/archive/SqliteArchiveRepository.h"
#include "core/config/DeviceConfigService.h"
#include "core/persistence/SqliteRuntimeRepository.h"
#include "core/model/StableIdRegistry.h"
#include "core/task/TaskService.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>

namespace {

/**
 * @brief 构造只有通用 UDP 航迹端点、尚未绑定厂家协议实现的有效设备配置。
 */
core::config::DeviceConfiguration makeDeviceConfiguration()
{
    core::config::DeviceConfiguration configuration;
    configuration.displayName = QStringLiteral("NODE_ALPHA_01");
    configuration.manufacturer = QStringLiteral("待定");
    configuration.model = QStringLiteral("待定");
    configuration.arrayType = core::config::RadarArrayType::SingleFace;
    configuration.maxRangeM = 5000.0;
    configuration.facesJson = QStringLiteral(
        "[{\"faceIndex\":0,\"headingDeg\":180,\"pitchDeg\":0,\"rollDeg\":0,"
        "\"scanHalfAngleDeg\":45,\"enabled\":true}]");
    configuration.longitudeDeg = 118.65309;
    configuration.latitudeDeg = 31.93502;
    configuration.altitudeM = 10.0;
    configuration.endpointsJson = QStringLiteral(
        "[{\"endpointKey\":\"track-rx\",\"role\":\"track_receive\","
        "\"transport\":\"udp\",\"localAddress\":\"0.0.0.0\","
        "\"localPort\":3456,\"remoteAddress\":\"\",\"remotePort\":0}]");
    configuration.protocolConfigJson = QStringLiteral("{}");
    configuration.changedBy = QStringLiteral("tester");
    return configuration;
}

/**
 * @brief 构造使用正式 Service 返回句柄的可归档航迹记录。
 */
core::domain::TopicRecord<core::model::TrackPacket> makeTrackRecord(
    core::model::TaskId taskId,
    core::model::DeviceId deviceId,
    core::model::EndpointId endpointId,
    core::model::AcquisitionId acquisitionId)
{
    core::domain::TopicRecord<core::model::TrackPacket> record;
    record.sequence = 1;
    record.sample.meta.endpointId = endpointId;
    record.sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    record.sample.meta.receiveTimeSteadyNs = 1000;
    record.sample.meta.traceId = 2000;
    record.sample.value.taskId = taskId;
    record.sample.value.deviceId = deviceId;
    record.sample.value.acquisitionId = acquisitionId;
    record.sample.value.trackId = 42;
    record.sample.value.rangeM = 1000.0;
    record.sample.value.azimuthRad = 0.1;
    record.sample.value.elevationRad = 0.2;
    record.sample.value.radialVelocityMps = 12.0;
    record.sample.value.amplitude = 48.5;
    record.sample.value.recognitionStatus = core::model::RecognitionStatus::Completed;
    return record;
}

/**
 * @brief 在 Repository 关闭后使用独立只读式短连接查询单个整数验收值。
 */
qint64 queryInteger(const QString &databasePath, const QString &sql)
{
    qint64 result = -1;
    const QString connectionName = QStringLiteral("test.services.query.%1")
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

/** @brief 汇总 DeviceConfigService 与 TaskService 的真实 SQLite 生命周期验收。 */
class ConfigAndTaskServicesTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收配置 revision、协议就绪门禁、会话启停、归档绑定和重启恢复。 */
    void ownsPersistentConfigurationAndCollectionLifecycle();
};

/**
 * @brief 先保存未配置协议的 revision 并验证不能采集，再追加已配置 revision 完成两次进程会话。
 */
void ConfigAndTaskServicesTest::ownsPersistentConfigurationAndCollectionLifecycle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("services.db"));
    QUuid deviceUuid;
    QUuid taskUuid;

    {
        core::archive::SqliteArchiveRepository database(databasePath, QStringLiteral("test-1.0"));
        QVERIFY2(database.open(), qPrintable(database.lastError()));
        core::model::StableIdRegistry stableIds;
        core::persistence::SqliteRuntimeRepository runtimeRepository(database);
        core::config::DeviceConfigService deviceService(runtimeRepository, database, stableIds);
        core::domain::LatestState<core::model::TaskState> taskState;
        core::task::TaskService taskService(
            runtimeRepository, deviceService, database, stableIds, taskState);
        QVERIFY(deviceService.loadFromDatabase());
        QVERIFY(taskService.loadFromDatabase());
        QVector<QUuid> templateUuids;
        QVector<QString> templateNames;
        QVector<core::task::RadarControlSettings> templateSettings;
        QVERIFY(taskService.listControlTemplates(templateUuids, templateNames, templateSettings));
        QCOMPARE(templateUuids.size(), 1);
        QCOMPARE(templateNames.size(), 1);

        auto configuration = makeDeviceConfiguration();
        core::model::DeviceId unconfiguredDevice;
        core::model::EndpointId unconfiguredEndpoint;
        QVERIFY2(deviceService.saveAndActivate(
                     configuration, unconfiguredDevice, unconfiguredEndpoint),
                 qPrintable(deviceService.lastError()));
        QCOMPARE(configuration.revision, quint32(1));
        deviceUuid = configuration.deviceUuid;

        core::task::TaskDefinition task;
        task.taskName = QStringLiteral("test01");
        task.operatorName = QStringLiteral("operator");
        task.radarArrayType = core::config::RadarArrayType::SingleFace;
        task.sourceTemplateUuid = templateUuids.front();
        task.deviceBindings.push_back(core::task::TaskDeviceBinding {deviceUuid, 0x01U});
        core::task::TaskDefinition mismatched = task;
        mismatched.radarArrayType = core::config::RadarArrayType::FourFace;
        core::model::TaskId rejectedTaskId;
        QVERIFY(!taskService.createTask(mismatched, rejectedTaskId));
        QVERIFY(!rejectedTaskId.isSet());
        core::model::TaskId taskId;
        QVERIFY2(taskService.createTask(task, taskId), qPrintable(taskService.lastError()));
        taskUuid = task.taskUuid;
        QVector<core::task::TaskDefinition> createdTasks;
        QVERIFY(taskService.listTasks(createdTasks));
        QCOMPARE(createdTasks.size(), 1);
        QCOMPARE(createdTasks[0].deviceBindings[0].faceMask, quint8(0x01U));
        core::task::TaskDefinition activeTask;
        QVERIFY2(taskService.activateTask(taskUuid, activeTask), qPrintable(taskService.lastError()));
        QCOMPARE(activeTask.taskUuid, taskUuid);
        QVERIFY(taskService.isTaskActive(taskUuid));
        QVERIFY(taskService.activeTask(activeTask));
        const auto readyState = taskState.latest();
        QVERIFY(readyState.has_value());
        QCOMPARE(readyState->sample->value.taskId, taskId);
        QCOMPARE(readyState->sample->value.phase, core::model::TaskPhase::Ready);
        QVERIFY(!taskService.activateTask(taskUuid, activeTask));

        core::model::AcquisitionId rejectedAcquisition;
        QVERIFY(!taskService.startCollection(
            taskId, unconfiguredDevice, unconfiguredEndpoint, 0U, rejectedAcquisition));
        QVERIFY(!rejectedAcquisition.isSet());

        configuration.protocolAdapterKey = QStringLiteral("test.radar.protocol");
        configuration.changeReason = QStringLiteral("绑定真实协议实现前的架构测试键");
        core::model::DeviceId deviceId;
        core::model::EndpointId endpointId;
        QVERIFY2(deviceService.saveAndActivate(configuration, deviceId, endpointId),
                 qPrintable(deviceService.lastError()));
        QCOMPARE(configuration.revision, quint32(2));
        QVERIFY(deviceId != unconfiguredDevice);
        QVERIFY(endpointId != unconfiguredEndpoint);

        auto foreignConfiguration = makeDeviceConfiguration();
        foreignConfiguration.displayName = QStringLiteral("未绑定到任务的雷达");
        foreignConfiguration.protocolAdapterKey = QStringLiteral("test.radar.protocol");
        core::model::DeviceId foreignDeviceId;
        core::model::EndpointId foreignEndpointId;
        QVERIFY2(deviceService.saveAndActivate(
                     foreignConfiguration, foreignDeviceId, foreignEndpointId),
                 qPrintable(deviceService.lastError()));
        core::model::AcquisitionId foreignAcquisition;
        QVERIFY(!taskService.startCollection(
            taskId, foreignDeviceId, foreignEndpointId, 0U, foreignAcquisition));
        QVERIFY(!foreignAcquisition.isSet());

        std::array<core::model::AcquisitionId, 4> acquisitionIds {};
        QVERIFY2(taskService.startCollections(taskId, deviceId, endpointId, 0x01U, acquisitionIds),
                 qPrintable(taskService.lastError()));
        const core::model::AcquisitionId acquisitionId = acquisitionIds[0];
        QVERIFY(acquisitionId.isSet());
        QVERIFY(!acquisitionIds[1].isSet());
        const auto runningState = taskState.latest();
        QVERIFY(runningState.has_value());
        QCOMPARE(runningState->sample->value.phase, core::model::TaskPhase::Running);
        QCOMPARE(stableIds.resolveDevice(deviceId), configuration.deviceUuid);
        QCOMPARE(stableIds.resolveTask(taskId), task.taskUuid);
        QVERIFY(!stableIds.resolveAcquisition(acquisitionId).isNull());
        QVERIFY(taskService.isCollectionActive(acquisitionId));

        const auto track = makeTrackRecord(taskId, deviceId, endpointId, acquisitionId);
        QVERIFY2(database.appendTrackBatch(&track, 1), qPrintable(database.lastError()));
        QVERIFY(taskService.stopCollection(
            acquisitionId,
            core::task::CollectionStopReason::UserStop,
            QDateTime::currentMSecsSinceEpoch()));
        QVERIFY(!taskService.isCollectionActive(acquisitionId));
        QVERIFY(!taskService.stopCollection(
            acquisitionId,
            core::task::CollectionStopReason::UserStop,
            QDateTime::currentMSecsSinceEpoch()));
        core::task::TaskDefinition completedTask;
        QVERIFY2(taskService.completeTask(taskUuid, completedTask), qPrintable(taskService.lastError()));
        QVERIFY(completedTask.completedUtcMs > 0);
        QVERIFY(!taskService.activeTask(activeTask));
        const auto clearedState = taskState.latest();
        QVERIFY(clearedState.has_value());
        QCOMPARE(clearedState->sample->value.phase, core::model::TaskPhase::None);
        QVERIFY(!clearedState->sample->value.taskId.isSet());
        QVERIFY(database.closeCleanly(QDateTime::currentMSecsSinceEpoch()));
    }

    {
        core::archive::SqliteArchiveRepository database(databasePath, QStringLiteral("test-1.1"));
        QVERIFY2(database.open(), qPrintable(database.lastError()));
        core::model::StableIdRegistry stableIds;
        core::persistence::SqliteRuntimeRepository runtimeRepository(database);
        core::config::DeviceConfigService deviceService(runtimeRepository, database, stableIds);
        core::domain::LatestState<core::model::TaskState> taskState;
        core::task::TaskService taskService(
            runtimeRepository, deviceService, database, stableIds, taskState);
        QVERIFY2(deviceService.loadFromDatabase(), qPrintable(deviceService.lastError()));
        QVERIFY2(taskService.loadFromDatabase(), qPrintable(taskService.lastError()));

        core::model::DeviceId restoredDevice;
        core::model::EndpointId restoredEndpoint;
        core::model::TaskId restoredTask;
        QVERIFY(deviceService.resolveLatestDevice(
            deviceUuid, restoredDevice, restoredEndpoint));
        QVERIFY(taskService.resolveTask(taskUuid, restoredTask));
        QVERIFY(deviceService.isCollectionReady(restoredDevice, restoredEndpoint));
        QVector<core::task::TaskDefinition> restoredTasks;
        QVERIFY(taskService.listTasks(restoredTasks));
        QCOMPARE(restoredTasks.size(), 1);
        QCOMPARE(restoredTasks[0].taskUuid, taskUuid);
        QVERIFY(restoredTasks[0].completedUtcMs > 0);
        QCOMPARE(restoredTasks[0].radarArrayType, core::config::RadarArrayType::SingleFace);
        QCOMPARE(restoredTasks[0].deviceBindings[0].deviceUuid, deviceUuid);

        core::model::AcquisitionId secondAcquisition;
        QVERIFY(!taskService.startCollection(
            restoredTask, restoredDevice, restoredEndpoint, 0U, secondAcquisition));
        QVERIFY(!secondAcquisition.isSet());
        QVERIFY2(taskService.softDeleteTask(taskUuid, QStringLiteral("tester")),
                 qPrintable(taskService.lastError()));
        QVERIFY(!taskService.resolveTask(taskUuid, restoredTask));
        restoredTasks.clear();
        QVERIFY(taskService.listTasks(restoredTasks));
        QVERIFY(restoredTasks.isEmpty());
        QVERIFY(database.closeCleanly(QDateTime::currentMSecsSinceEpoch()));
    }

    /** @brief 验收 Schema 9 逐阵面姿态迁移保留revision，并计入任务外设备拒绝用例产生的独立台账。 */
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM devices")), qint64(7));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT count(*) FROM tasks")), qint64(1));
    QCOMPARE(queryInteger(databasePath, QStringLiteral("SELECT is_deleted FROM tasks")), qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM collection_sessions")),
             qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM track_observations")),
             qint64(1));
    QCOMPARE(queryInteger(
                 databasePath,
                 QStringLiteral("SELECT count(*) FROM application_runs WHERE shutdown_state=1")),
             qint64(2));
}

/**
 * @brief 使用非 GUI QtTest 入口验证低频配置/任务 owner 与 QSQLITE 插件。
 */
QTEST_GUILESS_MAIN(ConfigAndTaskServicesTest)

#include "tst_ConfigAndTaskServices.moc"
