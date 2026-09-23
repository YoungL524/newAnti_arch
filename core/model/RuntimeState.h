/**
 * @file RuntimeState.h
 * @brief 定义设备、任务和后端进程三个低频完整运行状态。
 * @details 上游唯一 Owner 先在自身状态机内形成完整值并通过 RuntimeStateGate 校验，下游将在下一阶段由
 * CoreDataDomain 的三个 LatestState 共享给 WebReader；当前已接入心跳owner。本文件不保存历史、Command、设备配置或航迹，
 * DeviceRuntimeState 仅是多设备完整快照的有界元素，不单独建立 Topic；新人应先阅读三个 State 的不变量。
 */

#pragma once

#include "core/model/EntityIds.h"

#include <QVector>

#include <array>
#include <QtGlobal>

namespace core::model {

/** @brief 表示设备通信当前是否可用，与设备工作阶段正交。 */
enum class DeviceConnectionState : quint8
{
    Disabled = 0, ///< 用户禁用设备，不建立通信。
    Disconnected, ///< 已启用但当前没有可用通信连接。
    Connecting, ///< 正在建立通信，尚不能执行设备命令。
    Connected, ///< 通信正常，可继续解释工作状态。
    Degraded, ///< 通信仍可用，但至少一项端点能力已降级。
    Faulted ///< 通信处于持续故障，需恢复动作。
};

/** @brief 表示本地采集编排所处的设备工作阶段，不推断外部设备是否执行命令。 */
enum class DeviceOperatingState : quint8
{
    Idle = 0, ///< 当前没有本地采集会话。
    Collecting, ///< 本地采集会话已建立且 PPI 正在扫描。
    Reconfiguring, ///< Owner 正在提交一组本地配置变化。
    Faulted ///< 工作状态无法继续，需恢复动作。
};

/** @brief 标记 DeviceRuntimeState 中可选的采集会话字段。 */
enum class DeviceStateFieldFlag : quint16
{
    None = 0, ///< 当前没有采集会话。
    AcquisitionValid = 1U << 0 ///< acquisitionId 表示当前本地采集会话。
};

/** @brief 保存多个 DeviceStateFieldFlag 的紧凑位集合。 */
using DeviceStateFieldFlags = quint16;

/** @brief 把单个设备状态字段标志转换为位集合。 */
constexpr DeviceStateFieldFlags deviceStateFieldBit(DeviceStateFieldFlag flag) noexcept
{
    return static_cast<DeviceStateFieldFlags>(flag);
}

/** @brief 判断设备状态位集合是否包含指定可选字段。 */
constexpr bool hasDeviceStateField(DeviceStateFieldFlags flags, DeviceStateFieldFlag flag) noexcept
{
    return (flags & deviceStateFieldBit(flag)) != 0;
}

/** @brief 表示设备当前仍成立、解除后应清除的故障类别。 */
enum class DeviceFaultFlag : quint32
{
    None = 0, ///< 当前没有持续故障。
    CommunicationUnavailable = 1U << 0, ///< 设备通信不可用。
    DeviceReportedFault = 1U << 1, ///< 设备主动上报持续故障。
    ControlRecoveryRequired = 1U << 2, ///< 控制状态机需要恢复或人工干预。
    ConfigurationRejected = 1U << 3 ///< 设备拒绝当前期望配置。
};

/** @brief 保存多个 DeviceFaultFlag 的紧凑位集合。 */
using DeviceFaultFlags = quint32;

/**
 * @brief 保存一台已激活设备在完整 DeviceState 中的当前运行值。
 * @note 本结构不保存设备名称、安装姿态、端点 JSON、命令内容或历史错误；这些分别属于配置、控制与事件边界。
 */
struct DeviceRuntimeState
{
    std::array<AcquisitionId, 4> faceAcquisitionIds {}; ///< AcquisitionValid置位时，启用阵面保存各自独立会话；未启用阵面为零。
    DeviceId deviceId; ///< 已由 DeviceConfigService 激活的非零设备句柄。
    EndpointId primaryEndpointId; ///< 与当前配置 revision 一致的非零主设备端点。
    quint32 configurationRevision = 0; ///< 当前实际生效的 SQLite 配置 revision，必须大于零。
    DeviceFaultFlags currentFaultFlags = 0; ///< 当前持续故障位；未知位不得进入 State。
    DeviceStateFieldFlags validity = 0; ///< acquisitionId 的有效性位。
    DeviceConnectionState connectionState = DeviceConnectionState::Disabled; ///< 当前通信状态。
    DeviceOperatingState operatingState = DeviceOperatingState::Idle; ///< 当前设备工作阶段。
};

/**
 * @brief 保存当前进程全部已激活设备的完整运行时快照。
 * @details devices 必须按 DeviceId 升序且不重复；容量由组合根的 maxRuntimeDevices 限制。低频完整替换允许
 * 使用 QVector，但禁止把它当作设备事件队列或持续追加历史。
 */
struct DeviceState
{
    quint8 dpHeartbeat = 0; ///< DP心跳状态：0未知/超时灰，1在线绿，2离线/迟到黄。
    quint8 spHeartbeat = 0; ///< SP心跳状态，与DP独立计时；不表示采集工作状态。
    quint16 fpOnlineMask = 0; ///< DP报告的四阵面在线位；灰色时清零。
    quint16 fpDataMask = 0; ///< SP报告的四阵面数据位；灰色时清零。
    QVector<DeviceRuntimeState> devices; ///< 当前全部已激活设备；空数组表示配置域尚无激活设备。
};

/** @brief 表示系统中唯一活动任务的当前运行阶段。 */
enum class TaskPhase : quint8
{
    None = 0, ///< 当前没有活动任务，taskId 必须为零。
    Ready, ///< 已选择任务，但尚未启动设备采集。
    Running, ///< 至少一个本地采集会话正在运行。
    Completed, ///< 本次任务运行已正常结束，等待清除当前任务。
    Faulted ///< 任务无法继续，等待处置或清除。
};

/**
 * @brief 保存唯一活动任务的完整当前状态。
 * @details 不重复保存设备或采集会话数组；Web 需要关联设备时分别读取 DeviceState。任务台账字段仍由
 * TaskService/SQLite 持有。
 */
struct TaskState
{
    TaskId taskId; ///< None 时必须为零，其他阶段必须为 TaskService 可解析的非零句柄。
    TaskPhase phase = TaskPhase::None; ///< 当前唯一活动任务阶段。
};

/** @brief 表示后端进程从启动到停止的生命周期阶段。 */
enum class ApplicationPhase : quint8
{
    Starting = 0, ///< 组合根正在建立依赖，尚未对外宣告就绪。
    Ready, ///< 所有强制服务已经就绪，可接受业务请求。
    Stopping, ///< 正在拒绝新工作并按依赖逆序关闭。
    Stopped, ///< 运行资源已经关闭，仅供关停尾部观察。
    Faulted ///< 关键启动或运行不变量失败，无法正常提供服务。
};

/** @brief 表示后端进程当前总体健康程度，与生命周期阶段分开表达。 */
enum class SystemHealth : quint8
{
    Healthy = 0, ///< 没有已知降级或持续故障。
    Degraded, ///< 核心仍可服务，但至少一个降级位有效。
    Faulted ///< 关键能力不可用或系统进入不可恢复状态。
};

/** @brief 标记组合根已经确认可用的关键服务。 */
enum class SystemReadinessFlag : quint32
{
    None = 0, ///< 当前没有已确认就绪服务。
    DatabaseReady = 1U << 0, ///< SQLite 初始化和运行实例登记完成。
    ConfigurationLoaded = 1U << 1, ///< 设备配置真源已经装载并验证。
    DataDomainReady = 1U << 2, ///< CoreDataDomain 已按容量策略构造。
    ArchiveReady = 1U << 3, ///< 归档 Repository 与 Reader 已可工作。
    WebReady = 1U << 4, ///< Web 静态/API/WebSocket 边界已监听。
    ControlPlaneReady = 1U << 5 ///< ApplicationController 与设备控制通道已就绪。
};

/** @brief 保存多个 SystemReadinessFlag 的紧凑位集合。 */
using SystemReadinessFlags = quint32;

/** @brief 标记当前仍成立且能由 SystemStateOwner 解除的系统降级原因。 */
enum class SystemDegradationFlag : quint32
{
    None = 0, ///< 当前没有系统降级。
    ArchiveUnavailable = 1U << 0, ///< 归档暂时不可写。
    ArchiveGapDetected = 1U << 1, ///< 归档 Reader 已检测到航迹历史缺口。
    EventGapDetected = 1U << 2, ///< 事件归档或监控 Reader 已检测到缺口。
    StorageSpaceLow = 1U << 3, ///< 可用存储空间低于运行策略阈值。
    WebUnavailable = 1U << 4, ///< Web 服务当前不可用。
    ResourcePressure = 1U << 5 ///< 内存、句柄或执行资源达到降级阈值。
};

/** @brief 保存多个 SystemDegradationFlag 的紧凑位集合。 */
using SystemDegradationFlags = quint32;

/**
 * @brief 保存后端进程生命周期、总体健康和关键服务就绪情况的完整快照。
 * @note 不保存设备/任务状态、历史事件、版本字符串、数据库路径或累计指标。
 */
struct SystemState
{
    qint64 processStartedUtcMs = 0; ///< 当前 application run 的 Unix epoch 毫秒起点，所有 revision 保持不变。
    SystemReadinessFlags readinessFlags = 0; ///< 已确认就绪服务位；未知位不得进入 State。
    SystemDegradationFlags degradationFlags = 0; ///< 当前降级原因位；Healthy 时必须为零。
    ApplicationPhase phase = ApplicationPhase::Starting; ///< 当前进程生命周期阶段。
    SystemHealth health = SystemHealth::Healthy; ///< 当前总体健康状态。
};

static_assert(sizeof(DeviceRuntimeState) <= 64, "DeviceRuntimeState exceeds the low-frequency state budget");
static_assert(sizeof(TaskState) <= 8, "TaskState exceeds the compact state budget");
static_assert(sizeof(SystemState) <= 24, "SystemState exceeds the compact state budget");

} // namespace core::model
