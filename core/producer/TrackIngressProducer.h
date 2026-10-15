/**
 * @file TrackIngressProducer.h
 * @brief 定义0x0100协议航迹到权威TrackPacket的唯一映射与发布入口。
 * @details 上游DcmpProtocolAdapter同步交付已校验的阵面、源序号和DpTrack，下游仅调用TrackPublisher写入
 * CoreDataDomain::trackTopic，并将最终门禁失败限频写入统一 EventTopic。本类保存一台整机及四阵面采集会话的轻量身份绑定，不保存报文历史、不发送Web消息、
 * 不拥有Socket或Command状态；新人应从bind()和accept()阅读运行时上下文及单位转换规则。
 */
#pragma once

#include "core/model/EntityIds.h"
#include "core/producer/TrackPublisher.h"
#include "core/protocol/DcmpPayloads.h"

#include <array>

namespace core::producer {
class SystemEventPublisher;

/**
 * @brief 将一个内部UDP入口的四阵面协议观测映射到共享航迹数据域。
 * @warning bind()、clearBinding()和accept()必须在同一通信线程串行调用；控制线程须通过Qt queued调用切换绑定。
 */
class TrackIngressProducer final
{
public:
    /** @brief 表示一次协议航迹映射和发布的确定结果。 */
    enum class Result : quint8
    {
        Published = 0, ///< 已形成TrackPacket并成功写入TrackTopic。
        UnboundContext, ///< 当前尚未建立活动采集身份，报文未进入数据域。
        InvalidFaceIndex, ///< Adapter交付了0～3之外的阵面索引。
        UnboundFace, ///< 当前阵面尚未绑定非零DeviceId。
        PublisherRejected ///< 映射结果被TrackPublisher最终门禁拒绝。
    };

    /**
     * @brief 绑定航迹门禁与可空事件门禁；后者只报告最终业务校验失败，不报告正常未采集过滤。
     * @param trackTopic 唯一航迹历史，生命周期覆盖本对象。
     * @param eventPublisher 可空借用的共享事件门禁；非空时生命周期覆盖本对象。
     */
    explicit TrackIngressProducer(core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
                                  SystemEventPublisher *eventPublisher = nullptr) noexcept;

    /**
     * @brief 原子地替换下一条航迹使用的运行时身份上下文。
     * @param endpointId 完整雷达共用的逻辑接收端点。
     * @param deviceId 设备台账中完整雷达的非零运行时句柄。
     * @param taskId 四个阵面共享的非零活动任务句柄。
     * @param faceAcquisitionIds TaskService按本次任务启用阵面分别分配的非零且不可复用采集会话句柄；
     * 未启用阵面保持零，允许四面阵调试任意非空阵面子集。
     * @return 公共身份有效、至少启用一个阵面且所有非零采集句柄互不重复时返回true。
     */
    bool bind(core::model::EndpointId endpointId,
              core::model::DeviceId deviceId,
              core::model::TaskId taskId,
              const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds) noexcept;

    /** @brief 清除绑定及未报告拒绝计数；保留限频时刻，切换会话不能绕过五秒间隔。 */
    void clearBinding() noexcept;

    /**
     * @brief 将一条已校验0x0100协议航迹同步转换并发布。
     * @param faceIndex 0～3阵面索引。
     * @param sourceSequence FPGA包计数，作为有明确有效位的源序号。
     * @param track 仅在调用期借用的协议航迹，角度单位为度。
     * @param receiveUtcMs socket取出完整数据报时的Unix epoch UTC毫秒。
     * @param receiveSteadyNs 同一接收点采集的进程内单调纳秒。
     * @return Published表示唯一TrackTopic已经追加事实；其余结果均不改变Topic。
     */
    Result accept(quint8 faceIndex, quint32 sourceSequence,
                  const core::protocol::DpToDcmpTrackPayload::DpTrack &track,
                  qint64 receiveUtcMs, qint64 receiveSteadyNs);

private:
    TrackPublisher m_publisher; ///< TrackPacket发布前的最终字段门禁。
    SystemEventPublisher *m_eventPublisher = nullptr; ///< 同线程可空借用，只写入已有统一 EventTopic。
    qint64 m_lastRejectionReportSteadyNs = 0; ///< 最近拒绝事件尝试发布时间，单调纳秒，五秒间隔，无计时器。
    qint64 m_pendingRejectionCount = 0; ///< 自上次报告以来的所有最终门禁拒绝数，饱和于 int64 上限，不保存历史报文。
    core::model::EndpointId m_endpointId; ///< 一台完整雷达共用的逻辑接收端点句柄。
    core::model::DeviceId m_deviceId; ///< 一台完整雷达在设备注册表中的运行时句柄。
    core::model::TaskId m_taskId; ///< 四阵面共享的当前非零任务句柄。
    std::array<core::model::AcquisitionId, 4> m_faceAcquisitionIds {}; ///< 四阵面各自独立的活动采集会话。
    quint64 m_nextTraceId = 1; ///< 单通信线程内分配的非零诊断关联号。
};

} // namespace core::producer
