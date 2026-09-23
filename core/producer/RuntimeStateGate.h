/**
 * @file RuntimeStateGate.h
 * @brief 定义三个完整运行状态进入 LatestState 前的统一门禁。
 * @details DeviceController、TaskService 和 SystemStateOwner 分别调用对应校验函数，成功后才允许 set；本模块
 * 不持有 LatestState、不分配 revision、不修改输入、不发布 Event，也不替代各 Owner 的业务状态机。统一拒绝
 * 枚举避免为三个低频 State 增加三套 Publisher/Result DTO；新人应从状态组合不变量开始阅读。
 */

#pragma once

#include "core/model/RuntimeState.h"
#include "core/model/Sample.h"

#include <QtGlobal>

namespace core::producer {

/** @brief 表示运行状态门禁拒绝完整快照的首个稳定原因。 */
enum class RuntimeStateRejection : quint8
{
    None = 0, ///< 完整 Sample 满足对应状态合同。
    InvalidMeta, ///< 聚合 State 的 SampleMeta 时间、来源或质量合同无效。
    InvalidCapacity, ///< maxRuntimeDevices 小于零或快照设备数量超过上限。
    UnboundDeviceId, ///< DeviceRuntimeState 没有非零设备句柄。
    UnboundEndpointId, ///< DeviceRuntimeState 没有非零主端点句柄。
    InvalidConfigurationRevision, ///< 生效配置 revision 为零。
    DuplicateOrUnsortedDeviceId, ///< DeviceId 重复或未严格升序。
    InvalidDeviceEnum, ///< 连接或工作状态枚举不在登记范围。
    InvalidDeviceFlags, ///< validity 或 currentFaultFlags 含未知位。
    InvalidDeviceOptionalField, ///< validity 与 acquisitionId 零值不一致。
    InvalidDeviceStateCombination, ///< 连接、工作、会话、请求或故障状态互相矛盾。
    InvalidTaskEnum, ///< TaskPhase 不在登记范围。
    InvalidTaskIdentity, ///< TaskId 与 None/活动阶段不一致。
    InvalidSystemEnum, ///< ApplicationPhase 或 SystemHealth 不在登记范围。
    InvalidSystemFlags, ///< readiness/degradation 含未知位。
    InvalidSystemCombination ///< 启动时间、生命周期、健康度与状态位不一致。
};

/**
 * @brief 提供无状态、无分配的三个完整 State 发布前校验入口。
 * @note 返回首个拒绝原因；调用者自行决定转换为 CommandResult 或受限频 SystemEvent。
 */
class RuntimeStateGate final
{
public:
    /**
     * @brief 校验多设备完整快照及其聚合 SampleMeta。
     * @param sample DeviceController 已形成但尚未 set 的完整快照。
     * @param maxRuntimeDevices 组合根配置的设备数量上限；零允许合法空快照。
     * @return None 表示允许写入 LatestState<DeviceState>，否则返回首个拒绝原因。
     */
    static RuntimeStateRejection validateDeviceState(
        const core::model::Sample<core::model::DeviceState> &sample,
        qsizetype maxRuntimeDevices) noexcept;

    /**
     * @brief 校验唯一活动任务阶段及其聚合 SampleMeta。
     * @param sample TaskService 已形成但尚未 set 的完整状态。
     * @return None 表示允许写入 LatestState<TaskState>，否则返回首个拒绝原因。
     */
    static RuntimeStateRejection validateTaskState(
        const core::model::Sample<core::model::TaskState> &sample) noexcept;

    /**
     * @brief 校验系统生命周期、健康与就绪/降级位的一致性。
     * @param sample SystemStateOwner 已形成但尚未 set 的完整状态。
     * @return None 表示允许写入 LatestState<SystemState>，否则返回首个拒绝原因。
     */
    static RuntimeStateRejection validateSystemState(
        const core::model::Sample<core::model::SystemState> &sample) noexcept;

private:
    RuntimeStateGate() = delete;
};

} // namespace core::producer
