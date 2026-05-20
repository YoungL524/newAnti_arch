#pragma once

#include <QObject>
#include <QJsonObject>
#include <functional>
#include <QHash>

class QTcpServer;
class QTcpSocket;
class QWebSocketServer;
class QWebSocket;

/**
 * @brief HTTP + WebSocket 服务 (基于 Qt5::Network + Qt5::WebSockets)
 *
 * - QTcpServer 端口 8080: 静态文件服务 + REST API
 * - QWebSocketServer 端口 8081: 实时数据推送
 */
class HttpServer : public QObject
{
    Q_OBJECT

public:
    using JsonHandler = std::function<QJsonObject(const QJsonObject &)>;

    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() override;

    /// 启动服务，返回是否成功
    bool start(quint16 httpPort = 8080,
               quint16 wsPort = 8081,
               const QString &staticDir = QString());

    void stop();

    /// 注册 REST 处理器
    void handleGet(const QString &path, JsonHandler handler);
    void handlePost(const QString &path, JsonHandler handler);

    /// 向所有 WebSocket 客户端广播 JSON 消息
    void broadcast(const QJsonObject &msg);

    /// 当前 HTTP 端口
    quint16 httpPort() const { return m_httpPort; }
    /// 当前 WebSocket 端口
    quint16 wsPort() const { return m_wsPort; }

signals:
    void serverLog(const QString &level, const QString &msg);

private slots:
    void onNewHttpConnection();
    void onHttpReadyRead();
    void onNewWsConnection();
    void onWsDisconnected();
    void onHttpDisconnected();

private:
    void parseHttpRequest(QTcpSocket *sock, const QByteArray &raw);
    void serveStaticFile(QTcpSocket *sock, const QString &path);
    void serveIndex(QTcpSocket *sock);
    void sendJson(QTcpSocket *sock, int status, const QJsonObject &obj);
    void sendError(QTcpSocket *sock, int status, const QString &text);
    static QString mimeType(const QString &path);
    static QByteArray httpDate();

    QTcpServer *m_httpServer = nullptr;
    QWebSocketServer *m_wsServer = nullptr;
    QList<QWebSocket *> m_wsClients;

    QString m_staticDir;
    quint16 m_httpPort = 8080;
    quint16 m_wsPort = 8081;

    QHash<QString, JsonHandler> m_getHandlers;
    QHash<QString, JsonHandler> m_postHandlers;

    /// 连接 → 累积缓冲区 (处理分块到达的 HTTP 请求)
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
