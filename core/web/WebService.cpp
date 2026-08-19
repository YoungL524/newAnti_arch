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
#include <QAbstractSocket>
#include <QUrl>

namespace {
    constexpr int kMaxHeaderBytes  = 16 * 1024;
    constexpr int kMaxBodyBytes    = 1024 * 1024;
    constexpr int kMaxBufferedBytes = 2 * 1024 * 1024;
    constexpr int kIdleTimeoutMs   = 30 * 1000;
}

namespace web {

// ============================================================
// Construction
// ============================================================
/**
 * @brief 构造尚未监听端口的 Web 服务，并计算默认静态资源目录。
 * @param parent Qt 父对象；负责随父对象销毁服务。
 * @details 网络服务器和客户端连接均在 start() 后创建，并由本对象或 Qt 的 deleteLater() 管理。
 */
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
/**
 * @brief 依次建立 HTTP 与 WebSocket 监听器；任一失败时回收已启动的另一监听器。
 * @details HTTP 先监听、WebSocket 后监听；WebSocket 失败时关闭已建立的 HTTP 监听，避免出现只有半套接口可用的状态。
 */
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
    m_httpPort = m_httpServer->serverPort();

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
    m_wsPort = m_wsServer->serverPort();

    emit serverLog(QStringLiteral("info"),
        QStringLiteral("HTTP : %1:%2").arg(QStringLiteral("127.0.0.1")).arg(m_httpPort));
    emit serverLog(QStringLiteral("info"),
        QStringLiteral("WS   : %1:%2").arg(QStringLiteral("127.0.0.1")).arg(m_wsPort));
    emit serverLog(QStringLiteral("info"),
        QStringLiteral("Static: %1").arg(m_staticDir));
    return {true, m_httpPort, m_wsPort};
}

/**
 * @brief 关闭所有 WebSocket 客户端以及 HTTP、WebSocket 监听器。
 * @details 客户端先发送关闭握手并排入延迟销毁，随后监听器停止接收新连接；清空容器后可安全重复调用。
 */
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

    const auto socks = m_buffers.keys();
    for (auto* sock : socks) {
        sock->disconnect(this);
        if (auto* t = m_idleTimers.take(sock))
            t->stop();
        m_buffers.remove(sock);
        if (sock->state() != QAbstractSocket::UnconnectedState)
            sock->abort();
        sock->deleteLater();
    }
    m_idleTimers.clear();
}

// ============================================================
// Handler Registration
// ============================================================
/**
 * @brief 注册精确路径 GET 处理器，重复路径覆盖旧处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handleGet(const QString& path, GetJsonHandler handler) {
    m_getHandlers[path] = std::move(handler);
}
/**
 * @brief 注册精确路径 POST 处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handlePost(const QString& path, JsonHandler handler) {
    m_postHandlers[path] = std::move(handler);
}
/**
 * @brief 注册精确路径 PUT 处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handlePut(const QString& path, JsonHandler handler) {
    m_putHandlers[path] = std::move(handler);
}
/**
 * @brief 注册精确路径 DELETE 处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handleDelete(const QString& path, JsonHandler handler) {
    m_deleteHandlers[path] = std::move(handler);
}
/**
 * @brief 注册携带剩余路径参数的 GET 前缀处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handleGetPrefix(const QString& prefix, PrefixHandler handler) {
    m_getPrefixHandlers[prefix] = std::move(handler);
}
/**
 * @brief 注册携带剩余路径参数的 POST 前缀处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handlePostPrefix(const QString& prefix, PrefixHandler handler) {
    m_postPrefixHandlers[prefix] = std::move(handler);
}
/**
 * @brief 注册携带剩余路径参数的 PUT 前缀处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handlePutPrefix(const QString& prefix, PrefixHandler handler) {
    m_putPrefixHandlers[prefix] = std::move(handler);
}
/**
 * @brief 注册携带剩余路径参数的 DELETE 前缀处理器。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::handleDeletePrefix(const QString& prefix, PrefixHandler handler) {
    m_deletePrefixHandlers[prefix] = std::move(handler);
}

/**
 * @brief 保存静态资源根目录绝对路径，后续请求必须解析并限制在该目录内。
 */
void WebService::setStaticDir(const QString& dir) {
    m_staticDir = QDir(dir).absolutePath() + QStringLiteral("/");
}

// ============================================================
// WebSocket
// ============================================================
/**
 * @brief 向所有可用 WebSocket 客户端发送消息，并跳过待发送字节超过阈值的慢客户端。
 * @details 航迹属于可恢复实时数据，宁可丢弃单次广播也不让一个客户端拖住服务线程或无限占用内存。
 */
void WebService::broadcast(const QJsonObject& msg) {
    for (int i = m_wsClients.size() - 1; i >= 0; --i) {
        auto* ws = m_wsClients[i];
        if (!ws || ws->state() != QAbstractSocket::ConnectedState) {
            if (ws) ws->deleteLater();
            m_wsClients.removeAt(i);
            continue;
        }
        sendToClient(ws, msg);
    }
}

/**
 * @brief 向一个已连接 WebSocket 客户端发送紧凑 JSON 消息。
 * @param client 目标客户端，必须仍处于 ConnectedState。
 * @param msg 待序列化对象。
 * @return 消息成功交给 Qt 发送缓冲区时为 true；客户端无效、拥塞或写入失败时为 false。
 * @details 待发送量超过 256 KiB 视为慢客户端并主动断开，保护 7×24 小时运行中的服务端内存和实时性。
 */
bool WebService::sendToClient(QWebSocket* client, const QJsonObject& msg) {
    if (!client || client->state() != QAbstractSocket::ConnectedState)
        return false;

    constexpr qint64 kCongestionThreshold = 256 * 1024;
    if (client->bytesToWrite() > kCongestionThreshold) {
        qWarning() << QStringLiteral("[WebService] WS 慢客户端断开: %1 (%2 bytes pending)")
                      .arg(client->peerAddress().toString())
                      .arg(client->bytesToWrite());
        client->close();
        m_wsClients.removeOne(client);
        return false;
    }

    const QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    return client->sendTextMessage(QString::fromUtf8(data)) >= 0;
}

/**
 * @brief 接纳新 WebSocket、登记断开回调并发出连接信号供显示桥单播快照。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::onWsConnection() {
    while (auto* ws = m_wsServer->nextPendingConnection()) {
        m_wsClients.append(ws);
        connect(ws, &QWebSocket::disconnected, this, &WebService::onWsDisconnected);
        emit webSocketConnected(ws);
        emit serverLog(QStringLiteral("info"),
            QStringLiteral("WS 客户端已连接 (%1:%2)")
                .arg(ws->peerAddress().toString())
                .arg(ws->peerPort()));
    }
}

/**
 * @brief 从客户端集合移除并延迟销毁断开的 WebSocket。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::onWsDisconnected() {
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (ws) {
        m_wsClients.removeOne(ws);
        emit serverLog(QStringLiteral("info"),
            QStringLiteral("WS 客户端已断开 (%1:%2)")
                .arg(ws->peerAddress().toString())
                .arg(ws->peerPort()));
        ws->deleteLater();
    }
}

// ============================================================
// HTTP — Connection Handling
// ============================================================
/**
 * @brief 判断 HTTP 套接字是否仍可接受响应写入。
 * @param sock 待检查连接。
 * @return 指针非空且状态为 ConnectedState 时为 true。
 */
bool WebService::isSocketWritable(QTcpSocket* sock) const
{
    return sock && sock->state() == QAbstractSocket::ConnectedState;
}

/**
 * @brief 将已序列化的 HTTP 数据排入连接发送缓冲区。
 * @param sock 目标 HTTP 连接。
 * @param data 完整或部分 HTTP 响应字节。
 * @return 套接字可写且 QTcpSocket::write() 未立即报错时为 true。
 */
bool WebService::writeToSocket(QTcpSocket* sock, const QByteArray& data)
{
    if (!isSocketWritable(sock))
        return false;
    return sock->write(data) >= 0;
}

/**
 * @brief 停止一个 HTTP 连接的空闲计时并从连接状态表移除。
 * @param sock 待关闭连接；未知或已清理的连接会被忽略。
 * @details 已连接套接字先 disconnectFromHost()，随后统一 deleteLater()，避免在 readyRead 回调栈中直接析构。
 */
void WebService::closeHttpSocket(QTcpSocket* sock)
{
    if (!sock || !m_buffers.contains(sock))
        return;
    if (auto* t = m_idleTimers.take(sock))
        t->stop();
    if (sock->state() == QAbstractSocket::ConnectedState) {
        sock->flush();
        sock->disconnectFromHost();
    } else {
        cleanupHttpSocket(sock);
    }
}

/**
 * @brief 删除 HTTP 连接缓冲和状态后安排 socket 销毁，允许重复清理。
 */
void WebService::cleanupHttpSocket(QTcpSocket* sock)
{
    if (!sock) return;
    if (!m_buffers.contains(sock) && !m_idleTimers.contains(sock))
        return;

    sock->disconnect(this);
    if (auto* t = m_idleTimers.take(sock))
        t->stop();
    m_buffers.remove(sock);
    sock->deleteLater();
}

/**
 * @brief 接纳全部待处理 TCP 连接，为每个连接建立独立请求缓冲和错误/断开回调。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::onHttpConnection() {
    while (auto* sock = m_httpServer->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, &WebService::onHttpReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &WebService::onHttpDisconnected);
        connect(sock, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
                this, &WebService::onHttpSocketError);
        m_buffers[sock] = QByteArray();

        auto* timer = new QTimer(sock);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, this, [this, sock]() {
            qDebug() << "[WebService] HTTP 空闲超时" << sock->peerAddress().toString();
            closeHttpSocket(sock);
        });
        timer->start(kIdleTimeoutMs);
        m_idleTimers[sock] = timer;
    }
}

/**
 * @brief 在客户端正常断开时回收该连接的解析状态。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::onHttpDisconnected() {
    cleanupHttpSocket(qobject_cast<QTcpSocket*>(sender()));
}

/**
 * @brief 记录 TCP 错误并回收连接，避免损坏缓冲继续参与解析。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::onHttpSocketError(QAbstractSocket::SocketError err)
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock || !m_buffers.contains(sock))
        return;
    if (err == QAbstractSocket::RemoteHostClosedError)
        return;
    closeHttpSocket(sock);
}

/**
 * @brief 追加新字节并循环解析流水线请求，按缓冲上限隔离不完整或恶意客户端。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void WebService::onHttpReadyRead() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    if (!m_buffers.contains(sock))
        return;

    if (auto* t = m_idleTimers.value(sock))
        t->start(kIdleTimeoutMs);

    m_buffers[sock].append(sock->readAll());
    QByteArray& buf = m_buffers[sock];

    if (buf.size() > kMaxBufferedBytes) {
        qWarning() << "[WebService] 缓冲区超限，断开客户端" << sock->peerAddress().toString();
        sendError(sock, 413, QStringLiteral("Payload Too Large"));
        closeHttpSocket(sock);
        return;
    }

    if (!buf.contains("\r\n\r\n") && buf.size() > kMaxHeaderBytes) {
        qWarning() << "[WebService] 请求头部超限，断开客户端" << sock->peerAddress().toString();
        sendError(sock, 400, QStringLiteral("Request Header Too Large"));
        closeHttpSocket(sock);
        return;
    }

    while (m_buffers.contains(sock)) {
        bool processed = dispatchRequest(sock, m_buffers[sock]);
        if (!processed) break;
    }
}

/// @return true if a request was fully processed; false if incomplete or errored.
/**
 * @brief 按方法先匹配精确路由、再匹配最长前缀路由，最后回退静态资源或 404。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
bool WebService::dispatchRequest(QTcpSocket* sock, QByteArray& raw) {
    auto result = tryParseRequest(raw);
    if (!result) {
        sendError(sock, 400, QStringLiteral("Bad Request"));
        closeHttpSocket(sock);
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
        if (!keepAlive)
            closeHttpSocket(sock);
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
        sendJson(sock, 200, m_getHandlers[cleanPath](body, req.queryString), keepAlive);
    } else if (req.method == Method::GET) {
        if (!prefixMatch(m_getPrefixHandlers, "GET"))
            serveStaticFile(sock, cleanPath, keepAlive);
    } else if (req.method == Method::POST && m_postHandlers.contains(cleanPath)) {
        auto res = m_postHandlers[cleanPath](body);
        sendJson(sock, res.value(QStringLiteral("ok")).toBool() ? 200 : 400, res, keepAlive);
    } else if (req.method == Method::POST) {
        if (!prefixMatch(m_postPrefixHandlers, "POST"))
            sendError(sock, 404, QStringLiteral("Not Found"), keepAlive);
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

    if (!m_buffers.contains(sock))
        return false;

    if (keepAlive) {
        m_buffers[sock].remove(0, static_cast<int>(result->consumed));
    } else {
        closeHttpSocket(sock);
    }
    return true;
}

// ============================================================
// Static Files
// ============================================================
/**
 * @brief 返回前端 index.html，为单页应用根路径和客户端路由提供入口。
 */
void WebService::serveIndex(QTcpSocket* sock, bool keepAlive) {
    QString idx = m_staticDir + QStringLiteral("index.html");
    if (QFileInfo::exists(idx)) {
        QFile f(idx);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray content = f.readAll();
            HttpResponse resp;
            resp.setContent(content, QStringLiteral("text/html; charset=utf-8"));
            if (keepAlive) resp.applyKeepAlive();
            writeToSocket(sock, serializeResponse(resp));
            return;
        }
    }
    QJsonObject obj;
    obj[QStringLiteral("ok")]    = false;
    obj[QStringLiteral("error")] = QStringLiteral(
        "前端静态文件未构建。请在 ui/ 目录执行: npm install && npm run build");
    sendJson(sock, 503, obj, false);
}

/**
 * @brief 规范化请求路径并确认目标仍位于静态根目录后读取文件，阻止目录穿越。
 */
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
    writeToSocket(sock, serializeResponse(resp));
}

// ============================================================
// Response Helpers
// ============================================================
/**
 * @brief 构造并发送 JSON HTTP 响应。
 * @param sock 目标连接。
 * @param status HTTP 状态码。
 * @param obj 响应对象，以紧凑 JSON 编码。
 * @param keepAlive 是否在响应头中声明保持连接。
 */
void WebService::sendJson(QTcpSocket* sock, int status, const QJsonObject& obj, bool keepAlive) {
    if (!isSocketWritable(sock))
        return;
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    HttpResponse resp;
    resp.statusCode = status;
    resp.setJson(body);
    if (keepAlive) resp.applyKeepAlive();
    writeToSocket(sock, serializeResponse(resp));
}

/**
 * @brief 以统一 `{ok:false,error:...}` 结构发送 HTTP 错误。
 * @param sock 目标连接。
 * @param status HTTP 错误状态码。
 * @param text 面向调用方的错误说明。
 * @param keepAlive 是否保持连接供后续请求复用。
 */
void WebService::sendError(QTcpSocket* sock, int status, const QString& text, bool keepAlive) {
    QJsonObject obj;
    obj[QStringLiteral("ok")]    = false;
    obj[QStringLiteral("error")] = text;
    sendJson(sock, status, obj, keepAlive);
}

} // namespace web
