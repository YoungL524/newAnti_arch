#include "core/producer/TrackIngressProducer.h"

#include <utility>

namespace core::producer {
namespace {
constexpr double degreesToRadians = 3.14159265358979323846 / 180.0;
}

/**
 * @brief 直接复用现有TrackPublisher，不注册Adapter回调或复制Topic所有权。
 */
TrackIngressProducer::TrackIngressProducer(
    core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic) noexcept
    : m_publisher(trackTopic)
{
}

/**
 * @brief 先清空旧上下文再验证新值，使失败配置不可能继续沿用上一采集会话。
 * @details 单面阵只设置首项；四面阵按任务 faceMask 设置任意非空子集。每个启用阵面使用独立会话句柄，
 * 这样调试部分阵面时不会把观测错误归入另一阵面的 collection_session。
 */
bool TrackIngressProducer::bind(
    core::model::EndpointId endpointId,
    core::model::DeviceId deviceId,
    core::model::TaskId taskId,
    const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds) noexcept
{
    clearBinding();
    if (!endpointId.isSet() || !deviceId.isSet() || !taskId.isSet()) return false;
    bool hasEnabledFace = false;
    for (std::size_t index = 0; index < faceAcquisitionIds.size(); ++index) {
        if (!faceAcquisitionIds[index].isSet()) continue;
        hasEnabledFace = true;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (faceAcquisitionIds[index] == faceAcquisitionIds[previous]) return false;
        }
    }
    if (!hasEnabledFace) return false;
    m_endpointId = endpointId;
    m_deviceId = deviceId;
    m_taskId = taskId;
    m_faceAcquisitionIds = faceAcquisitionIds;
    return true;
}

/**
 * @brief 同步清零全部身份，不清空TrackTopic中已经发布且由retention管理的历史。
 */
void TrackIngressProducer::clearBinding() noexcept
{
    m_endpointId = {};
    m_deviceId = {};
    m_taskId = {};
    m_faceAcquisitionIds = {};
}

/**
 * @brief 在协议回调栈内完成身份快照、度转弧度和定长Sample构造，再交给最终发布门禁。
 * @details sourceTime未具有可信UTC合同，故保持零且不置有效位；packFlag按0消亡、1存在原样窄化。
 */
TrackIngressProducer::Result TrackIngressProducer::accept(
    quint8 faceIndex, quint32 sourceSequence,
    const core::protocol::DpToDcmpTrackPayload::DpTrack &track,
    qint64 receiveUtcMs, qint64 receiveSteadyNs)
{
    if (!m_taskId.isSet()) return Result::UnboundContext;
    if (faceIndex >= m_faceAcquisitionIds.size()) return Result::InvalidFaceIndex;
    const core::model::AcquisitionId acquisitionId = m_faceAcquisitionIds[faceIndex];
    if (!m_deviceId.isSet() || !m_endpointId.isSet() || !acquisitionId.isSet()) {
        return Result::UnboundFace;
    }

    core::model::Sample<core::model::TrackPacket> sample;
    sample.meta.receiveTimeUtcMs = receiveUtcMs;
    sample.meta.receiveTimeSteadyNs = receiveSteadyNs;
    sample.meta.sourceSequence = sourceSequence;
    sample.meta.traceId = m_nextTraceId++;
    if (m_nextTraceId == 0) m_nextTraceId = 1;
    sample.meta.endpointId = m_endpointId;
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence);
    sample.value.acquisitionId = acquisitionId;
    sample.value.deviceId = m_deviceId;
    sample.value.taskId = m_taskId;
    sample.value.trackId = static_cast<quint32>(track.targetId);
    sample.value.rangeM = static_cast<double>(track.range);
    sample.value.azimuthRad = static_cast<double>(track.azimuth) * degreesToRadians;
    sample.value.elevationRad = static_cast<double>(track.elevation) * degreesToRadians;
    sample.value.radialVelocityMps = static_cast<double>(track.radialSpeed);
    sample.value.amplitude = static_cast<double>(track.amplitude);
    sample.value.packFlag = static_cast<quint8>(track.packFlag);
    sample.value.faceIndex = faceIndex;
    sample.value.recognitionStatus = core::model::RecognitionStatus::Completed;
    return m_publisher.publish(std::move(sample)).accepted()
        ? Result::Published : Result::PublisherRejected;
}

} // namespace core::producer
