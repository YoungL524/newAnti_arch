#include "WebService.h"
#include "HttpParser.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDebug>

namespace web {

// ============================================================
// Construction
// ============================================================
WebService::WebService(AppController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
    // Default static dir relative to executable
    m_staticDir = QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../ui/dist/");
    m_staticDir = QDir(m_staticDir).absolutePath() + QStringLiteral("/");
}

WebService::~WebService() {
    stop();
}

// ============================================================
// Lifecycle
// ============================================================
WebService::Result WebService::start(quint16 httpPort, quint16 wsPort) {
    m_httpPort = httpPort;
    m_wsPort   = wsPort;

    // ---- HTTP server ----
    m_httpServer = new QTcpServer(this);
    connect(m_httpServer, &QTcpServer::newConnection,
            this, &WebService::onHttpConnection);

    if (!m_httpServer->listen(QHostAddress::AnyIPv4, httpPort)) {
        emit serverLog(QStringLiteral("error"),
            QStringLiteral("HTTP 监听失败: %1").arg(m_httpServer->errorString()));
        return {false, 0, 0};
    }

    // ---- WebSocket server ----
    m_wsServer = new QWebSocketServer(QStringLiteral("CoreWS"),
                                       QWebSocketServer::NonSecureMode, this);
    connect(m_wsServer, &QWebSocketServer::newConnection,
            this, &WebService::onWsConnection);

    if (!m_wsServer->listen(QHostAddress::AnyIPv4, wsPort)) {
        emit serverLog(QStringLiteral("error"),
            QStringLiteral("WebSocket 监听失败: %1").arg(m_wsServer->errorString()));
        stop();
        return {false, 0, 0};
    }

    emit serverLog(QStringLiteral("info"),
        QStringLiteral("HTTP : %1:%2").arg(QStringLiteral("127.0.0.1")).arg(httpPort));
    emit serverLog(QStringLiteral("info"),
        QStringLiteral("WS   : %1:%2").arg(QStringLiteral("127.0.0.1")).arg(wsPort));
    emit serverLog(QStringLiteral("info"),
        QStringLiteral("Static: %1").arg(m_staticDir));
    return {true, httpPort, wsPort};
}

void WebService::stop() {
    // WS clients
    for (auto* ws : m_wsClients) {
        ws->close();
        ws->deleteLater();
    }
    m_wsClients.clear();

    if (m_wsServer) {
        m_wsServer->close();
        m_wsServer->deleteLater();
        m_wsServer = nullptr;
    }

    if (m_httpServer) {
        m_httpServer->close();
        m_httpServer->deleteLater();
        m_httpServer = nullptr;
    }

    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_buffers.clear();
}

// ============================================================
// Handler Registration
// ============================================================
void WebService::handleGet(const QString& path, JsonHandler handler) {
    m_getHandlers[path] = std::move(handler);
}

void WebService::handlePost(const QString& path, JsonHandler handler) {
    m_postHandlers[path] = std::move(handler);
}

void WebService::setStaticDir(const QString& dir) {
    m_staticDir = QDir(dir).absolutePath() + QStringLiteral("/");
}

// ============================================================
// WebSocket
// ============================================================
void WebService::broadcast(const QJsonObject& msg) {
    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    for (auto* ws : m_wsClients) {
        ws->sendTextMessage(QString::fromUtf8(data));
    }
}

void WebService::onWsConnection() {
    while (auto* ws = m_wsServer->nextPendingConnection()) {
        m_wsClients.append(ws);
        connect(ws, &QWebSocket::disconnected, this, &WebService::onWsDisconnected);
        emit serverLog(QStringLiteral("info"),
            QStringLiteral("WS 客户端已连接 (%1)").arg(ws->peerAddress().toString()));
    }
}

void WebService::onWsDisconnected() {
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (ws) {
        m_wsClients.removeOne(ws);
        ws->deleteLater();
    }
}

// ============================================================
// HTTP — Connection Handling
// ============================================================
void WebService::onHttpConnection() {
    while (auto* sock = m_httpServer->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, &WebService::onHttpReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &WebService::onHttpDisconnected);
        m_buffers[sock] = QByteArray();
    }
}

void WebService::onHttpDisconnected() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (sock) {
        m_buffers.remove(sock);
        sock->deleteLater();
    }
}

void WebService::onHttpReadyRead() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    m_buffers[sock].append(sock->readAll());
    dispatchRequest(sock, m_buffers[sock]);
}

void WebService::dispatchRequest(QTcpSocket* sock, const QByteArray& raw) {
    auto result = tryParseRequest(raw);
    if (!result) {
        sendError(sock, 400, QStringLiteral("Bad Request"));
        sock->disconnectFromHost();
        return;
    }
    if (result->consumed == 0) {
        return;  // incomplete, wait for more data
    }

    // Request fully parsed; remove buffer to free memory
    m_buffers.remove(sock);

    const auto& req = result->request;
    QString cleanPath = req.path;

    // Route
    if (req.method == Method::GET && cleanPath == QStringLiteral("/")) {
        serveIndex(sock);
    } else if (req.method == Method::GET && m_getHandlers.contains(cleanPath)) {
        QJsonObject params;
        auto result = m_getHandlers[cleanPath](params);
        sendJson(sock, 200, result);
    } else if (req.method == Method::POST && m_postHandlers.contains(cleanPath)) {
        QJsonObject params;
        if (!req.body.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(req.body);
            if (doc.isObject()) params = doc.object();
        }
        auto result = m_postHandlers[cleanPath](params);
        int status = result.value(QStringLiteral("ok")).toBool() ? 200 : 400;
        sendJson(sock, status, result);
    } else if (req.method == Method::GET) {
        serveStaticFile(sock, cleanPath);
    } else {
        sendError(sock, 404, QStringLiteral("Not Found"));
    }

    sock->disconnectFromHost();
}

// ============================================================
// Static Files
// ============================================================
void WebService::serveIndex(QTcpSocket* sock) {
    QString idx = m_staticDir + QStringLiteral("index.html");
    if (QFileInfo::exists(idx)) {
        QFile f(idx);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray content = f.readAll();
            HttpResponse resp;
            resp.setContent(content, QStringLiteral("text/html; charset=utf-8"));
            sock->write(serializeResponse(resp));
            return;
        }
    }
    QJsonObject obj;
    obj[QStringLiteral("ok")]    = false;
    obj[QStringLiteral("error")] = QStringLiteral(
        "前端静态文件未构建。请在 ui/ 目录执行: npm install && npm run build");
    sendJson(sock, 503, obj);
}

void WebService::serveStaticFile(QTcpSocket* sock, const QString& path) {
    if (path.contains(QStringLiteral(".."))) {
        sendError(sock, 403, QStringLiteral("Forbidden"));
        return;
    }
    QString filePath = m_staticDir + path.mid(1);
    if (!QFileInfo::exists(filePath)) {
        sendError(sock, 404, QStringLiteral("Not Found"));
        return;
    }
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        sendError(sock, 500, QStringLiteral("Internal Server Error"));
        return;
    }
    QByteArray content = f.readAll();
    HttpResponse resp;
    resp.setContent(content, web::mimeType(filePath));
    sock->write(serializeResponse(resp));
}

// ============================================================
// Response Helpers
// ============================================================
void WebService::sendJson(QTcpSocket* sock, int status, const QJsonObject& obj) {
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    HttpResponse resp;
    resp.statusCode = status;
    resp.setJson(body);
    sock->write(serializeResponse(resp));
}

void WebService::sendError(QTcpSocket* sock, int status, const QString& text) {
    QJsonObject obj;
    obj[QStringLiteral("ok")]    = false;
    obj[QStringLiteral("error")] = text;
    sendJson(sock, status, obj);
}

} // namespace web