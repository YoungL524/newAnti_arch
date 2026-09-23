/**
 * @file EventWebSocketService.h
 * @brief 从统一事件 Topic 主动读取有界批次，并转换为浏览器活动日志。
 * @details 上游 TaskService 等 Producer 发布已发生的 SystemEvent；本 Reader 通过独立 cursor 和共享
 * ExecutionLane 向唯一 WebSocketGateway 输出日志。它不持有事件历史、不执行设备控制、不写 SQLite；
 * 新人应从 refresh() 和 drain() 阅读首次补偿与 Doorbell 合并规则。
 */
#pragma once

#include "core/datadomain/EventTopic.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/StableIdRegistry.h"
#include "core/model/SystemEvent.h"

#include <QObject>
#include <QJsonObject>

QT_FORWARD_DECLARE_CLASS(QWebSocket)

namespace core::web {

class WebSocketGateway;

/** @brief 仅保存事件 cursor 与固定 ScratchBuffer 的 Web 日志 Consumer。 */
class EventWebSocketService final : public QObject
{
public:
    /**
     * @brief 借用唯一事件日志、主线程 Lane、Gateway 和稳定 ID 注册表。
     * @param topic 生命周期覆盖本 Reader 的统一 EventTopic。
     * @param lane 与本 Reader 同线程的共享 Web ExecutionLane。
     * @param gateway 拥有浏览器 Socket 的唯一网络出口。
     * @param stableIds 跨线程解析任务 UUID 的只读注册表。
     */
    EventWebSocketService(
        const core::domain::EventTopic<core::model::SystemEvent> &topic,
        core::execution::ExecutionLane &lane,
        WebSocketGateway &gateway,
        const core::model::StableIdRegistry &stableIds);

    /** @brief 先断开 Doorbell、Lane 和 Gateway，再释放独占调度位。 */
    ~EventWebSocketService() override;

    /** @brief 初始化时主动请求读取，补偿 Reader 接线前已经发布的事件。 */
    void refresh();

private:
    /** @brief 每轮最多处理固定批次，剩余记录交回 Lane 的下一轮唤醒。 */
    void drain(quint64 pendingMask);

    /** @brief 新连接读取当前 Topic 中最近至多 100 条事件，不保留第二份历史。 */
    void sendRecent(QWebSocket *client);

    /** @brief 将稳定事件码映射为当前 UI 的轻量 log 消息，不在 Topic 保存文本。 */
    QJsonObject message(const core::domain::TopicRecord<core::model::SystemEvent> &record) const;

    const core::domain::EventTopic<core::model::SystemEvent> &m_topic; ///< 借用的唯一事件历史，只读。
    core::execution::ExecutionLane &m_lane; ///< 借用主线程合并调度器，不保存业务数据。
    WebSocketGateway &m_gateway; ///< 借用浏览器 Socket Owner，不持有连接。
    const core::model::StableIdRegistry &m_stableIds; ///< 借用 UUID 注册表，不复制任务台账。
    core::domain::TopicScratchBuffer<core::model::SystemEvent> m_scratch {100}; ///< 固定 100 条短期读取槽位。
    core::execution::ExecutionSlot m_slot; ///< 本 Reader 独占 Lane 位。
    core::domain::TopicSequence m_cursor = 0; ///< 最后已广播的事件序号，仅在主线程更新。
};

} // namespace core::web
