/**
 * @file TrackPublisher.h
 * @brief 定义标准航迹进入 CoreDataDomain::trackTopic 前的最后一道进程内发布门禁。
 * @details 本模块位于 ProtocolAdapter/同步识别之后、HistoryTopic<TrackPacket> 之前：上游必须先完成报文
 * 完整性校验、endpoint 到设备映射、活动 acquisition 绑定和阵面解析，本类再校验 Sample 自身可观察的
 * 元数据、运动学数值与识别状态，成功后写入共享 Topic。它不解析厂家字节、不查询设备注册表、不产生
 * SystemEvent，也不保存 Consumer 状态；新人应从 publish() 的前置条件和 TrackPublishRejection 阅读。
 */

#pragma once

#include "core/datadomain/HistoryTopic.h"
#include "core/model/TrackPacket.h"

#include <QtGlobal>

#include <atomic>
#include <optional>

namespace core::producer {

/** @brief 表示 TrackPublisher 能从 Sample 本身确定的拒绝原因。 */
enum class TrackPublishRejection : quint8
{
    None = 0,                 ///< Sample 已通过门禁并成功进入 trackTopic。
    UnboundEndpointId,        ///< 通信端点句柄为零，无法追溯原始报文来源。
    UnboundDeviceId,          ///< 雷达/阵面设备句柄为零，无法取得安装位置与绝对姿态。
    UnboundAcquisitionId,     ///< 采集会话句柄为零，无法隔离重连后复用的 trackId。
    InvalidReceiveUtcTime,    ///< receiveTimeUtcMs 未填写为正的 Unix epoch 毫秒。
    InvalidReceiveSteadyTime, ///< receiveTimeSteadyNs 未填写为正的进程内单调纳秒。
    InvalidMetaValidity,      ///< SampleMeta validity 含未知位，或未置位字段没有清零。
    InvalidSourceTime,        ///< 已声明有效的 sourceTimeUtcMs 不是正 Unix epoch 毫秒。
    MissingTraceId,           ///< traceId 为零，失败与时延无法关联到接收处理链。
    InvalidQuality,           ///< SampleQuality 不是合同冻结的 Good 或 Suspect。
    InvalidRange,             ///< rangeM 非有限数或小于零。
    InvalidAzimuth,           ///< azimuthRad 非有限数或不在 [-π/4, π/4] 范围。
    InvalidElevation,         ///< elevationRad 非有限数或不在 [0, π/4] 范围。
    InvalidRadialVelocity,    ///< radialVelocityMps 不是有限数。
    InvalidAmplitude,         ///< 厂家 double 幅值不是有限数。
    InvalidTrackValidity,     ///< TrackPacket validity 含未知位，或缺失扫描号未清零。
    RecognitionNotCompleted   ///< 同步识别仍为 Pending 或已经 Failed。
};

/** @brief 表示一次航迹发布尝试的确定结果。 */
struct TrackPublishResult
{
    TrackPublishRejection rejection = TrackPublishRejection::None; ///< 成功为 None，失败为首个确定拒绝原因。
    std::optional<core::domain::TopicSequence> sequence; ///< 成功发布后分配的 Topic sequence，拒绝时为空。

    /**
     * @brief 判断本次调用是否真正写入了 trackTopic。
     * @return rejection 为 None 且 sequence 有值时返回 true。
     */
    bool accepted() const noexcept;
};

/**
 * @brief 校验航迹 Sample 的进程内字段合同，并将合法事实写入指定 TrackTopic。
 * @details 类不拥有 Topic；组合根必须保证 Topic 的生命周期覆盖本对象及其所有并发 publish 调用。多个通信
 * 线程可以共享一个 TrackPublisher，门禁本身无可变业务状态，计数器使用原子操作，Topic 负责发布序列化。
 */
class TrackPublisher final
{
public:
    /**
     * @brief 绑定唯一的航迹共享历史。
     * @param trackTopic CoreDataDomain 拥有的 TrackTopic；调用方保留所有权。
     */
    explicit TrackPublisher(core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic) noexcept;

    /** @brief 禁止复制，避免误以为复制对象会复制或拥有 TrackTopic。 */
    TrackPublisher(const TrackPublisher&) = delete;

    /** @brief 禁止复制赋值，保持构造时绑定的 TrackTopic 不可替换。 */
    TrackPublisher& operator=(const TrackPublisher&) = delete;

    /** @brief 禁止移动，避免并发 Producer 持有的发布入口地址发生变化。 */
    TrackPublisher(TrackPublisher&&) = delete;

    /** @brief 禁止移动赋值，保证 Topic 绑定和统计生命周期稳定。 */
    TrackPublisher& operator=(TrackPublisher&&) = delete;

    /**
     * @brief 执行 Sample 自身可验证的最后门禁，并仅在全部通过后发布。
     * @param sample 已完成协议校验、设备/会话映射和同步识别的航迹事实；函数取得其所有权。
     * @return 成功时包含 Topic sequence；失败时包含首个拒绝原因且 sequence 为空。
     * @pre 上游已验证包头、命令字、声明长度、包尾和 CRC，deviceId/acquisitionId 均来自当前有效
     * 控制上下文。
     * @post 拒绝时 trackTopic 水位不变；成功时 Sample 作为不可变实例追加一次。
     */
    TrackPublishResult publish(core::model::Sample<core::model::TrackPacket> sample);

    /**
     * @brief 返回本发布入口累计成功写入 Topic 的次数。
     * @return 进程内单调累计值，不因 Topic 覆盖而减少。
     */
    quint64 acceptedCount() const noexcept;

    /**
     * @brief 返回本发布入口累计被门禁拒绝的次数。
     * @return 进程内单调累计值；后续可供 metrics 或 SystemEvent 聚合使用。
     */
    quint64 rejectedCount() const noexcept;

private:
    /**
     * @brief 按稳定顺序检查 Sample 中可独立判定的合同条件。
     * @param sample 不会被修改的候选航迹事实。
     * @return 全部通过返回 None，否则返回首个失败原因。
     */
    static TrackPublishRejection validate(const core::model::Sample<core::model::TrackPacket> &sample) noexcept;

    core::domain::HistoryTopic<core::model::TrackPacket> &m_trackTopic; ///< 借用的数据域航迹 Topic，不拥有其生命周期。
    std::atomic<quint64> m_acceptedCount {0}; ///< 成功发布累计值，允许多 Producer 线程并发更新。
    std::atomic<quint64> m_rejectedCount {0}; ///< 门禁拒绝累计值，不包含 HistoryTopic 抛出的异常。
};

} // namespace core::producer
