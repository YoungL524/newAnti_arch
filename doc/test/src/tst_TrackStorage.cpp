#include <QtTest>

#include "DataStore.h"
#include "GenericPackets.h"
#include "SystemBus.h"
#include "TrackStorage.h"
#include "TrackTypes.h"

#include <QJsonObject>
#include <QTemporaryDir>

class TrackStorageTest : public QObject
{
    Q_OBJECT

private:
    static QJsonObject sqliteConfig(const QString& path)
    {
        return QJsonObject{
            {QStringLiteral("driver"), QStringLiteral("QSQLITE")},
            {QStringLiteral("database"), path}
        };
    }

    static bool createTracksTable(const QString& path)
    {
        DataStore store;
        if (!store.connectToSqlite(path))
            return false;
        return store.ensureTable(QStringLiteral("tracks"), QStringLiteral(
            "CREATE TABLE tracks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, ts_ms BIGINT NOT NULL, source TEXT, "
            "task_id TEXT, device_id TEXT, scene_num INTEGER, target_id INTEGER, "
            "range_m REAL, azimuth_rad REAL, elevation_rad REAL, radial_speed REAL, "
            "altitude_m REAL, x REAL, y REAL, z REAL)"));
    }

    static DataPacket trackPacket(uint64_t traceId, const QString& taskId)
    {
        auto track = std::make_shared<TrackPacket>();
        track->traceId = traceId;
        track->endpointId = QStringLiteral("radar_cpu");
        track->deviceId = QStringLiteral("radar-1");
        track->taskId = taskId;
        track->receivedAtMs = 123456;
        track->sceneNumber = 7;
        track->trackId = static_cast<uint32_t>(traceId);
        track->rangeM = 800.0;

        DataPacket packet;
        packet.sourceType = DEVICE_RADAR_NH_KU_1;
        packet.dispatchTarget = DISPATCH_TRACK_DISPLAY_MANAGER;
        packet.traceId = traceId;
        packet.command = DataCommand::Track;
        packet.payload = std::shared_ptr<const TrackPacket>(std::move(track));
        return packet;
    }

private slots:
    void persistsAssignedTrackAndSkipsUnassigned()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("tracks.db"));
        QVERIFY(createTracksTable(path));

        TrackStorage storage;
        QVERIFY(storage.start(sqliteConfig(path)));
        QVERIFY(SystemBus::getInstance().publish(trackPacket(41, QStringLiteral("task-1"))));
        QVERIFY(SystemBus::getInstance().publish(trackPacket(42, QString())));

        QTRY_COMPARE_WITH_TIMEOUT(storage.stats().persisted, uint64_t(1), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(storage.stats().unassignedSkips, uint64_t(1), 3000);
        storage.stop();

        DataStore verify;
        QVERIFY(verify.connectToSqlite(path));
        const auto rows = verify.query(QStringLiteral(
            "SELECT task_id, device_id, target_id FROM tracks"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.front().value(QStringLiteral("task_id")).toString(),
                 QStringLiteral("task-1"));
        QCOMPARE(rows.front().value(QStringLiteral("device_id")).toString(),
                 QStringLiteral("radar-1"));
        QCOMPARE(rows.front().value(QStringLiteral("target_id")).toUInt(), uint(41));
    }

    void boundedQueueDropsWithoutBlockingPublisher()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("overload.db"));
        QVERIFY(createTracksTable(path));

        TrackStorage::Options options;
        options.queueCapacity = 1;
        options.batchSize = 1;
        options.flushIntervalMs = 10;
        TrackStorage storage(options);
        QVERIFY(storage.start(sqliteConfig(path)));

        DataStore lockStore;
        QVERIFY(lockStore.connectToSqlite(path));
        QVERIFY(lockStore.beginTransaction());
        QVERIFY(lockStore.insert(QStringLiteral("tracks"), {
            {QStringLiteral("ts_ms"), 1},
            {QStringLiteral("task_id"), QStringLiteral("lock")}
        }));

        QVERIFY(SystemBus::getInstance().publish(trackPacket(100, QStringLiteral("task-1"))));
        QTRY_VERIFY_WITH_TIMEOUT(storage.stats().accepted >= 1, 1000);
        for (uint64_t i = 101; i < 140; ++i)
            QVERIFY(SystemBus::getInstance().publish(trackPacket(i, QStringLiteral("task-1"))));

        QTRY_VERIFY_WITH_TIMEOUT(storage.stats().queueFullDrops > 0, 1000);
        lockStore.rollbackTransaction();
        storage.stop();
        QVERIFY(storage.stats().highWatermark <= options.queueCapacity);
    }
};

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TrackStorageTest)
#include "tst_TrackStorage.moc"
