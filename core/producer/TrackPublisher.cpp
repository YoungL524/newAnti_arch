#include "core/producer/TrackPublisher.h"

#include <cmath>

namespace core::producer {
constexpr double kPi = 3.14159265358979323846;
constexpr double kQuarterPi = kPi / 4.0;

/**
 * @brief 根据拒绝原因和 Topic sequence 的组合判断发布是否成功。
 * @details 同时检查两个成员，避免未来构造错误导致“无 sequence 的成功”或“带 sequence 的失败”被调用方接受。
 */
bool TrackPublishResult::accepted() const noexcept
{
    return rejection == TrackPublishRejection::None && sequence.has_value();
}

/**
 * @brief 绑定由 CoreDataDomain 拥有的唯一 TrackTopic。
 * @details 构造不注册回调、不创建队列或线程；引用绑定后不可更换，生命周期责任仍由应用组合根承担。
 */
TrackPublisher::TrackPublisher(core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic) noexcept
    : m_trackTopic(trackTopic)
{
}

/**
 * @brief 在移动 Sample 所有权之前完成门禁，拒绝时保持 Topic 完全不变。
 * @details 校验成功后才调用 HistoryTopic::publish；Topic 分配或 sequence 耗尽等基础设施异常继续向上传播，
 * 因其不是业务数据拒绝，故不计入 rejectedCount。成功计数只在 Topic 确认返回 sequence 后推进。
 */
TrackPublishResult TrackPublisher::publish(core::model::Sample<core::model::TrackPacket> sample)
{
    const TrackPublishRejection rejection = validate(sample);
    if (rejection != TrackPublishRejection::None) {
        m_rejectedCount.fetch_add(1, std::memory_order_relaxed);
        return {rejection, std::nullopt};
    }

    const core::domain::TopicSequence sequence = m_trackTopic.publish(std::move(sample));
    m_acceptedCount.fetch_add(1, std::memory_order_relaxed);
    return {TrackPublishRejection::None, sequence};
}

/**
 * @brief 读取成功发布累计值而不建立跨线程同步屏障。
 * @details 计数仅用于观测，不参与发布正确性或生命周期决策，因此 relaxed 顺序足够。
 */
quint64 TrackPublisher::acceptedCount() const noexcept
{
    return m_acceptedCount.load(std::memory_order_relaxed);
}


/**
 * @brief 读取业务门禁拒绝累计值而不影响 Producer 热路径。
 * @details 返回值适合指标快照；并发发布时读取到的是某一瞬间值，不承诺与 Topic stats 属于同一事务快照。
 */
quint64 TrackPublisher::rejectedCount() const noexcept
{
    return m_rejectedCount.load(std::memory_order_relaxed);
}

/**
 * @brief 以身份、时间、质量、运动学、辅助量、识别状态及类型的稳定顺序返回首个拒绝原因。
 * @details 顺序保证同一非法 Sample 的结果可重复，便于聚合指标。函数只做 Sample 内部合同检查；设备注册、
 * 当前 acquisition 和 DeviceId 安装配置真实性必须由调用前的控制上下文验证，不能由数据域反向依赖控制平面。
 */
TrackPublishRejection TrackPublisher::validate(
    const core::model::Sample<core::model::TrackPacket> &sample) noexcept
{
    const core::model::SampleMeta &meta = sample.meta;
    const core::model::TrackPacket &track = sample.value;

    constexpr core::model::SampleFieldFlags knownMetaFlags =
        core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence)
        | core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceTimeUtcMs);
    constexpr core::model::TrackFieldFlags knownTrackFlags =
        core::model::trackFieldBit(core::model::TrackFieldFlag::HasScanSequence);

    if (!meta.endpointId.isSet()) return TrackPublishRejection::UnboundEndpointId;
    if (!track.deviceId.isSet()) return TrackPublishRejection::UnboundDeviceId;
    if (!track.acquisitionId.isSet()) return TrackPublishRejection::UnboundAcquisitionId;
    if (meta.receiveTimeUtcMs <= 0) return TrackPublishRejection::InvalidReceiveUtcTime;
    if (meta.receiveTimeSteadyNs <= 0) return TrackPublishRejection::InvalidReceiveSteadyTime;
    if ((meta.validity & ~knownMetaFlags) != 0
        || (!core::model::hasSampleField(meta.validity, core::model::SampleFieldFlag::HasSourceSequence)
            && meta.sourceSequence != 0)
        || (!core::model::hasSampleField(meta.validity, core::model::SampleFieldFlag::HasSourceTimeUtcMs)
            && meta.sourceTimeUtcMs != 0)) {
        return TrackPublishRejection::InvalidMetaValidity;
    }
    if (core::model::hasSampleField(meta.validity, core::model::SampleFieldFlag::HasSourceTimeUtcMs)
        && meta.sourceTimeUtcMs <= 0) {
        return TrackPublishRejection::InvalidSourceTime;
    }
    if (meta.traceId == 0) return TrackPublishRejection::MissingTraceId;
    if (meta.quality != core::model::SampleQuality::Good
        && meta.quality != core::model::SampleQuality::Suspect) {
        return TrackPublishRejection::InvalidQuality;
    }
    if (!std::isfinite(track.rangeM) || track.rangeM < 0.0) return TrackPublishRejection::InvalidRange;
    if (!std::isfinite(track.azimuthRad) || track.azimuthRad < -kQuarterPi
        || track.azimuthRad > kQuarterPi) {
        return TrackPublishRejection::InvalidAzimuth;
    }
    if (!std::isfinite(track.elevationRad) || track.elevationRad < 0.0
        || track.elevationRad > kQuarterPi) {
        return TrackPublishRejection::InvalidElevation;
    }
    if (!std::isfinite(track.radialVelocityMps)) return TrackPublishRejection::InvalidRadialVelocity;
    if (!std::isfinite(track.amplitude)) return TrackPublishRejection::InvalidAmplitude;
    if ((track.validity & ~knownTrackFlags) != 0
        || (!core::model::hasTrackField(track.validity, core::model::TrackFieldFlag::HasScanSequence)
            && track.scanSequence != 0)) {
        return TrackPublishRejection::InvalidTrackValidity;
    }
    if (track.packFlag > 1U) return TrackPublishRejection::InvalidPackFlag;
    if (track.faceIndex > 3U) return TrackPublishRejection::InvalidFaceIndex;
    if (track.recognitionStatus != core::model::RecognitionStatus::Completed) {
        return TrackPublishRejection::RecognitionNotCompleted;
    }
    if (track.recognitionType != core::model::RecognitionType::Unknown) {
        return TrackPublishRejection::InvalidRecognitionType;
    }
    return TrackPublishRejection::None;
}

} // namespace core::producer
