/**
 * @file tst_TrackPublisher.cpp
 * @brief 验证紧凑航迹进入 TrackTopic 前的身份、有效位、运动学和识别门禁。
 * @details 测试覆盖单面阵角度边界、抵近为正的速度样例、厂家 double 幅值及拒绝不推进 Topic 水位；注册表
 * 存在性、协议包头和 CRC 由上游测试负责，不在本夹具伪造。
 */

#include "core/producer/TrackPublisher.h"

#include <QtTest>

#include <cmath>
#include <limits>

namespace {

/**
 * @brief 构造满足紧凑 TrackPacket 发布合同的确定性 Sample。
 * @return 含非零强类型身份、可信接收时间、有限运动学和 Completed 状态的事实。
 */
core::model::Sample<core::model::TrackPacket> validTrackSample()
{
    core::model::Sample<core::model::TrackPacket> sample;
    sample.meta.endpointId = core::model::EndpointId::fromRaw(3);
    sample.meta.sourceSequence = 0;
    sample.meta.sourceTimeUtcMs = 1'777'500'000'000;
    sample.meta.receiveTimeUtcMs = 1'777'500'000'010;
    sample.meta.receiveTimeSteadyNs = 500;
    sample.meta.traceId = 77;
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence)
        | core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceTimeUtcMs);
    sample.value.acquisitionId = core::model::AcquisitionId::fromRaw(9);
    sample.value.deviceId = core::model::DeviceId::fromRaw(5);
    sample.value.trackId = 12;
    sample.value.rangeM = 900.0;
    sample.value.azimuthRad = 0.25;
    sample.value.elevationRad = 0.2;
    sample.value.radialVelocityMps = 12.0;
    sample.value.amplitude = 41.5;
    sample.value.packFlag = 1;
    sample.value.recognitionStatus = core::model::RecognitionStatus::Completed;
    return sample;
}

} // namespace

/** @brief 汇总 TrackPublisher 紧凑模型发布门禁及统计验收。 */
class TrackPublisherTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收合法 Sample 进入 Topic 并返回单调 sequence。 */
    void acceptsValidTrack();

    /** @brief 验收未绑定 ID、非法元数据有效位和时间被拒绝。 */
    void rejectsInvalidIdentityAndMeta();

    /** @brief 验收单面阵角度、有限速度和厂家幅值边界。 */
    void rejectsInvalidMeasurements();

    /** @brief 验收 Pending/Failed 航迹不会成为权威事实。 */
    void requiresCompletedRecognition();
};

/**
 * @brief 验证成功路径只追加一次，并保存抵近为正和原始幅值语义。
 * @details Topic sequence 从一开始；Publisher 计数与 Topic 水位同步推进。
 */
void TrackPublisherTest::acceptsValidTrack()
{
    core::domain::HistoryTopic<core::model::TrackPacket> topic(4);
    core::producer::TrackPublisher publisher(topic);

    const auto result = publisher.publish(validTrackSample());

    QVERIFY(result.accepted());
    QCOMPARE(result.sequence.value(), core::domain::TopicSequence(1));
    QCOMPARE(publisher.acceptedCount(), quint64(1));
    QCOMPARE(publisher.rejectedCount(), quint64(0));
    core::domain::TopicRecord<core::model::TrackPacket> latest;
    QVERIFY(topic.latest(latest));
    QCOMPARE(latest.sample.value.radialVelocityMps, 12.0);
    QCOMPARE(latest.sample.value.amplitude, 41.5);
}

/**
 * @brief 逐项破坏强类型身份、接收时间和 validity 一致性。
 * @details 每个拒绝都使用新 Topic，确保失败不会改变共享历史水位。
 */
void TrackPublisherTest::rejectsInvalidIdentityAndMeta()
{
    using Rejection = core::producer::TrackPublishRejection;
    const auto expectRejected = [](core::model::Sample<core::model::TrackPacket> sample, Rejection expected) {
        core::domain::HistoryTopic<core::model::TrackPacket> topic(2);
        core::producer::TrackPublisher publisher(topic);
        const auto result = publisher.publish(std::move(sample));
        QCOMPARE(result.rejection, expected);
        QVERIFY(!result.sequence.has_value());
        QVERIFY(topic.range().empty);
    };

    auto sample = validTrackSample(); sample.meta.endpointId = {}; expectRejected(sample, Rejection::UnboundEndpointId);
    sample = validTrackSample(); sample.value.deviceId = {}; expectRejected(sample, Rejection::UnboundDeviceId);
    sample = validTrackSample(); sample.value.acquisitionId = {}; expectRejected(sample, Rejection::UnboundAcquisitionId);
    sample = validTrackSample(); sample.meta.receiveTimeUtcMs = 0; expectRejected(sample, Rejection::InvalidReceiveUtcTime);
    sample = validTrackSample(); sample.meta.receiveTimeSteadyNs = 0; expectRejected(sample, Rejection::InvalidReceiveSteadyTime);
    sample = validTrackSample(); sample.meta.validity = 0x80; expectRejected(sample, Rejection::InvalidMetaValidity);
    sample = validTrackSample(); sample.meta.validity = 0; expectRejected(sample, Rejection::InvalidMetaValidity);
    sample = validTrackSample(); sample.meta.sourceTimeUtcMs = 0; expectRejected(sample, Rejection::InvalidSourceTime);
}

/**
 * @brief 验证方位与俯仰闭区间以及所有 double 必须有限。
 * @details `[-π/4,π/4]` 和 `[0,π/4]` 的端点合法；速度只约束有限性，正号已经冻结为抵近。
 */
void TrackPublisherTest::rejectsInvalidMeasurements()
{
    using Rejection = core::producer::TrackPublishRejection;
    constexpr double quarterPi = 3.14159265358979323846 / 4.0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto expectRejected = [](core::model::Sample<core::model::TrackPacket> sample, Rejection expected) {
        core::domain::HistoryTopic<core::model::TrackPacket> topic(2);
        core::producer::TrackPublisher publisher(topic);
        QCOMPARE(publisher.publish(std::move(sample)).rejection, expected);
    };

    auto sample = validTrackSample(); sample.value.rangeM = -0.1; expectRejected(sample, Rejection::InvalidRange);
    sample = validTrackSample(); sample.value.azimuthRad = -quarterPi - 0.001; expectRejected(sample, Rejection::InvalidAzimuth);
    sample = validTrackSample(); sample.value.azimuthRad = quarterPi + 0.001; expectRejected(sample, Rejection::InvalidAzimuth);
    sample = validTrackSample(); sample.value.elevationRad = -0.001; expectRejected(sample, Rejection::InvalidElevation);
    sample = validTrackSample(); sample.value.elevationRad = quarterPi + 0.001; expectRejected(sample, Rejection::InvalidElevation);
    sample = validTrackSample(); sample.value.radialVelocityMps = nan; expectRejected(sample, Rejection::InvalidRadialVelocity);
    sample = validTrackSample(); sample.value.amplitude = nan; expectRejected(sample, Rejection::InvalidAmplitude);
    sample = validTrackSample(); sample.value.scanSequence = 1; expectRejected(sample, Rejection::InvalidTrackValidity);
    sample = validTrackSample(); sample.value.packFlag = 2; expectRejected(sample, Rejection::InvalidPackFlag);
    sample = validTrackSample(); sample.value.faceIndex = 4; expectRejected(sample, Rejection::InvalidFaceIndex);

    core::domain::HistoryTopic<core::model::TrackPacket> topic(2);
    core::producer::TrackPublisher publisher(topic);
    sample = validTrackSample(); sample.value.azimuthRad = -quarterPi; sample.value.elevationRad = 0.0;
    QVERIFY(publisher.publish(sample).accepted());
    sample = validTrackSample(); sample.value.azimuthRad = quarterPi; sample.value.elevationRad = quarterPi;
    QVERIFY(publisher.publish(sample).accepted());
}

/**
 * @brief 确认同步识别状态仍是发布权威事实的最后业务门禁。
 * @details Pending 与 Failed 均拒绝；两次失败累计但不占用 Topic sequence。
 */
void TrackPublisherTest::requiresCompletedRecognition()
{
    core::domain::HistoryTopic<core::model::TrackPacket> topic(2);
    core::producer::TrackPublisher publisher(topic);

    auto sample = validTrackSample();
    sample.value.recognitionStatus = core::model::RecognitionStatus::Pending;
    QCOMPARE(publisher.publish(sample).rejection, core::producer::TrackPublishRejection::RecognitionNotCompleted);
    sample.value.recognitionStatus = core::model::RecognitionStatus::Failed;
    QCOMPARE(publisher.publish(sample).rejection, core::producer::TrackPublishRejection::RecognitionNotCompleted);
    QCOMPARE(publisher.rejectedCount(), quint64(2));
    QVERIFY(topic.range().empty);
}

/**
 * @brief 创建仅含 QCoreApplication 的 QtTest 入口。
 * @details 测试不依赖 GUI，按声明顺序执行 TrackPublisherTest 私有槽。
 */
QTEST_APPLESS_MAIN(TrackPublisherTest)

#include "tst_TrackPublisher.moc"
