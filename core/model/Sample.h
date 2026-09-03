/**
 * @file Sample.h
 * @brief 定义紧凑业务事实与公共采集元数据的定长组合。
 * @details 本文件位于通信入口/Producer 与 CoreDataDomain 之间：上游用 EndpointId 和 validity flags 构造
 * 无动态成员的 SampleMeta，下游 Topic 将 Sample 内嵌到预分配槽位。它不分配 Topic sequence、不保存
 * publish 性能时间，也不解释厂家协议；新人应先阅读 SampleMeta 的零值规则，再阅读 HistoryTopic 读取合同。
 */

#pragma once

#include "core/model/EntityIds.h"

#include <QtGlobal>

namespace core::model {

/** @brief 表示已通过 Producer 有效性门禁的 Sample 质量等级。 */
enum class SampleQuality : quint8
{
    Good = 0,   ///< 所有已提供字段均通过协议校验和业务一致性校验。
    Suspect = 1 ///< 事实仍可消费，但来源声明降级或辅助测量存在已记录的不一致。
};

/**
 * @brief 标记 SampleMeta 中无法用数值零表达缺失语义的可选字段。
 * @details 标志与对应值必须保持一致：未置位字段必须清零，置位后零仍可作为合法 sourceSequence。
 */
enum class SampleFieldFlag : quint8
{
    None = 0,                 ///< 没有厂家源序号或可信源 UTC 时间。
    HasSourceSequence = 1U,   ///< sourceSequence 由厂家明确提供。
    HasSourceTimeUtcMs = 2U   ///< sourceTimeUtcMs 来自已同步且基准明确的设备时钟。
};

/** @brief 保存多个 SampleFieldFlag 的紧凑位集合。 */
using SampleFieldFlags = quint8;

/**
 * @brief 把单个 SampleFieldFlag 转为可存入 SampleMeta 的位集合。
 * @param flag 待转换标志。
 * @return 保持枚举位模式的 quint8。
 */
constexpr SampleFieldFlags sampleFieldBit(SampleFieldFlag flag) noexcept
{
    return static_cast<SampleFieldFlags>(flag);
}

/**
 * @brief 判断紧凑位集合是否包含指定 Sample 字段。
 * @param flags SampleMeta 保存的全部有效性位。
 * @param flag 待检查的单一标志。
 * @return 指定位非零时返回 true。
 */
constexpr bool hasSampleField(SampleFieldFlags flags, SampleFieldFlag flag) noexcept
{
    return (flags & sampleFieldBit(flag)) != 0;
}

/**
 * @brief 保存通信入口和 Producer 形成的定长公共采集元数据。
 * @details sourceSequence/sourceTimeUtcMs 通过 validity 区分缺失与合法零值；receiveTimeUtcMs 用于归档时间轴，
 * receiveTimeSteadyNs 用于进程内时延和超时。publish 延迟只进入聚合指标，不在每条 Sample 中长期驻留。
 */
struct SampleMeta
{
    qint64 sourceTimeUtcMs = 0; ///< 厂家已同步的 Unix epoch 毫秒；仅 HasSourceTimeUtcMs 置位时有效。
    qint64 receiveTimeUtcMs = 0; ///< socket 取出完整报文时采集的 Unix epoch 毫秒，发布时必须为正。
    qint64 receiveTimeSteadyNs = 0; ///< 同一接收点采集的进程内单调纳秒，发布时必须为正。
    quint64 sourceSequence = 0; ///< 厂家源侧序号；仅 HasSourceSequence 置位时有效，零可以是合法序号。
    quint64 traceId = 0; ///< 当前进程一次接收/处理链的非零诊断关联值，不作为业务主键。
    EndpointId endpointId; ///< 接收完整报文的端点注册表句柄；发布时必须非零且已由上游验证存在。
    SampleFieldFlags validity = 0; ///< 可选源序号和源时间的有效性位；未知位必须被发布门禁拒绝。
    SampleQuality quality = SampleQuality::Good; ///< 可消费质量；完全无效事实不得构造成 Sample。
};

static_assert(sizeof(SampleMeta) <= 64, "SampleMeta exceeds the frozen hot-path budget");

/**
 * @brief 将定长公共元数据与纯业务事实 T 组合成 Topic 的内嵌值。
 * @tparam T 不含动态所有权、Consumer 状态或路由信息的强类型业务模型。
 */
template<typename T>
struct Sample
{
    SampleMeta meta; ///< 来源、接收时间和质量元数据；不含 Topic sequence。
    T value;         ///< 业务事实；单位、坐标和字段有效性由对应合同定义。
};

} // namespace core::model
