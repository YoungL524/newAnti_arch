/**
 * @file StateWebSocketService.h
 * @brief 从DeviceState主动读取完整快照，并通过唯一WebSocketGateway输出。
 * @details Doorbell只请求共享Lane；每个Gateway连接首次读取最新值，后续按revision合并推送。本类不拥有
 * socket、不判断DP/SP掉线、不保存第二份状态，也不处理Command；新人应从构造函数的三项借用依赖阅读。
 */
#pragma once

#include "core/datadomain/LatestState.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/RuntimeState.h"

#include <QJsonObject>

QT_FORWARD_DECLARE_CLASS(QWebSocket)

namespace core::web {

class WebSocketGateway;

/** @brief 与Lane同线程执行的低频状态Reader，三项借用依赖必须覆盖本对象生命周期。 */
class StateWebSocketService final : public QObject
{
public:
    /** @brief 绑定只读状态、已有执行通道和唯一Gateway；不启动线程或网络端口。 */
    StateWebSocketService(
        const core::domain::LatestState<core::model::DeviceState> &state,
        core::execution::ExecutionLane &lane,
        WebSocketGateway &gateway);

    /** @brief 断开三项借用依赖的信号并释放Lane位，必须在所属线程析构。 */
    ~StateWebSocketService() override;

private:
    /** @brief 主动读取完整State并向新连接发送一次快照；未初始化State时不伪造全零消息。 */
    void send(QWebSocket *socket);

    /** @brief 形成当前DeviceState网络DTO；State未初始化时返回空对象。 */
    QJsonObject message() const;

    const core::domain::LatestState<core::model::DeviceState> &m_state; ///< 唯一事实来源。
    core::execution::ExecutionLane &m_lane; ///< 借用当前事件循环执行器。
    WebSocketGateway &m_gateway; ///< 借用唯一网络连接owner。
    core::execution::ExecutionSlot m_slot; ///< Reader在共享Lane中的调度位。
    quint64 m_revision = 0; ///< 已处理的DeviceState独立修订号。
};

} // namespace core::web
