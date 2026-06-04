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
    void dispatchRequest(QTcpSocket* sock, const QByteArray& raw);
    void serveStaticFile(QTcpSocket* sock, const QString& path);
    void serveIndex(QTcpSocket* sock);
    void sendJson(QTcpSocket* sock, int status, const QJsonObject& obj);
    void sendError(QTcpSocket* sock, int status, const QString& text);

    // HTTP
    QTcpServer*  m_httpServer = nullptr;
    quint16      m_httpPort   = 8088;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QHash<QString, JsonHandler>    m_getHandlers;
    QHash<QString, JsonHandler>    m_postHandlers;
    QString      m_staticDir;

    // WebSocket
    QWebSocketServer*       m_wsServer = nullptr;
    QList<QWebSocket*>      m_wsClients;
    quint16                 m_wsPort   = 8089;

};

} // namespace web