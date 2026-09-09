/**
 * @file CoreDataDomain.h
 * @brief 定义新架构共享强类型数据域的组合入口，持有航迹历史与统一事件日志。
 * @details 组合根按容量策略创建 CoreDataDomain；上游航迹 Producer 向 trackTopic 写入已完成识别的
 * Sample<TrackPacket>，下游 ArchiveReader、WebReader 与后续 PluginReader 使用各自 cursor 主动读取。
 * 本类不解析协议、不验证 TrackPacket、不登记 Consumer、不创建线程或 Command 通道；新人应从
 * trackTopic()/eventTopic() 阅读当前已落地的数据平面，再进入 HistoryTopic.h 查看 retention 与 gap 语义。
 */

#pragma once

#include "core/datadomain/HistoryTopic.h"
#include "core/datadomain/EventTopic.h"
#include "core/model/SystemEvent.h"
#include "core/model/TrackPacket.h"
#include "core/model/RuntimeState.h"
#include "core/datadomain/LatestState.h"

namespace core::domain {

/**
 * @brief 集中拥有进程内共享强类型 Topic，但不承担中央消息路由或执行调度。
 * @details 当前只建立具有真实字段合同的 TrackPacket 历史和 SystemEvent 有界日志；Point、Optical 与
 * 三类State按已冻结合同保留最后值，无owner时保持未设置。对象由应用组合根拥有，Consumer只能借用只读引用。
 */
class CoreDataDomain final
{
public:
    /**
     * @brief 创建航迹共享历史和统一事件日志。
     * @param trackHistoryCapacity 航迹 RingBuffer 固定槽位数，必须大于零；容量策略由组合根配置。
     * @param eventHistoryCapacity 事件 RingBuffer 固定槽位数，必须大于零；归档落后时仍允许覆盖并报告 gap。
     * @throws std::invalid_argument 容量小于等于零时由 HistoryTopic 拒绝构造。
     */
    CoreDataDomain(qsizetype trackHistoryCapacity, qsizetype eventHistoryCapacity);

    /** @brief 禁止复制数据域，确保全进程只有组合根明确拥有的 Topic 实例。 */
    CoreDataDomain(const CoreDataDomain&) = delete;

    /** @brief 禁止复制赋值，避免替换运行中 Reader 所借用的 Topic。 */
    CoreDataDomain& operator=(const CoreDataDomain&) = delete;

    /** @brief 禁止移动数据域，保持 Topic Doorbell 地址和 Qt 线程亲和性稳定。 */
    CoreDataDomain(CoreDataDomain&&) = delete;

    /** @brief 禁止移动赋值，防止已取得 Topic 引用的 Consumer 发生悬空。 */
    CoreDataDomain& operator=(CoreDataDomain&&) = delete;

    /**
     * @brief 返回航迹 Producer 和 Reader 共用的有界历史。
     * @return 数据域拥有的唯一 HistoryTopic<TrackPacket> 可写引用。
     * @note 仅 Producer 应调用 publish；组合根应向纯 Reader 暴露 const 引用。
     */
    HistoryTopic<core::model::TrackPacket>& trackTopic() noexcept;

    /**
     * @brief 返回供只读 Consumer 使用的航迹有界历史。
     * @return 数据域拥有的唯一 HistoryTopic<TrackPacket> 常量引用。
     */
    const HistoryTopic<core::model::TrackPacket>& trackTopic() const noexcept;

    /**
     * @brief 返回四类事件 Producer 共用的有界事件日志。
     * @return 数据域拥有的唯一 EventTopic<SystemEvent> 可写引用。
     * @note 只有通过 SystemEventPublisher 门禁的 Producer 应调用 publish。
     */
    EventTopic<core::model::SystemEvent>& eventTopic() noexcept;

    /**
     * @brief 返回供监控、Web、日志和归档 Consumer 顺序读取的事件日志。
     * @return 数据域拥有的唯一 EventTopic<SystemEvent> 常量引用。
     */
    const EventTopic<core::model::SystemEvent>& eventTopic() const noexcept;

public:
    /** @brief 返回设备完整快照；心跳阶段唯一写入者为HeartbeatStateOwner，读取方使用const重载。 */
    LatestState<core::model::DeviceState>& deviceState() noexcept { return m_deviceState; }
    /** @brief 返回设备状态只读入口，生命周期由数据域拥有。 */
    const LatestState<core::model::DeviceState>& deviceState() const noexcept { return m_deviceState; }
    /** @brief 返回任务状态，后续由TaskService唯一写入；当前未设置不伪造运行任务。 */
    LatestState<core::model::TaskState>& taskState() noexcept { return m_taskState; }
    /** @brief 返回任务状态只读入口。 */
    const LatestState<core::model::TaskState>& taskState() const noexcept { return m_taskState; }
    /** @brief 返回系统状态，后续由SystemStateOwner唯一写入。 */
    LatestState<core::model::SystemState>& systemState() noexcept { return m_systemState; }
    /** @brief 返回系统状态只读入口。 */
    const LatestState<core::model::SystemState>& systemState() const noexcept { return m_systemState; }
private:
    LatestState<core::model::DeviceState> m_deviceState; ///< 唯一设备完整状态。
    LatestState<core::model::TaskState> m_taskState; ///< 唯一任务完整状态。
    LatestState<core::model::SystemState> m_systemState; ///< 唯一系统完整状态。
    HistoryTopic<core::model::TrackPacket> m_trackTopic; ///< 航迹本体唯一驻留位置；容量与 Reader cursor 无关。
    EventTopic<core::model::SystemEvent> m_eventTopic; ///< 四类已发生事件的唯一有界内存日志。
};

} // namespace core::domain
