/**
 * @file TrackPacket.h
 * @brief 定义单面阵雷达航迹链的紧凑权威事实及 16 字节运行时身份键。
 * @details 上游 ProtocolAdapter 将厂家字段映射为所属 DeviceId 的局部阵面观测，同步识别完成后由
 * TrackPublisher 发布；下游显示、归档、Web 和插件读取同一事实。当前一个 DeviceId 就代表一个雷达阵面，
 * 因而不再保存重复 FaceId；未来多面阵以多个 DeviceId 注册，跨阵面融合另行形成独立事实。
 */

#pragma once

#include "core/model/EntityIds.h"

#include <QHashFunctions>
#include <QtGlobal>

namespace core::model {

/** @brief 表示同步识别阶段对单条标准航迹的处理状态。 */
enum class RecognitionStatus : quint8
{
    Pending = 0, ///< 已完成协议标准化但尚未完成识别，禁止发布到 trackTopic。
    Completed,   ///< 识别步骤已经结束，满足其余门禁后允许发布。
    Failed       ///< 识别失败，禁止发布并应由后续 EventTopic 记录失败事实。
};

/** @brief 表示当前已冻结的目标识别类型，待真实算法合同形成后再扩展。 */
enum class RecognitionType : quint8
{
    Unknown = 0 ///< 已完成识别，但当前算法不能确定目标类别。
};

/** @brief 标记 TrackPacket 中厂家可能不提供的定长辅助字段。 */
enum class TrackFieldFlag : quint8
{
    None = 0,               ///< 当前没有可选扫描序号。
    HasScanSequence = 1U    ///< scanSequence 由厂家协议明确提供。
};

/** @brief 保存多个 TrackFieldFlag 的紧凑位集合。 */
using TrackFieldFlags = quint8;

/**
 * @brief 把单个 TrackFieldFlag 转为 TrackPacket 的位集合。
 * @param flag 待转换标志。
 * @return 保持枚举位模式的 quint8。
 */
constexpr TrackFieldFlags trackFieldBit(TrackFieldFlag flag) noexcept
{
    return static_cast<TrackFieldFlags>(flag);
}

/**
 * @brief 判断 TrackPacket 是否包含指定可选字段。
 * @param flags TrackPacket 保存的全部有效性位。
 * @param flag 待检查的单一标志。
 * @return 指定位非零时返回 true。
 */
constexpr bool hasTrackField(TrackFieldFlags flags, TrackFieldFlag flag) noexcept
{
    return (flags & trackFieldBit(flag)) != 0;
}

/**
 * @brief 表示某雷达阵面、某次采集会话在一个测量时刻产生的单条航迹观测。
 * @note 默认值只保证定长对象可安全覆盖，不代表满足发布门禁；trackId 和 amplitude 的零值均允许来自厂家。
 */
struct TrackPacket
{
    AcquisitionId acquisitionId; ///< 当前采集会话句柄；重新开始接收或重连必须分配新值。
    double rangeM = 0.0; ///< 雷达原点至目标斜距，单位 m；发布时有限且不小于零。
    double azimuthRad = 0.0; ///< 相对当前 DeviceId 阵面法线的局部方位角，范围 [-π/4, π/4]。
    double elevationRad = 0.0; ///< 从局部水平面向上为正的俯仰角，范围 [0, π/4]。
    double radialVelocityMps = 0.0; ///< 径向速度，单位 m/s；抵近雷达为正，远离雷达为负。
    double amplitude = 0.0; ///< 厂家 double 幅值原值；单位/标度不做 dBsm 换算，发布时仅要求有限。
    DeviceId deviceId; ///< 雷达设备即阵面的注册表句柄；用于取得安装经纬高和绝对姿态。
    TaskId taskId; ///< 接收瞬间活动任务句柄；零表示无任务，不参与 TrackKey。
    quint32 trackId = 0; ///< 厂家在采集会话内分配的目标航迹号；零是合法值。
    quint32 scanSequence = 0; ///< 厂家扫描序号；仅 HasScanSequence 置位时有效。
    TrackFieldFlags validity = 0; ///< 可选扫描序号有效性；未知位必须被发布门禁拒绝。
    RecognitionStatus recognitionStatus = RecognitionStatus::Pending; ///< 发布时必须为 Completed。
    RecognitionType recognitionType = RecognitionType::Unknown; ///< Completed 状态仍允许 Unknown。
};

static_assert(sizeof(TrackPacket) <= 96, "TrackPacket exceeds the frozen hot-path budget");

/**
 * @brief 唯一标识一个雷达采集会话内的目标航迹生命周期。
 * @details DeviceId 已等价表示所属阵面；TaskId、时间、traceId、扫描号和 Topic sequence 均不参与身份。
 */
struct TrackKey
{
    AcquisitionId acquisitionId; ///< 采集会话句柄，隔离停止、重连和厂家 trackId 复用。
    DeviceId deviceId; ///< 产生航迹的雷达/阵面句柄。
    quint32 trackId = 0; ///< 厂家航迹号，完整 uint32 范围均参与身份比较。

    /**
     * @brief 判断两个键是否指向同一航迹生命周期。
     * @param other 待比较身份键。
     * @return acquisitionId、deviceId 和 trackId 全部相等时返回 true。
     */
    bool operator==(const TrackKey &other) const noexcept
    {
        return acquisitionId == other.acquisitionId
            && deviceId == other.deviceId
            && trackId == other.trackId;
    }
};

static_assert(sizeof(TrackKey) <= 24, "TrackKey exceeds the frozen hot-path budget");

/**
 * @brief 从航迹事实提取无动态成员的稳定运行时身份键。
 * @param track 已完成身份字段映射的航迹事实。
 * @return 由 acquisitionId、deviceId 和 trackId 组成的 16 字节值对象。
 */
inline TrackKey makeTrackKey(const TrackPacket &track) noexcept
{
    return {track.acquisitionId, track.deviceId, track.trackId};
}

/**
 * @brief 为 TrackKey 提供 Qt 哈希容器所需的哈希值。
 * @param key 待计算身份键。
 * @param seed Qt 容器传入的随机种子。
 * @return 混合三个身份成员后的进程内哈希值。
 */
inline uint qHash(const TrackKey &key, uint seed = 0) noexcept
{
    seed = core::model::qHash(key.acquisitionId, seed);
    seed = core::model::qHash(key.deviceId, seed);
    return ::qHash(key.trackId, seed);
}

} // namespace core::model
