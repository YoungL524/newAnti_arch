#pragma once

#include <QObject>
#include <QJsonObject>
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
 * @brief HTTP (QTcpServer) + WebSocket (QWebSocketServer) service.
 *
 * Owns both servers on the Qt event loop (no threading needed).
 * Handles HTTP parsing via HttpParser, WS push via QWebSocketServer.
 * REST handlers are registered via handleGet/handlePost.
 */
class WebService : public QObject {
    Q_OBJECT
public:
    using JsonHandler = std::function<QJsonObject(const QJsonObject&)>;
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
    void handleGet(const QString& path, JsonHandler handler);
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

    /// Static file directory.
    void setStaticDir(const QString& dir);

signals:
    void serverLog(const QString& level, const QString& msg);

private slots:
    void onHttpConnection();
    void onHttpReadyRead();
    void onHttpDisconnected();
    void onWsConnection();
    void onWsDisconnected();

private:
    bool dispatchRequest(QTcpSocket* sock, QByteArray& raw);
    void scheduleHttpClose(QTcpSocket* sock);
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
    QHash<QString, JsonHandler>    m_getHandlers;
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