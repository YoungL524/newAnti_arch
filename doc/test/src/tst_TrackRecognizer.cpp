#include <QtTest>

#include <limits>

#include "TrackRecognizer.h"

namespace {

/**
 * @brief 验证 populatedPacket 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
TrackPacket populatedPacket()
{
    TrackPacket packet;
    packet.schemaVersion = 3;
    packet.traceId = 12345;
    packet.endpointId = QStringLiteral("ep1");
    packet.deviceId = QStringLiteral("devA");
    packet.taskId = QStringLiteral("taskX");
    packet.receivedAtMs = 1000;
    packet.ingressSteadyNs = 2000;
    packet.normalizedSteadyNs = 3000;
    packet.recognizedSteadyNs = 0;
    packet.sceneNumber = 7;
    packet.trackId = 42;
    packet.rangeM = 123.5;
    packet.azimuthRad = 1.25;
    packet.elevationRad = -0.5;
    packet.radialSpeedMps = 17.0;
    packet.rcs = 8.5;
    packet.altitudeM = 90.0;
    packet.xM = 1.0;
    packet.yM = 2.0;
    packet.zM = 3.0;
    packet.vxMps = 4.0;
    packet.vyMps = 5.0;
    packet.vzMps = 6.0;
    return packet;
}

/**
 * @brief 验证 comparePacket 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void comparePacket(const TrackPacket& actual, const TrackPacket& expected)
{
    QCOMPARE(actual.schemaVersion, expected.schemaVersion);
    QCOMPARE(actual.traceId, expected.traceId);
    QCOMPARE(actual.endpointId, expected.endpointId);
    QCOMPARE(actual.deviceId, expected.deviceId);
    QCOMPARE(actual.taskId, expected.taskId);
    QCOMPARE(actual.receivedAtMs, expected.receivedAtMs);
    QCOMPARE(actual.ingressSteadyNs, expected.ingressSteadyNs);
    QCOMPARE(actual.normalizedSteadyNs, expected.normalizedSteadyNs);
    QCOMPARE(actual.recognizedSteadyNs, expected.recognizedSteadyNs);
    QCOMPARE(actual.sceneNumber, expected.sceneNumber);
    QCOMPARE(actual.trackId, expected.trackId);
    QCOMPARE(actual.rangeM, expected.rangeM);
    QCOMPARE(actual.azimuthRad, expected.azimuthRad);
    QCOMPARE(actual.elevationRad, expected.elevationRad);
    QCOMPARE(actual.radialSpeedMps, expected.radialSpeedMps);
    QCOMPARE(actual.rcs, expected.rcs);
    QCOMPARE(actual.altitudeM, expected.altitudeM);
    QCOMPARE(actual.xM, expected.xM);
    QCOMPARE(actual.yM, expected.yM);
    QCOMPARE(actual.zM, expected.zM);
    QCOMPARE(actual.vxMps, expected.vxMps);
    QCOMPARE(actual.vyMps, expected.vyMps);
    QCOMPARE(actual.vzMps, expected.vzMps);
    QCOMPARE(actual.recognitionStatus, expected.recognitionStatus);
    QCOMPARE(actual.recognitionType, expected.recognitionType);
}

} // namespace

class TestTrackRecognizer : public QObject
{
    Q_OBJECT

private slots:
    void recognizesPendingPacketWithoutChangingPayload();
    void rejectsCompletedPacketWithoutChanges();
    void rejectsFailedPacketWithoutChanges();
    void respectsLaterNormalizedTimestamp();
};

/**
 * @brief 验证 recognizesPendingPacketWithoutChangingPayload 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackRecognizer::recognizesPendingPacketWithoutChangingPayload()
{
    TrackPacket packet = populatedPacket();
    TrackPacket expected = packet;

    const TrackRecognizer recognizer;
    QVERIFY(recognizer.recognize(packet));

    expected.recognitionStatus = RecognitionStatus::Completed;
    expected.recognitionType = RecognitionType::Unknown;
    expected.recognizedSteadyNs = packet.recognizedSteadyNs;
    comparePacket(packet, expected);
    QVERIFY(packet.recognizedSteadyNs > 0);
    QVERIFY(packet.recognizedSteadyNs >= packet.normalizedSteadyNs);
}

/**
 * @brief 验证 rejectsCompletedPacketWithoutChanges 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackRecognizer::rejectsCompletedPacketWithoutChanges()
{
    TrackPacket packet = populatedPacket();
    packet.recognitionStatus = RecognitionStatus::Completed;
    packet.recognizedSteadyNs = 4000;
    const TrackPacket before = packet;

    const TrackRecognizer recognizer;
    QVERIFY(!recognizer.recognize(packet));
    comparePacket(packet, before);
}

/**
 * @brief 验证 rejectsFailedPacketWithoutChanges 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackRecognizer::rejectsFailedPacketWithoutChanges()
{
    TrackPacket packet = populatedPacket();
    packet.recognitionStatus = RecognitionStatus::Failed;
    packet.recognizedSteadyNs = 4000;
    const TrackPacket before = packet;

    const TrackRecognizer recognizer;
    QVERIFY(!recognizer.recognize(packet));
    comparePacket(packet, before);
}

/**
 * @brief 验证 respectsLaterNormalizedTimestamp 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackRecognizer::respectsLaterNormalizedTimestamp()
{
    TrackPacket packet;
    packet.normalizedSteadyNs = std::numeric_limits<qint64>::max();

    const TrackRecognizer recognizer;
    QVERIFY(recognizer.recognize(packet));
    QCOMPARE(packet.recognizedSteadyNs, packet.normalizedSteadyNs);
}

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TestTrackRecognizer)
#include "tst_TrackRecognizer.moc"
