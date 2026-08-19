#include <QtTest>
#include <QSemaphore>

#include "bus/SystemBus.h"
#include "business/TrackTypes.h"

#include <memory>

class TestTrackDataPacketBus : public QObject
{
    Q_OBJECT

private slots:
    void rejectsCommandPayloadMismatch();
    void publishesCompletedTrackOnceWithSharedIdentity();
};

/**
 * @brief 验证 rejectsCommandPayloadMismatch 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackDataPacketBus::rejectsCommandPayloadMismatch()
{
    DataPacket packet;
    packet.command = DataCommand::Track;
    packet.payload = workStandby{};

    QVERIFY(!packet.hasConsistentPayload());
    QVERIFY(!SystemBus::getInstance().publish(std::move(packet)));
}

/**
 * @brief 验证 publishesCompletedTrackOnceWithSharedIdentity 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackDataPacketBus::publishesCompletedTrackOnceWithSharedIdentity()
{
    auto mutableTrack = std::make_shared<TrackPacket>();
    mutableTrack->traceId = 99;
    mutableTrack->deviceId = QStringLiteral("radar_001");
    mutableTrack->taskId = QStringLiteral("task_001");
    mutableTrack->trackId = 42;
    mutableTrack->recognitionStatus = RecognitionStatus::Completed;
    std::shared_ptr<const TrackPacket> publishedTrack = mutableTrack;

    QSemaphore delivered;
    int callbackCount = 0;
    const TrackPacket* receivedIdentity = nullptr;
    const auto subscription = SystemBus::getInstance().subscribeByTarget(
        DISPATCH_TRACK_DISPLAY_MANAGER,
        [&](const DataPacket* packet) {
            if (!packet || packet->command != DataCommand::Track)
                return;
            const auto received = packet->sharedPayloadAs<TrackPacket>();
            if (!received)
                return;
            ++callbackCount;
            receivedIdentity = received.get();
            delivered.release();
        });
    QVERIFY(subscription != 0);

    DataPacket packet;
    packet.sourceType = DEVICE_RADAR_NH_KU_1;
    packet.dispatchTarget = DISPATCH_TRACK_DISPLAY_MANAGER;
    packet.packetSequence = 1;
    packet.traceId = publishedTrack->traceId;
    packet.command = DataCommand::Track;
    packet.payload = publishedTrack;

    QVERIFY(packet.hasConsistentPayload());
    QVERIFY(SystemBus::getInstance().publish(std::move(packet)));
    QVERIFY(delivered.tryAcquire(1, 2000));
    QCOMPARE(callbackCount, 1);
    QCOMPARE(receivedIdentity, publishedTrack.get());
    QVERIFY(SystemBus::getInstance().unsubscribe(subscription));
}

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TestTrackDataPacketBus)
#include "tst_TrackDataPacketBus.moc"
