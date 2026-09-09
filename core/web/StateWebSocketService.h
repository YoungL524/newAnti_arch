/**
 * @file StateWebSocketService.h
 * @brief 从DeviceState主动读取完整快照，提供心跳Web出口。
 * @details Doorbell只请求共享Lane；从start开始阅读。每个连接首次读最新值，后续按revision合并推送。
 * 不解析协议、不判断掉线，不拥有第二份状态；最多8连接，每连接待发送上限64KiB。
 */
#pragma once
#include "core/model/RuntimeState.h"
#include "core/datadomain/LatestState.h"
#include "core/execution/ExecutionLane.h"
#include <QWebSocketServer>
#include <QWebSocket>
#include <QSet>
namespace core::web {
/** @brief 与Lane同线程执行的低频状态Reader，State及Lane必须比本对象先构造、后析构。 */
class StateWebSocketService final : public QObject {
public:
    /** @brief 绑定只读状态及已有执行通道；不启动线程。 */
    StateWebSocketService(const core::domain::LatestState<core::model::DeviceState>& state, core::execution::ExecutionLane& lane);
    /** @brief 关闭客户端并释放Lane位，所属线程析构。 */
    ~StateWebSocketService() override;
    /** @brief 监听本地回环地址，端口零供测试；失败返回false。 */
    bool start(quint16 port);
    /** @brief 关闭监听与连接，可重复调用，必须在所属线程执行。 */
    void stop();
    /** @brief 返回监听错误供组合根诊断。 */
    QString lastError() const { return m_server.errorString(); }
    /** @brief 返回实际监听端口。 */
    quint16 port() const;
private:
    /** @brief 主动读取完整State并向指定连接序列化；慢连接断开后可重连取新快照。 */
    void send(QWebSocket* socket);
    const core::domain::LatestState<core::model::DeviceState>& m_state; ///< 唯一事实来源。
    core::execution::ExecutionLane& m_lane; ///< 借用当前事件循环执行器。
    core::execution::ExecutionSlot m_slot; ///< Reader调度位。
    quint64 m_revision = 0; ///< 已处理修订号。
    QWebSocketServer m_server; ///< 网络边界。
    QSet<QWebSocket*> m_clients; ///< 最多8个连接，无业务队列。
};
}
