#include <QtTest>

#include <cstring>

#include "TrackPacketMapper.h"

class TestTrackPacketMapper : public QObject
{
    Q_OBJECT

private slots:
    void mapsProtocolFieldsWithoutUnitChanges();
    void carriesSourceContext();
    void doesNotInventRecognitionResult();
};

/**
 * @brief 验证 mapsProtocolFieldsWithoutUnitChanges 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackPacketMapper::mapsProtocolFieldsWithoutUnitChanges()
{
    GeneralPackage source{};
    source.h_c.TrackInfo.TargetID = 42;
    source.h_c.TrackInfo.Range = 123.5;
    source.h_c.TrackInfo.Azimuth = 1.25;
    source.h_c.TrackInfo.Elevation = -0.5;
    source.h_c.TrackInfo.RadialSpeed = 17.0;
    source.h_c.TrackInfo.RCS = 8.5;
    source.h_c.TrackInfo.Altitude = 90.0;
    source.h_c.TrackInfo.x = 1.0;
    source.h_c.TrackInfo.y = 2.0;
    source.h_c.TrackInfo.z = 3.0;
    source.h_c.TrackInfo.vx = 4.0;
    source.h_c.TrackInfo.vy = 5.0;
    source.h_c.TrackInfo.vz = 6.0;

    TrackPacketSourceContext context;
    context.sceneNumber = 7;
    const TrackPacket packet = mapGeneralPackageToTrackPacket(source, context);

    QCOMPARE(packet.trackId, uint32_t(42));
    QCOMPARE(packet.rangeM, 123.5);
    QCOMPARE(packet.azimuthRad, 1.25);
    QCOMPARE(packet.elevationRad, -0.5);
    QCOMPARE(packet.radialSpeedMps, 17.0);
    QCOMPARE(packet.rcs, 8.5);
    QCOMPARE(packet.altitudeM, 90.0);
    QCOMPARE(packet.xM, 1.0);
    QCOMPARE(packet.yM, 2.0);
    QCOMPARE(packet.zM, 3.0);
    QCOMPARE(packet.vxMps, 4.0);
    QCOMPARE(packet.vyMps, 5.0);
    QCOMPARE(packet.vzMps, 6.0);
    QCOMPARE(packet.sceneNumber, uint32_t(7));
}

/**
 * @brief 验证 carriesSourceContext 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackPacketMapper::carriesSourceContext()
{
    GeneralPackage source{};
    TrackPacketSourceContext context;
    context.traceId = 99;
    context.endpointId = QStringLiteral("radar_cpu");
    context.deviceId = QStringLiteral("radar_01");
    context.taskId = QStringLiteral("task_01");
    context.receivedAtMs = 123456;
    context.ingressSteadyNs = 1000;
    context.normalizedSteadyNs = 2000;

    const TrackPacket packet = mapGeneralPackageToTrackPacket(source, context);

    QCOMPARE(packet.traceId, uint64_t(99));
    QCOMPARE(packet.endpointId, QStringLiteral("radar_cpu"));
    QCOMPARE(packet.deviceId, QStringLiteral("radar_01"));
    QCOMPARE(packet.taskId, QStringLiteral("task_01"));
    QCOMPARE(packet.receivedAtMs, qint64(123456));
    QCOMPARE(packet.ingressSteadyNs, qint64(1000));
    QCOMPARE(packet.normalizedSteadyNs, qint64(2000));
}

/**
 * @brief 验证 doesNotInventRecognitionResult 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
void TestTrackPacketMapper::doesNotInventRecognitionResult()
{
    GeneralPackage source{};
    const TrackPacket packet = mapGeneralPackageToTrackPacket(
        source, TrackPacketSourceContext{});

    QCOMPARE(packet.recognitionStatus, RecognitionStatus::Pending);
    QCOMPARE(packet.recognitionType, RecognitionType::Unknown);
}

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(TestTrackPacketMapper)
#include "tst_TrackPacketMapper.moc"
