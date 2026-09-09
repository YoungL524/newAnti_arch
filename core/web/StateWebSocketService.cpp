#include "StateWebSocketService.h"
#include <QJsonDocument>
#include <QJsonObject>
namespace core::web {
/**
 * @brief 将门铃直接交给线程安全Lane请求，避免在跨线程接收方积累门铃事件。
 */
StateWebSocketService::StateWebSocketService(const core::domain::LatestState<core::model::DeviceState>& state, core::execution::ExecutionLane& lane)
    : m_state(state), m_lane(lane), m_slot(lane.acquireSlot()), m_server(QStringLiteral("DeviceState"), QWebSocketServer::NonSecureMode, this)
{
    connect(state.doorbell(), &core::domain::TopicDoorbell::advanced, this,
            [this](quint64) { m_lane.request(m_slot); }, Qt::DirectConnection);
    connect(&lane, &core::execution::ExecutionLane::executeRequested, this, [this](quint64 mask) {
        if (!(mask & (quint64(1) << m_slot)) || m_revision == m_state.revision()) return;
        m_revision = m_state.revision();
        const auto clients = m_clients;
        for (auto* client : clients) send(client);
    });
    connect(&m_server, &QWebSocketServer::newConnection, this, [this] {
        while (m_server.hasPendingConnections()) {
            auto* client = m_server.nextPendingConnection();
            if (!client) continue;
            if (m_clients.size() >= 8) { client->abort(); client->deleteLater(); continue; }
            m_clients.insert(client);
            connect(client, &QWebSocket::disconnected, this, [this, client] { m_clients.remove(client); client->deleteLater(); });
            send(client);
        }
    });
}
/**
 * @brief 先断通知再关连接，避免关闭时重入集合遍历。
 */
StateWebSocketService::~StateWebSocketService()
{
    disconnect(m_state.doorbell(), nullptr, this, nullptr);
    disconnect(&m_lane, nullptr, this, nullptr);
    m_lane.releaseSlot(m_slot);
    stop();
}
/**
 * @brief 先断连接回调再释放对象，清理不依赖事件循环继续运行。
 */
void StateWebSocketService::stop()
{
    const auto clients = m_clients;
    for (auto* client : clients) { disconnect(client, nullptr, this, nullptr); delete client; }
    m_clients.clear();
    m_server.close();
}
/**
 * @brief 本轮仅开放回环状态出口；远程部署需显式配置网络边界。
 */
bool StateWebSocketService::start(quint16 port) { return !m_server.isListening() && m_server.listen(QHostAddress::LocalHost, port); }
/**
 * @brief 查询操作系统分配端口，供联试连接。
 */
quint16 StateWebSocketService::port() const { return m_server.serverPort(); }
/**
 * @brief 保持状态码原义，以字符串传输64位revision，避免JS精度损失。
 */
void StateWebSocketService::send(QWebSocket* socket)
{
    const auto record = m_state.latest();
    if (!record) return;
    if (socket->bytesToWrite() >= 65536) { socket->abort(); return; }
    const auto& value = record->sample->value;
    QJsonObject json{{"type", "deviceState"}, {"revision", QString::number(record->revision)},
        {"dpHeartbeat", value.dpHeartbeat}, {"spHeartbeat", value.spHeartbeat},
        {"fpOnlineMask", value.fpOnlineMask}, {"fpDataMask", value.fpDataMask}};
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
}
}
