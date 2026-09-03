/**
 * @file tst_TrackModelAndDomain.cpp
 * @brief 验证紧凑 SampleMeta、TrackPacket、TrackKey 和 CoreDataDomain 组合合同。
 * @details 测试防止可选字段重新引入 optional、FaceId 回流、TrackKey 混入任务，以及热结构超过冻结预算。
 */

#include "core/datadomain/CoreDataDomain.h"

#include <QHash>
#include <QtTest>

#include <array>
#include <cstddef>
#include <type_traits>

namespace {

/**
 * @brief 构造包含合法零号厂家序列与零号航迹的紧凑 Sample。
 * @return 可直接用于 Topic 值语义测试的 Completed 航迹。
 */
core::model::Sample<core::model::TrackPacket> makeTrackSample()
{
    core::model::Sample<core::model::TrackPacket> sample;
    sample.meta.endpointId = core::model::EndpointId::fromRaw(1);
    sample.meta.sourceSequence = 0;
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence);
    sample.meta.receiveTimeUtcMs = 1'777'000'000'000;
    sample.meta.receiveTimeSteadyNs = 1'000;
    sample.meta.traceId = 8;
    sample.value.acquisitionId = core::model::AcquisitionId::fromRaw(10);
    sample.value.deviceId = core::model::DeviceId::fromRaw(3);
    sample.value.trackId = 0;
    sample.value.rangeM = 1200.0;
    sample.value.amplitude = 6.5;
    sample.value.recognitionStatus = core::model::RecognitionStatus::Completed;
    return sample;
}

} // namespace

/** @brief 汇总紧凑航迹模型与数据域第一阶段合同测试。 */
class TrackModelAndDomainTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收 validity 区分缺失与合法零号源序列。 */
    void sampleMetaUsesValidityBits();

    /** @brief 验收 TrackKey 只由会话、设备和厂家航迹号组成。 */
    void trackKeyUsesThreeCompactFields();

    /** @brief 验收热结构布局不超过固定资源预算。 */
    void compactLayoutsStayWithinBudget();

    /** @brief 记录定长航迹 Sample 批量复制的基准耗时。 */
    void benchmarkTrackSampleCopy();

    /** @brief 验收数据域容量由组合根注入，并保存紧凑值。 */
    void domainOwnsConfiguredTrackTopic();
};

/**
 * @brief 区分厂家未提供序号和明确提供零号序列。
 * @details 数值字段始终存在，缺失语义只由 validity 表达，从而保持定长布局。
 */
void TrackModelAndDomainTest::sampleMetaUsesValidityBits()
{
    core::model::SampleMeta meta;
    QVERIFY(!core::model::hasSampleField(meta.validity, core::model::SampleFieldFlag::HasSourceSequence));
    QCOMPARE(meta.sourceSequence, quint64(0));

    meta = makeTrackSample().meta;
    QVERIFY(core::model::hasSampleField(meta.validity, core::model::SampleFieldFlag::HasSourceSequence));
    QCOMPARE(meta.sourceSequence, quint64(0));
}

/**
 * @brief 证明任务变化不改变航迹身份，而设备、会话和航迹号任一变化都会改变身份。
 * @details DeviceId 已代表雷达阵面，因此不再存在 FaceId 维度。
 */
void TrackModelAndDomainTest::trackKeyUsesThreeCompactFields()
{
    auto track = makeTrackSample().value;
    const auto original = core::model::makeTrackKey(track);
    QHash<core::model::TrackKey, int> values;
    values.insert(original, 7);

    track.taskId = core::model::TaskId::fromRaw(9);
    QCOMPARE(core::model::makeTrackKey(track), original);
    QCOMPARE(values.value(core::model::makeTrackKey(track)), 7);

    track.deviceId = core::model::DeviceId::fromRaw(4);
    QVERIFY(!(core::model::makeTrackKey(track) == original));
    track = makeTrackSample().value; track.acquisitionId = core::model::AcquisitionId::fromRaw(11);
    QVERIFY(!(core::model::makeTrackKey(track) == original));
    track = makeTrackSample().value; track.trackId = 1;
    QVERIFY(!(core::model::makeTrackKey(track) == original));
}

/**
 * @brief 固化编译器实测尺寸和可复制性质，为预分配 RingBuffer 计算上限。
 * @details 这里只约束进程内布局，不授权把对象内存直接作为序列化格式。
 */
void TrackModelAndDomainTest::compactLayoutsStayWithinBudget()
{
    static_assert(std::is_trivially_copyable<core::model::SampleMeta>::value, "SampleMeta must be trivially copyable");
    static_assert(std::is_trivially_copyable<core::model::TrackPacket>::value, "TrackPacket must be trivially copyable");
    static_assert(std::is_trivially_copyable<core::model::TrackKey>::value, "TrackKey must be trivially copyable");

    QCOMPARE(sizeof(core::model::SampleMeta), std::size_t(48));
    QCOMPARE(alignof(core::model::SampleMeta), std::size_t(8));
    QCOMPARE(offsetof(core::model::SampleMeta, sourceTimeUtcMs), std::size_t(0));
    QCOMPARE(offsetof(core::model::SampleMeta, receiveTimeUtcMs), std::size_t(8));
    QCOMPARE(offsetof(core::model::SampleMeta, receiveTimeSteadyNs), std::size_t(16));
    QCOMPARE(offsetof(core::model::SampleMeta, sourceSequence), std::size_t(24));
    QCOMPARE(offsetof(core::model::SampleMeta, traceId), std::size_t(32));
    QCOMPARE(offsetof(core::model::SampleMeta, endpointId), std::size_t(40));
    QCOMPARE(offsetof(core::model::SampleMeta, validity), std::size_t(44));
    QCOMPARE(offsetof(core::model::SampleMeta, quality), std::size_t(45));

    QCOMPARE(sizeof(core::model::TrackPacket), std::size_t(72));
    QCOMPARE(alignof(core::model::TrackPacket), std::size_t(8));
    QCOMPARE(offsetof(core::model::TrackPacket, acquisitionId), std::size_t(0));
    QCOMPARE(offsetof(core::model::TrackPacket, rangeM), std::size_t(8));
    QCOMPARE(offsetof(core::model::TrackPacket, azimuthRad), std::size_t(16));
    QCOMPARE(offsetof(core::model::TrackPacket, elevationRad), std::size_t(24));
    QCOMPARE(offsetof(core::model::TrackPacket, radialVelocityMps), std::size_t(32));
    QCOMPARE(offsetof(core::model::TrackPacket, amplitude), std::size_t(40));
    QCOMPARE(offsetof(core::model::TrackPacket, deviceId), std::size_t(48));
    QCOMPARE(offsetof(core::model::TrackPacket, taskId), std::size_t(52));
    QCOMPARE(offsetof(core::model::TrackPacket, trackId), std::size_t(56));
    QCOMPARE(offsetof(core::model::TrackPacket, scanSequence), std::size_t(60));
    QCOMPARE(offsetof(core::model::TrackPacket, validity), std::size_t(64));
    QCOMPARE(offsetof(core::model::TrackPacket, recognitionStatus), std::size_t(65));
    QCOMPARE(offsetof(core::model::TrackPacket, recognitionType), std::size_t(66));

    QCOMPARE(sizeof(core::model::TrackKey), std::size_t(16));
    QCOMPARE(alignof(core::model::TrackKey), std::size_t(8));
    QCOMPARE(offsetof(core::model::TrackKey, acquisitionId), std::size_t(0));
    QCOMPARE(offsetof(core::model::TrackKey, deviceId), std::size_t(8));
    QCOMPARE(offsetof(core::model::TrackKey, trackId), std::size_t(12));
}

/**
 * @brief 基准测量 256 条完整航迹 Sample 的定长值复制。
 * @details 本用例建立当前编译器与平台的可比较基线，不设置易受机器负载影响的硬耗时阈值；尺寸、平凡复制
 * 性和字段偏移仍由确定性断言负责阻止布局膨胀。
 */
void TrackModelAndDomainTest::benchmarkTrackSampleCopy()
{
    using TrackSample = core::model::Sample<core::model::TrackPacket>;
    std::array<TrackSample, 256> source {};
    std::array<TrackSample, 256> destination {};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = makeTrackSample();
        source[index].value.trackId = static_cast<quint32>(index);
    }

    QBENCHMARK {
        destination = source;
    }

    QCOMPARE(destination.back().value.trackId, quint32(255));
}

/**
 * @brief 确认 CoreDataDomain 仍只拥有一个有界 TrackTopic。
 * @details 发布零号 trackId 后 latest 必须原样返回，证明零值没有被误作缺失哨兵。
 */
void TrackModelAndDomainTest::domainOwnsConfiguredTrackTopic()
{
    core::domain::CoreDataDomain domain(2, 4);
    QCOMPARE(domain.trackTopic().stats().capacity, qsizetype(2));
    QCOMPARE(domain.eventTopic().stats().capacity, qsizetype(4));
    domain.trackTopic().publish(makeTrackSample());
    core::domain::TopicRecord<core::model::TrackPacket> latest;
    QVERIFY(domain.trackTopic().latest(latest));
    QCOMPARE(latest.sample.value.trackId, quint32(0));
}

/**
 * @brief 创建仅含 QCoreApplication 的 QtTest 入口。
 * @details 测试不依赖 GUI，按声明顺序执行 TrackModelAndDomainTest 私有槽。
 */
QTEST_APPLESS_MAIN(TrackModelAndDomainTest)

#include "tst_TrackModelAndDomain.moc"
