/**
 * @file WebSocketGateway.h
 * @brief 定义React实时接口唯一的/ws/v1连接与有界发送出口。
 * @details 上游State/Track WebReader提交已经形成的网络JSON，下游为最多8个浏览器连接；本类只拥有
 * QWebSocketServer和连接对象，不读取CoreDataDomain、不保存业务历史、不判断心跳超时或执行Command。
 * 新人应从start()和clientConnected信号阅读连接生命周期，再查看各强类型WebReader的读取合同。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QWebSocketServer>

QT_FORWARD_DECLARE_CLASS(QWebSocket)

namespace core::web {

/**
 * @brief 在一个端口上复用State与Track网络连接，但不合并二者的cursor、revision或业务状态。
 * @warning 所有方法和信号必须在Gateway所属Qt线程执行；慢连接超过4MiB待发送数据时被主动断开。
 */
class WebSocketGateway final : public QObject
{
    Q_OBJECT

public:
    /** @brief 创建尚未监听的V1 Gateway；parent负责可选Qt对象生命周期。 */
    explicit WebSocketGateway(QObject *parent = nullptr);

    /** @brief 关闭监听与全部连接，可重复调用。 */
    ~WebSocketGateway() override;

    /**
     * @brief 在本机回环地址监听/ws/v1。
     * @param port 固定部署端口；零用于测试时由操作系统分配。
     * @return 成功开始监听返回true；重复启动或端口占用返回false。
     */
    bool start(quint16 port);

    /** @brief 关闭监听和全部客户端，不影响任何WebReader的cursor或投影状态。 */
    void stop();

    /** @brief 返回实际监听端口；未监听时返回零。 */
    quint16 port() const noexcept;

    /** @brief 返回当前已接纳的/ws/v1客户端数量。 */
    qsizetype clientCount() const noexcept;

    /**
     * @brief 向一个仍受Gateway管理的客户端发送紧凑JSON。
     * @param client clientConnected给出的借用连接。
     * @param message Reader形成的单条网络DTO。
     * @return 已提交发送返回true；连接无效或积压超限返回false并断开慢连接。
     */
    bool send(QWebSocket &client, const QJsonObject &message);

    /** @brief 向当前全部客户端复用一份序列化文本发送，无客户端时不序列化。 */
    void broadcast(const QJsonObject &message);

Q_SIGNALS:
    /**
     * @brief 通知各WebReader为新客户端发送自己的当前快照。
     * @param client Gateway拥有、仅在回调期及其连接存续期可借用的客户端。
     * @note 本信号是网络连接事件，不是CoreDataDomain Doorbell，也不携带业务事实。
     */
    void clientConnected(QWebSocket *client);

private Q_SLOTS:
    /** @brief 接纳/ws/v1连接、拒绝错误路径和超额客户端，再通知Reader发送快照。 */
    void acceptPendingConnections();

private:
    static constexpr qsizetype maxClients = 8; ///< V1浏览器并发连接硬上限。
    static constexpr qint64 maxPendingBytes = 4 * 1024 * 1024; ///< 单连接发送积压硬上限。
    QWebSocketServer m_server; ///< 唯一实时网络监听者。
    QSet<QWebSocket *> m_clients; ///< Gateway拥有的活动连接，不含业务消息队列。
};

} // namespace core::web
