/**
 * @file WebService.h
 * @brief 声明 HTTP/REST 和 WebSocket 传输服务及每连接背压策略。
 * @details WebService 负责监听、请求缓冲、路由匹配、静态资源和 JSON/二进制发送，不持有业务状态。
 *          WebSocket 慢客户端按待发送字节限制隔离，实时航迹允许丢弃并由后续快照恢复。
 */

#pragma once

#include <QObject>
#include <QJsonObject>
#include <QAbstractSocket>
#include <functional>
#include <QHash>
#include <QList>

class QTcpServer;
class QTcpSocket;
class QWebSocketServer;
class QWebSocket;
class QTimer;

namespace web {

/**
 * @brief 在同一 Qt 事件线程中提供轻量 HTTP API、静态文件和 WebSocket 推送服务。
 * @details REST 路由由上层控制器注册，本类只负责协议解析、连接生命周期与响应序列化。
 *          WebSocket 发送采用拥塞阈值保护：慢客户端的待发送字节持续堆积时主动断开，防止单个浏览器拖垮核心进程。
 */
class WebService : public QObject {
    Q_OBJECT
public:
    using JsonHandler = std::function<QJsonObject(const QJsonObject& body)>;
    using GetJsonHandler = std::function<QJsonObject(const QJsonObject& body, const QString& queryString)>;
    using PrefixHandler = std::function<QJsonObject(const QString& suffix, const QJsonObject& body)>;

    struct Result {
        bool   ok      = false;
        quint16 httpPort = 0;
        quint16 wsPort   = 0;
    };

    explicit WebService(QObject* parent = nullptr);
    ~WebService() override;

    /// Start both HTTP and WebSocket servers. Returns false if either fails.
    Result start(quint16 httpPort = 8088, quint16 wsPort = 8089);

    /// Stop all servers and disconnect.
    void stop();

    /// Register REST handlers.
    void handleGet(const QString& path, GetJsonHandler handler);
    void handlePost(const QString& path, JsonHandler handler);
    void handlePut(const QString& path, JsonHandler handler);
    void handleDelete(const QString& path, JsonHandler handler);

    /// Register prefix-matched handlers (path starts with prefix, handler receives suffix + body).
    void handleGetPrefix(const QString& prefix, PrefixHandler handler);
    void handlePostPrefix(const QString& prefix, PrefixHandler handler);
    void handlePutPrefix(const QString& prefix, PrefixHandler handler);
    void handleDeletePrefix(const QString& prefix, PrefixHandler handler);

    /// Broadcast JSON to all WebSocket clients (main thread safe).
    void broadcast(const QJsonObject& msg);
    /// Send JSON to one connected client (used for connection-scoped initial state).
    bool sendToClient(QWebSocket* client, const QJsonObject& msg);

    /// Static file directory.
    void setStaticDir(const QString& dir);

signals:
    void serverLog(const QString& level, const QString& msg);
    void webSocketConnected(QWebSocket* client);

private slots:
    void onHttpConnection();
    void onHttpReadyRead();
    void onHttpDisconnected();
    void onHttpSocketError(QAbstractSocket::SocketError error);
    void onWsConnection();
    void onWsDisconnected();

private:
    bool dispatchRequest(QTcpSocket* sock, QByteArray& raw);
    void closeHttpSocket(QTcpSocket* sock);
    void cleanupHttpSocket(QTcpSocket* sock);
    bool isSocketWritable(QTcpSocket* sock) const;
    bool writeToSocket(QTcpSocket* sock, const QByteArray& data);
    void serveIndex(QTcpSocket* sock, bool keepAlive = false);
    void serveStaticFile(QTcpSocket* sock, const QString& path, bool keepAlive = false);
    void sendJson(QTcpSocket* sock, int status, const QJsonObject& obj, bool keepAlive = false);
    void sendError(QTcpSocket* sock, int status, const QString& text, bool keepAlive = false);

    // HTTP
    QTcpServer*  m_httpServer = nullptr;
    quint16      m_httpPort   = 8088;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QHash<QTcpSocket*, QTimer*>    m_idleTimers;
    QHash<QString, GetJsonHandler> m_getHandlers;
    QHash<QString, JsonHandler>    m_postHandlers;
    QHash<QString, JsonHandler>    m_putHandlers;
    QHash<QString, JsonHandler>    m_deleteHandlers;
    QHash<QString, PrefixHandler>  m_getPrefixHandlers;
    QHash<QString, PrefixHandler>  m_postPrefixHandlers;
    QHash<QString, PrefixHandler>  m_putPrefixHandlers;
    QHash<QString, PrefixHandler>  m_deletePrefixHandlers;
    QString      m_staticDir;

    // WebSocket
    QWebSocketServer*       m_wsServer = nullptr;
    QList<QWebSocket*>      m_wsClients;
    quint16                 m_wsPort   = 8089;

};

} // namespace web
