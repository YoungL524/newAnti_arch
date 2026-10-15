/**
 * @file SystemEvent.h
 * @brief 定义 Device、Track、System 与 Control 四个错误域共用的紧凑事件事实。
 * @details 上游设备接入、航迹门禁、数据域 Reader 和控制 owner 构造本类型，下游监控、Web 与归档从
 * EventTopic<SystemEvent> 顺序读取。时间、端点和 trace 统一放在 SampleMeta；本文件不保存动态文本、
 * 当前故障状态、CommandResult 或消费者路由。新人应先阅读 eventCode 与 flags 的一致性约束。
 */

#pragma once

#include "core/model/EntityIds.h"

#include <QtGlobal>

namespace core::model {

/** @brief 标识已经发生的事件由哪类业务职责拥有。 */
enum class EventDomain : quint8
{
    Device = 1, ///< 设备通信、协议输入或设备主动上报的事实。
    Track = 2, ///< 航迹映射、门禁、识别或连续性相关事实。
    System = 3, ///< 数据域、调度、存储和运行时基础设施事实。
    Control = 4 ///< 本地命令受理、状态转换和设备数据报发送相关事实。
};

/** @brief 表示事件对当前运行和操作员处置的影响程度。 */
enum class EventSeverity : quint8
{
    Info = 0, ///< 正常但值得追溯的操作或状态变化。
    Warning = 1, ///< 已出现异常征兆或降级，但核心服务仍可继续。
    Error = 2, ///< 当前操作或数据失败，系统仍能提供部分服务。
    Fault = 3, ///< 设备或核心模块进入需要恢复动作的持续故障。
    Alarm = 4 ///< 需要操作员及时确认或处置的高优先级事实。
};

/** @brief 标记 SystemEvent 中哪些定长关联字段和诊断参数有效。 */
enum class EventSubjectFlag : quint16
{
    None = 0, ///< 当前事件不关联可选业务主体或诊断参数。
    DeviceIdValid = 1U << 0, ///< deviceId 已绑定设备注册表句柄。
    TaskIdValid = 1U << 1, ///< taskId 已绑定任务注册表句柄。
    AcquisitionIdValid = 1U << 2, ///< acquisitionId 已绑定采集会话句柄。
    TrackIdValid = 1U << 3, ///< trackId 是有效厂家航迹号；零也可有效。
    RequestIdValid = 1U << 4, ///< requestId 关联一次控制请求。
    Argument0Valid = 1U << 5, ///< argument0 按事件码登记语义有效。
    Argument1Valid = 1U << 6 ///< argument1 按事件码登记语义有效。
};

/** @brief 保存多个 EventSubjectFlag 的紧凑位集合。 */
using EventSubjectFlags = quint16;

/**
 * @brief 把单个事件关联字段标志转换为位集合。
 * @param flag 待转换的单一标志。
 * @return 保持枚举位模式的 quint16。
 */
constexpr EventSubjectFlags eventSubjectBit(EventSubjectFlag flag) noexcept
{
    return static_cast<EventSubjectFlags>(flag);
}

/**
 * @brief 判断事件是否声明指定关联字段有效。
 * @param flags 事件保存的全部关联字段有效性位。
 * @param flag 待检查的单一标志。
 * @return 指定位存在时返回 true。
 */
constexpr bool hasEventSubject(EventSubjectFlags flags, EventSubjectFlag flag) noexcept
{
    return (flags & eventSubjectBit(flag)) != 0;
}

/**
 * @brief 首批已登记、可持久化的稳定事件码。
 * @details 高 8 位与 EventDomain 数值一致，中 8 位是稳定组件登记值，低 16 位是原因；已发布数值不得复用。
 */
enum class EventCode : quint32
{
    DeviceMalformedFrame = 0x01010001U, ///< 封装或协议字段无效；argument0 为 Adapter 拒绝编号，argument1 可含窗口坏帧总数。
    TrackPublishRejected = 0x02010001U, ///< 航迹最终门禁拒绝；argument0 为拒绝编号，argument1 可含窗口最终门禁拒绝总数。
    SystemTopicSequenceGap = 0x03010001U, ///< Reader cursor 落后于 Topic 历史窗口；两个参数保存丢失闭区间。
    SystemArchiveHealthChanged = 0x03020001U, ///< 归档状态变化；两个参数为前后三位降级位图，当前状态以 SystemState 为准。
    SystemArchiveTrackGap = 0x03020002U, ///< 归档首次观察航迹缺口；两个参数为丢失 sequence 闭区间。
    SystemArchiveEventGap = 0x03020003U, ///< 归档首次观察事件缺口；两个参数为丢失 sequence 闭区间。
    ControlDatagramSendFailed = 0x04010002U, ///< 控制数据报未能交给本地 UDP Socket；requestId 有效。
    ControlTaskLifecycleChanged = 0x04010003U, ///< 任务本地持久化生命周期变化；argument0 为1创建、2激活、3完成、4归档。
    ControlTaskControlSettingsPersistFailed = 0x04010004U, ///< 任务控制参数快照未能提交 SQLite；taskId 有效，severity=Error。
    ControlTaskControlSettingsInvariantFailed = 0x04010005U, ///< 任务快照已更新但设备运行副本替换失败；taskId 有效，severity=Fault。
    DeviceHeartbeatChanged = 0x01010002U, ///< DP/SP 在线、迟到、超时或阵面位变化；argument0 为来源，argument1 为状态及位图。
    ControlAcquisitionChanged = 0x04010006U, ///< 本地采集状态变化；argument0 为 1 开始、0 停止，不代表设备起控。
    ControlUdpSubmissionFailed = 0x04010007U, ///< 控制报文未提交本地 UDP；argument0 为消息类型，argument1 为工作位。
    ControlUdpSubmissionRecovered = 0x04010008U ///< 0x0001 周期报文本地提交从失败恢复；不代表设备收到。
};

/**
 * @brief 判断稳定事件码是否已登记为可发布、可归档的业务事实。
 * @param code 待核对的事件码。
 * @return 发布门禁和 SQLite 归档共用同一名单；未知码返回 false。
 */
constexpr bool isRegisteredEventCode(EventCode code) noexcept
{
    switch (code) {
    case EventCode::DeviceMalformedFrame:
    case EventCode::DeviceHeartbeatChanged:
    case EventCode::TrackPublishRejected:
    case EventCode::SystemTopicSequenceGap:
    case EventCode::SystemArchiveHealthChanged:
    case EventCode::SystemArchiveTrackGap:
    case EventCode::SystemArchiveEventGap:
    case EventCode::ControlDatagramSendFailed:
    case EventCode::ControlTaskLifecycleChanged:
    case EventCode::ControlTaskControlSettingsPersistFailed:
    case EventCode::ControlTaskControlSettingsInvariantFailed:
    case EventCode::ControlAcquisitionChanged:
    case EventCode::ControlUdpSubmissionFailed:
    case EventCode::ControlUdpSubmissionRecovered:
        return true;
    }
    return false;
}

/**
 * @brief 保存一条可显示、可归档且无动态成员的已发生事件事实。
 * @note 无效可选字段必须清零；消费者只能依据 flags 解释字段，不能把零自行视为缺失。
 */
struct SystemEvent
{
    AcquisitionId acquisitionId; ///< 关联采集会话；仅 AcquisitionIdValid 置位时有效。
    quint64 requestId = 0; ///< 控制请求编号；仅 RequestIdValid 置位时有效，不等同于 SampleMeta::traceId。
    qint64 argument0 = 0; ///< 事件码登记的第一数值参数；单位和含义由具体 EventCode 固定。
    qint64 argument1 = 0; ///< 事件码登记的第二数值参数；单位和含义由具体 EventCode 固定。
    DeviceId deviceId; ///< 关联雷达设备/阵面；仅 DeviceIdValid 置位时有效。
    TaskId taskId; ///< 关联任务；仅 TaskIdValid 置位时有效。
    quint32 trackId = 0; ///< 厂家航迹号；仅 TrackIdValid 置位时有效，零可以是合法航迹号。
    EventCode eventCode = EventCode::DeviceMalformedFrame; ///< 已登记且域编码与 domain 一致的稳定事件码。
    quint32 messageTemplateId = 0; ///< 中文显示模板注册表句柄；第一版尚无注册表，发布时必须为零。
    EventSubjectFlags flags = 0; ///< 可选主体与诊断参数有效性；未知位必须被门禁拒绝。
    EventDomain domain = EventDomain::Device; ///< 事实拥有域，不承担消费者路由。
    EventSeverity severity = EventSeverity::Info; ///< 影响级别，不代表故障当前仍然存在。
};

static_assert(sizeof(SystemEvent) == 56, "SystemEvent layout must remain 56 bytes");

/**
 * @brief 从稳定事件码读取其编码的业务域。
 * @param code 已登记或待校验的事件码。
 * @return 事件码最高 8 位对应的 EventDomain 数值。
 */
constexpr EventDomain eventDomainOf(EventCode code) noexcept
{
    return static_cast<EventDomain>((static_cast<quint32>(code) >> 24U) & 0xFFU);
}

} // namespace core::model
