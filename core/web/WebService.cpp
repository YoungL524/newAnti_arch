#include "WebService.h"
#include "HttpParser.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDebug>
#include <QUrl>

namespace {
    constexpr int kMaxHeaderBytes  = 16 * 1024;       // 16 KiB
    constexpr int kMaxBodyBytes    = 1024 * 1024;      // 1 MiB
    constexpr int kMaxBufferedBytes = 2 * 1024 * 1024; // 2 MiB
    constexpr int kIdleTimeoutMs   = 30 * 1000;        // 30 s
}

namespace web {

// ============================================================
// Construction
// ============================================================
WebService::WebService(QObject* parent)
    : QObject(parent)
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

    for (auto it = m_idleTimers.begin(); it != m_idleTimers.end(); ++it) {
        if (it.value()) it.value()->stop();
    }
    m_idleTimers.clear();
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
void WebService::handlePut(const QString& path, JsonHandler handler) {
    m_putHandlers[path] = std::move(handler);
}
void WebService::handleDelete(const QString& path, JsonHandler handler) {
    m_deleteHandlers[path] = std::move(handler);
}
void WebService::handleGetPrefix(const QString& prefix, PrefixHandler handler) {
    m_getPrefixHandlers[prefix] = std::move(handler);
}
void WebService::handlePutPrefix(const QString& prefix, PrefixHandler handler) {
    m_putPrefixHandlers[prefix] = std::move(handler);
}
void WebService::handleDeletePrefix(const QString& prefix, PrefixHandler handler) {
    m_deletePrefixHandlers[prefix] = std::move(handler);
}

void WebService::setStaticDir(const QString& dir) {
    m_staticDir = QDir(dir).absolutePath() + QStringLiteral("/");
}

// ============================================================
// WebSocket
// ============================================================
void WebService::broadcast(const QJsonObject& msg) {
    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    constexpr qint64 kCongestionThreshold = 64 * 1024;  // 64KB pending write data

    for (int i = m_wsClients.size() - 1; i >= 0; --i) {
        auto* ws = m_wsClients[i];

        // 慢客户端检测：写缓冲区积压超过阈值时断开
        if (ws->bytesToWrite() > kCongestionThreshold) {
            qWarning() << QStringLiteral("[WebService] WS 慢客户端断开: %1 (%2 bytes pending)")
                          .arg(ws->peerAddress().toString())
                          .arg(ws->bytesToWrite());
            ws->close();
            m_wsClients.removeAt(i);
            continue;
        }

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

        // Per-socket idle timer
        auto* timer = new QTimer(sock);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, sock, [sock, this]() {
            qDebug() << "[WebService] HTTP 空闲超时" << sock->peerAddress().toString();
            sock->disconnectFromHost();
        });
        timer->start(kIdleTimeoutMs);
        m_idleTimers[sock] = timer;
    }
}

void WebService::onHttpDisconnected() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (sock) {
        if (auto* t = m_idleTimers.take(sock)) t->stop();
        m_buffers.remove(sock);
        sock->deleteLater();
    }
}

void WebService::onHttpReadyRead() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    // Reset idle timer
    if (auto* t = m_idleTimers.value(sock)) t->start(kIdleTimeoutMs);

    QByteArray& buf = m_buffers[sock];
    buf.append(sock->readAll());

    // Oversized total buffer guard
    if (buf.size() > kMaxBufferedBytes) {
        qWarning() << "[WebService] 缓冲区超限，断开客户端" << sock->peerAddress().toString();
        m_buffers.remove(sock);
        sock->disconnectFromHost();
        return;
    }

    // Oversized header guard: if no \r\n\r\n found and buffer exceeds max header size
    if (!buf.contains("\r\n\r\n") && buf.size() > kMaxHeaderBytes) {
        qWarning() << "[WebService] 请求头部超限，断开客户端" << sock->peerAddress().toString();
        m_buffers.remove(sock);
        sock->disconnectFromHost();
        return;
    }

    // Process all complete requests in the buffer (handle pipelining)
    while (true) {
        bool processed = dispatchRequest(sock, buf);
        if (!processed) break;
    }
}

/// @return true if a request was fully processed; false if incomplete or errored.
bool WebService::dispatchRequest(QTcpSocket* sock, QByteArray& raw) {
    auto result = tryParseRequest(raw);
    if (!result) {
        sendError(sock, 400, QStringLiteral("Bad Request"));
        m_buffers.remove(sock);
        sock->disconnectFromHost();
        return false;
    }
    if (result->consumed == 0) {
        return false;  // incomplete, wait for more data
    }

    const auto& req = result->request;
    const bool keepAlive = result->keepAlive;

    // Body size limit
    if (req.body.size() > kMaxBodyBytes) {
        sendError(sock, 413, QStringLiteral("Payload Too Large"), keepAlive);
        m_buffers.remove(sock);
        sock->disconnectFromHost();
        return false;
    }

    QString cleanPath = req.path;

    auto parseBody = [&]() -> QJsonObject {
        if (req.body.isEmpty()) return {};
        QJsonDocument doc = QJsonDocument::fromJson(req.body);
        return doc.isObject() ? doc.object() : QJsonObject();
    };

    auto body = parseBody();

    // Prefix-match helper: extract suffix after prefix
    auto prefixMatch = [&](const QHash<QString, PrefixHandler>& handlers,
                            const QString& methodLabel) -> bool {
        for (auto it = handlers.begin(); it != handlers.end(); ++it) {
            if (cleanPath.startsWith(it.key())) {
                QString suffix = cleanPath.mid(it.key().size());
                if (!suffix.isEmpty() && suffix[0] == QChar('/')) suffix = suffix.mid(1);
                suffix = QUrl::fromPercentEncoding(suffix.toUtf8());
                auto res = it.value()(suffix, body);
                sendJson(sock, res.value(QStringLiteral("ok")).toBool() ? 200 : 400, res, keepAlive);
                return true;
            }
        }
        return false;
    };

    // Route
    if (req.method == Method::GET && cleanPath == QStringLiteral("/")) {
        serveIndex(sock, keepAlive);
    } else if (req.method == Method::GET && m_getHandlers.contains(cleanPath)) {
        sendJson(sock, 200, m_getHandlers[cleanPath](body), keepAlive);
    } else if (req.method == Method::GET) {
        if (!prefixMatch(m_getPrefixHandlers, "GET"))
            serveStaticFile(sock, cleanPath, keepAlive);
    } else if (req.method == Method::POST && m_postHandlers.contains(cleanPath)) {
        auto res = m_postHandlers[cleanPath](body);
        sendJson(sock, res.value(QStringLiteral("ok")).toBool() ? 200 : 400, res, keepAlive);
    } else if (req.method == Method::PUT && m_putHandlers.contains(cleanPath)) {
        auto res = m_putHandlers[cleanPath](body);
        sendJson(sock, res.value(QStringLiteral("ok")).toBool() ? 200 : 400, res, keepAlive);
    } else if (req.method == Method::PUT) {
        if (!prefixMatch(m_putPrefixHandlers, "PUT"))
            sendError(sock, 404, QStringLiteral("Not Found"), keepAlive);
    } else if (req.method == Method::DELETE && m_deleteHandlers.contains(cleanPath)) {
        auto res = m_deleteHandlers[cleanPath](body);
        sendJson(sock, res.value(QStringLiteral("ok")).toBool() ? 200 : 400, res, keepAlive);
    } else if (req.method == Method::DELETE) {
        if (!prefixMatch(m_deletePrefixHandlers, "DELETE"))
            sendError(sock, 404, QStringLiteral("Not Found"), keepAlive);
    } else {
        sendError(sock, 404, QStringLiteral("Not Found"), keepAlive);
    }

    if (keepAlive) {
        // Trim consumed bytes but keep socket open for next request
        raw.remove(0, static_cast<int>(result->consumed));
        if (raw.isEmpty())
            m_buffers.remove(sock);
    } else {
        m_buffers.remove(sock);
        sock->disconnectFromHost();
    }
    return true;
}

// ============================================================
// Static Files
// ============================================================
void WebService::serveIndex(QTcpSocket* sock, bool keepAlive) {
    QString idx = m_staticDir + QStringLiteral("index.html");
    if (QFileInfo::exists(idx)) {
        QFile f(idx);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray content = f.readAll();
            HttpResponse resp;
            resp.setContent(content, QStringLiteral("text/html; charset=utf-8"));
            if (keepAlive) resp.applyKeepAlive();
            sock->write(serializeResponse(resp));
            return;
        }
    }
    QJsonObject obj;
    obj[QStringLiteral("ok")]    = false;
    obj[QStringLiteral("error")] = QStringLiteral(
        "前端静态文件未构建。请在 ui/ 目录执行: npm install && npm run build");
    sendJson(sock, 503, obj, false);
}

void WebService::serveStaticFile(QTcpSocket* sock, const QString& path, bool keepAlive) {
    if (path.contains(QStringLiteral(".."))) {
        sendError(sock, 403, QStringLiteral("Forbidden"), keepAlive);
        return;
    }
    QString filePath = m_staticDir + path.mid(1);
    if (!QFileInfo::exists(filePath)) {
        sendError(sock, 404, QStringLiteral("Not Found"), keepAlive);
        return;
    }
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        sendError(sock, 500, QStringLiteral("Internal Server Error"), keepAlive);
        return;
    }
    QByteArray content = f.readAll();
    HttpResponse resp;
    resp.setContent(content, web::mimeType(filePath));
    if (keepAlive) resp.applyKeepAlive();
    sock->write(serializeResponse(resp));
}

// ============================================================
// Response Helpers
// ============================================================
void WebService::sendJson(QTcpSocket* sock, int status, const QJsonObject& obj, bool keepAlive) {
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    HttpResponse resp;
    resp.statusCode = status;
    resp.setJson(body);
    if (keepAlive) resp.applyKeepAlive();
    sock->write(serializeResponse(resp));
}

void WebService::sendError(QTcpSocket* sock, int status, const QString& text, bool keepAlive) {
    QJsonObject obj;
    obj[QStringLiteral("ok")]    = false;
    obj[QStringLiteral("error")] = text;
    sendJson(sock, status, obj, keepAlive);
}

} // namespace web