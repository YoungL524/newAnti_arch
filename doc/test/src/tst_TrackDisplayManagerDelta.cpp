#include <QtTest>

#include "DataStore.h"
#include "DeviceRegistry.h"
#include "SystemBus.h"
#include "TaskService.h"
#include "TrackDisplayManager.h"
#include "TrackTypes.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

class TrackDisplayManagerDeltaTest : public QObject
{
    Q_OBJECT

    static DataPacket trackPacket(const QString& taskId, uint64_t traceId, qint64 timestamp)
    {
        auto track = std::make_shared<TrackPacket>();
        track->traceId = traceId;
        track->deviceId = QStringLiteral("radar-1");
        track->taskId = taskId;
        track->trackId = 99;
        track->receivedAtMs = timestamp;
        track->rangeM = 1000.0 + traceId;
        track->recognitionStatus = RecognitionStatus::Completed;

        DataPacket packet;
        packet.sourceType = DEVICE_RADAR_NH_KU_1;
        packet.dispatchTarget = DISPATCH_TRACK_DISPLAY_MANAGER;
        packet.traceId = traceId;
        packet.command = DataCommand::Track;
        packet.payload = std::shared_ptr<const TrackPacket>(std::move(track));
        return packet;
    }

private slots:
    void rollingWindowPublishesOneUpsertAndOneRemoval()
    {
        DataStore store;
        QVERIFY(store.connectToSqlite(QStringLiteral(":memory:")));
        DeviceRegistry devices(&store);
        QVERIFY(devices.ensureSchema());
        DeviceInfo radar;
        radar.id = QStringLiteral("radar-1");
        radar.name = radar.id;
        radar.kind = DeviceKind::Radar;
        radar.endpointId = QStringLiteral("radar_cpu");
        QVERIFY(devices.create(radar));

        TaskService tasks(&store, &devices);
        QVERIFY(tasks.ensureSchema());
        task::TaskInfo task;
        task.name = QStringLiteral("delta-test");
        task.deviceIds = QStringList{radar.id};
        const auto created = tasks.create(task);
        QVERIFY(created.has_value());
        QVERIFY(tasks.activate(created->id));

        QMutex updatesMutex;
        QVector<std::shared_ptr<const TrackDisplayUpdate>> updates;
        const uint32_t outputSub = SystemBus::getInstance().subscribeBySource(
            SOURCE_TRACK_DISPLAY,
            [&updatesMutex, &updates](const DataPacket* packet) {
                if (!packet || packet->command != DataCommand::TrackDisplayUpdate)
                    return;
                const auto update = packet->sharedPayloadAs<TrackDisplayUpdate>();
                if (!update)
                    return;
                QMutexLocker lock(&updatesMutex);
                updates.append(update);
            });
        QVERIFY(outputSub != 0);

        TrackDisplayManager manager;
        manager.setTaskService(&tasks);
        manager.start();
        manager.publishReset();

        const qint64 base = QDateTime::currentMSecsSinceEpoch();
        for (uint64_t i = 0; i < 601; ++i)
            QVERIFY(SystemBus::getInstance().publish(trackPacket(created->id, i + 1, base + i)));

        auto hasWindowFill = [&]() {
            QMutexLocker lock(&updatesMutex);
            for (const auto& update : updates) {
                if (update->mode == TrackDisplayUpdateMode::Delta
                    && update->upserts.size() == 600) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasWindowFill(), 4000);

        QVERIFY(SystemBus::getInstance().publish(trackPacket(created->id, 602, base + 601)));
        auto hasRollingDelta = [&]() {
            QMutexLocker lock(&updatesMutex);
            for (const auto& update : updates) {
                if (update->mode == TrackDisplayUpdateMode::Delta
                    && update->upserts.size() == 1 && update->removals.size() == 1) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasRollingDelta(), 4000);

        manager.stop();
        QVERIFY(SystemBus::getInstance().unsubscribe(outputSub));
    }
};

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TrackDisplayManagerDeltaTest)
#include "tst_TrackDisplayManagerDelta.moc"
