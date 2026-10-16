#include "core/web/WebSocketGateway.h"
#include "core/web/LocalWebAccess.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QWebSocket>

namespace core::web {

/**
 * @brief 只建立服务器连接生命周期信号，不创建线程或读取任何Topic。
 */
WebSocketGateway::WebSocketGateway(QObject *parent)
    : QObject(parent)
    , m_server(QStringLiteral("DCMP WebSocket V1"), QWebSocketServer::NonSecureMode, this)
{
    QObject::connect(&m_server, &QWebSocketServer::newConnection,
                     this, &WebSocketGateway::acceptPendingConnections);
}

/**
 * @brief 在成员析构前同步释放所有连接，避免Reader借用悬空socket。
 */
WebSocketGateway::~WebSocketGateway()
{
    stop();
}

/**
 * @brief V1只开放回环监听；接纳连接时仍独立检查本机 Host/Origin，不依赖代理代替 Core 门禁。
 */
bool WebSocketGateway::start(quint16 port)
{
    return !m_server.isListening() && m_server.listen(QHostAddress::LocalHost, port);
}

/**
 * @brief 先阻止新连接，再断开并同步删除现有socket；不依赖事件循环继续运行。
 */
void WebSocketGateway::stop()
{
    m_server.close();
    const auto clients = m_clients;
    m_clients.clear();
    for (QWebSocket *client : clients) {
        QObject::disconnect(client, nullptr, this, nullptr);
        delete client;
    }
}

/**
 * @brief 仅暴露操作系统确认的监听端口，不以配置值伪装启动成功。
 */
quint16 WebSocketGateway::port() const noexcept
{
    return m_server.isListening() ? m_server.serverPort() : 0;
}

/**
 * @brief 查询连接集合大小；该集合不保存每连接业务副本。
 */
qsizetype WebSocketGateway::clientCount() const noexcept
{
    return m_clients.size();
}

/**
 * @brief 在发送前同时验证所有权与积压，超过硬上限立即断开而不反压Reader或Producer。
 */
bool WebSocketGateway::send(QWebSocket &client, const QJsonObject &message)
{
    if (!m_clients.contains(&client)) return false;
    const QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (client.bytesToWrite() + bytes.size() > maxPendingBytes) {
        client.abort();
        return false;
    }
    client.sendTextMessage(QString::fromUtf8(bytes));
    return true;
}

/**
 * @brief 单次序列化后遍历连接；断开慢客户端不会影响其他客户端或上游数据域。
 */
void WebSocketGateway::broadcast(const QJsonObject &message)
{
    if (m_clients.isEmpty()) return;
    const QString payload = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    const qint64 payloadBytes = payload.toUtf8().size();
    const auto clients = m_clients;
    for (QWebSocket *client : clients) {
        if (client->bytesToWrite() + payloadBytes > maxPendingBytes) client->abort();
        else client->sendTextMessage(payload);
    }
}

/**
 * @brief 在任何业务快照前验证路径、本机 Host/Origin 和容量，远程网页即使直连 Core 也无法读取数据。
 */
void WebSocketGateway::acceptPendingConnections()
{
    while (m_server.hasPendingConnections()) {
        QWebSocket *client = m_server.nextPendingConnection();
        if (!client) continue;
        if (client->requestUrl().path() != QStringLiteral("/ws/v1")
            || !localAuthority(client->requestUrl().authority().toLatin1())
            || !localOrigin(client->origin().toLatin1())) {
            client->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("仅允许本机页面访问/ws/v1"));
            client->deleteLater();
            continue;
        }
        if (m_clients.size() >= maxClients) {
            client->abort();
            client->deleteLater();
            continue;
        }
        m_clients.insert(client);
        QObject::connect(client, &QWebSocket::disconnected, this, [this, client] {
            m_clients.remove(client);
            client->deleteLater();
        });
        emit clientConnected(client);
    }
}

} // namespace core::web
