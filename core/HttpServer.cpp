#include "HttpServer.h"
#include "bus/SystemBus.h"
#include "bus/GenericPackets.h"
#include "protocol/StructureReceive.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <QRegularExpression>

// ============================================================================
// Construction / Destruction
// ============================================================================
HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
{
}

HttpServer::~HttpServer()
{
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================
bool HttpServer::start(quint16 httpPort, quint16 wsPort, const QString &staticDir)
{
    m_httpPort = httpPort;
    m_wsPort = wsPort;

    // ---- HTTP server ----
    m_httpServer = new QTcpServer(this);
    connect(m_httpServer, &QTcpServer::newConnection,
            this, &HttpServer::onNewHttpConnection);

    if (!m_httpServer->listen(QHostAddress::LocalHost, httpPort)) {
        emit serverLog(QStringLiteral("error"),
                       QStringLiteral("HTTP 监听失败: %1").arg(m_httpServer->errorString()));
        return false;
    }

    // ---- WebSocket server ----
    m_wsServer = new QWebSocketServer(QStringLiteral("CoreWS"),
                                       QWebSocketServer::NonSecureMode, this);
    connect(m_wsServer, &QWebSocketServer::newConnection,
            this, &HttpServer::onNewWsConnection);

    if (!m_wsServer->listen(QHostAddress::LocalHost, wsPort)) {
        emit serverLog(QStringLiteral("error"),
                       QStringLiteral("WebSocket 监听失败: %1").arg(m_wsServer->errorString()));
        stop();
        return false;
    }

    // ---- Static dir ----
    if (staticDir.isEmpty()) {
        // 默认相对于可执行文件路径: 可执行文件所在目录/../ui/dist/
        m_staticDir = QCoreApplication::applicationDirPath()
                      + QStringLiteral("/../../ui/dist/");
    } else {
        m_staticDir = staticDir;
    }
    m_staticDir = QDir(m_staticDir).absolutePath() + QStringLiteral("/");

    emit serverLog(QStringLiteral("info"),
                   QStringLiteral("HTTP  : %1:%2").arg(QStringLiteral("127.0.0.1")).arg(httpPort));
    emit serverLog(QStringLiteral("info"),
                   QStringLiteral("WS    : %1:%2").arg(QStringLiteral("127.0.0.1")).arg(wsPort));
    emit serverLog(QStringLiteral("info"),
                   QStringLiteral("Static: %1").arg(m_staticDir));
    return true;
}

void HttpServer::stop()
{
    for (auto *ws : m_wsClients) {
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

    // 清理未完成的 HTTP 连接
    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_buffers.clear();
}

// ============================================================================
// Handler Registration
// ============================================================================
void HttpServer::handleGet(const QString &path, JsonHandler handler)
{
    m_getHandlers[path] = std::move(handler);
}

void HttpServer::handlePost(const QString &path, JsonHandler handler)
{
    m_postHandlers[path] = std::move(handler);
}

// ============================================================================
// WebSocket Broadcast
// ============================================================================
void HttpServer::broadcast(const QJsonObject &msg)
{
    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    for (auto *ws : m_wsClients) {
        ws->sendTextMessage(QString::fromUtf8(data));
    }
}

// ============================================================================
// HTTP — New Connection
// ============================================================================
void HttpServer::onNewHttpConnection()
{
    while (auto *sock = m_httpServer->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, &HttpServer::onHttpReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &HttpServer::onHttpDisconnected);
        m_buffers[sock] = QByteArray();
    }
}

void HttpServer::onHttpDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (sock) {
        m_buffers.remove(sock);
        sock->deleteLater();
    }
}

// ============================================================================
// HTTP — Request Parsing
// ============================================================================
void HttpServer::onHttpReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;

    m_buffers[sock].append(sock->readAll());

    // 检查是否收到完整请求头 (找到 \r\n\r\n)
    QByteArray &buf = m_buffers[sock];
    int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;  // 等待更多数据

    // 解析请求行: METHOD /path HTTP/1.1
    int firstLine = buf.indexOf("\r\n");
    if (firstLine < 0) return;
    QByteArray reqLine = buf.left(firstLine);
    QList<QByteArray> parts = reqLine.split(' ');
    if (parts.size() < 2) {
        sendError(sock, 400, QStringLiteral("Bad Request"));
        sock->disconnectFromHost();
        return;
    }

    QString method = QString::fromUtf8(parts[0]).toUpper();
    QString path   = QString::fromUtf8(parts[1]);

    // 解析 Content-Length
    int contentLength = 0;
    QRegularExpression clRe(
        QStringLiteral("Content-Length:\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto clMatch = clRe.match(QString::fromUtf8(buf.left(headerEnd)));
    if (clMatch.hasMatch())
        contentLength = clMatch.captured(1).toInt();

    int bodyStart = headerEnd + 4;  // 跳过 \r\n\r\n
    int totalBody = buf.size() - bodyStart;

    // 如果 body 还没收完，等待
    if (totalBody < contentLength) return;

    QByteArray body = buf.mid(bodyStart, contentLength);

    // 请求已完整接收，可以安心移除此连接的缓冲区了
    m_buffers.remove(sock);

    // 解析 query string 并提取纯净路径
    int qpos = path.indexOf(QChar('?'));
    QString cleanPath = (qpos >= 0) ? path.left(qpos) : path;

    // 路由
    if (method == QStringLiteral("GET") && cleanPath == QStringLiteral("/")) {
        serveIndex(sock);
    } else if (method == QStringLiteral("GET") && m_getHandlers.contains(cleanPath)) {
        QJsonObject params;  // 可从 query string 解析
        QJsonObject result = m_getHandlers[cleanPath](params);
        sendJson(sock, 200, result);
    } else if (method == QStringLiteral("POST") && m_postHandlers.contains(cleanPath)) {
        QJsonObject params;
        if (!body.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject()) params = doc.object();
        }
        QJsonObject result = m_postHandlers[cleanPath](params);
        int status = result.value(QStringLiteral("ok")).toBool() ? 200 : 400;
        sendJson(sock, status, result);
    } else if (method == QStringLiteral("GET")) {
        serveStaticFile(sock, cleanPath);
    } else {
        sendError(sock, 404, QStringLiteral("Not Found"));
    }

    sock->disconnectFromHost();
}

// ============================================================================
// HTTP — Static Files
// ============================================================================
void HttpServer::serveIndex(QTcpSocket *sock)
{
    // 先尝试 index.html
    QString idx = m_staticDir + QStringLiteral("index.html");
    if (QFileInfo::exists(idx)) {
        QFile f(idx);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray content = f.readAll();
            QByteArray header = QStringLiteral(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %1\r\n"
                "Connection: close\r\n"
                "Cache-Control: no-cache\r\n"
                "\r\n").arg(content.size()).toUtf8();
            sock->write(header);
            sock->write(content);
            return;
        }
    }

    // 没有 index.html → 返回 503 和指引
    QJsonObject obj;
    obj[QStringLiteral("ok")] = false;
    obj[QStringLiteral("error")] = QStringLiteral(
        "前端静态文件未构建。请在 ui/ 目录执行: npm install && npm run build");
    sendJson(sock, 503, obj);
}

void HttpServer::serveStaticFile(QTcpSocket *sock, const QString &path)
{
    // 安全: 拒绝包含 .. 的路径
    if (path.contains(QStringLiteral(".."))) {
        sendError(sock, 403, QStringLiteral("Forbidden"));
        return;
    }

    QString filePath = m_staticDir + path.mid(1);  // 去掉开头的 /
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
    QString mt = mimeType(filePath);
    QByteArray header = QStringLiteral(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %1\r\n"
        "Content-Length: %2\r\n"
        "Connection: close\r\n"
        "\r\n").arg(mt).arg(content.size()).toUtf8();
    sock->write(header);
    sock->write(content);
}

// ============================================================================
// HTTP — Response Helpers
// ============================================================================
void HttpServer::sendJson(QTcpSocket *sock, int status, const QJsonObject &obj)
{
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QString statusText = (status == 200) ? QStringLiteral("OK")
                        : (status == 400) ? QStringLiteral("Bad Request")
                        : (status == 404) ? QStringLiteral("Not Found")
                        : (status == 503) ? QStringLiteral("Service Unavailable")
                        : QStringLiteral("Error");

    QByteArray header = QStringLiteral(
        "HTTP/1.1 %1 %2\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %3\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n").arg(status).arg(statusText).arg(body.size()).toUtf8();
    sock->write(header);
    sock->write(body);
}

void HttpServer::sendError(QTcpSocket *sock, int status, const QString &text)
{
    QJsonObject obj;
    obj[QStringLiteral("ok")] = false;
    obj[QStringLiteral("error")] = text;
    sendJson(sock, status, obj);
}

// ============================================================================
// WebSocket — Connection Management
// ============================================================================
void HttpServer::onNewWsConnection()
{
    while (auto *ws = m_wsServer->nextPendingConnection()) {
        m_wsClients.append(ws);
        connect(ws, &QWebSocket::disconnected, this, &HttpServer::onWsDisconnected);
        emit serverLog(QStringLiteral("info"),
                       QStringLiteral("WS 客户端已连接 (%1)").arg(ws->peerAddress().toString()));
    }
}

void HttpServer::onWsDisconnected()
{
    auto *ws = qobject_cast<QWebSocket *>(sender());
    if (ws) {
        m_wsClients.removeOne(ws);
        ws->deleteLater();
    }
}

// ============================================================================
// MIME Types
// ============================================================================
QString HttpServer::mimeType(const QString &path)
{
    if (path.endsWith(QStringLiteral(".html")))  return QStringLiteral("text/html; charset=utf-8");
    if (path.endsWith(QStringLiteral(".css")))   return QStringLiteral("text/css; charset=utf-8");
    if (path.endsWith(QStringLiteral(".js")))    return QStringLiteral("application/javascript");
    if (path.endsWith(QStringLiteral(".json")))  return QStringLiteral("application/json");
    if (path.endsWith(QStringLiteral(".png")))   return QStringLiteral("image/png");
    if (path.endsWith(QStringLiteral(".svg")))   return QStringLiteral("image/svg+xml");
    if (path.endsWith(QStringLiteral(".ico")))   return QStringLiteral("image/x-icon");
    if (path.endsWith(QStringLiteral(".woff2"))) return QStringLiteral("font/woff2");
    if (path.endsWith(QStringLiteral(".woff")))  return QStringLiteral("font/woff");
    if (path.endsWith(QStringLiteral(".ttf")))   return QStringLiteral("font/ttf");
    return QStringLiteral("application/octet-stream");
}

// ============================================================================
// SystemBus Subscription — 将航迹数据推送到 WebSocket
// ============================================================================
void HttpServer::subscribeToBus()
{
    // 订阅所有 UI 方向的数据包 (dispatchTarget = UI_OUTPUTWIDGET)
    // RadarDataProcessor 发布航迹时使用此 target
    SystemBus::getInstance().subscribeByTarget(
        UI_OUTPUTWIDGET,
        [this](const DataPacket* pkt) {
            if (!pkt || pkt->payloadType != PAYLOAD_TYPE_GENERAL_PACKAGE) return;
            if (pkt->payloadSize < sizeof(GeneralPackage)) return;

            // 从 DataPacket 载荷中解包 GeneralPackage
            GeneralPackage gp;
            memcpy(&gp, pkt->payload.rawData, sizeof(GeneralPackage));
            const TargetType& tgt = gp.h_c.TrackInfo;

            QJsonObject msg;
            msg[QStringLiteral("type")] = QStringLiteral("tracks");
            msg[QStringLiteral("deviceType")] = static_cast<int>(pkt->sourceType);

            QJsonObject track;
            track[QStringLiteral("targetId")]     = static_cast<int>(tgt.TargetID);
            track[QStringLiteral("range")]        = tgt.Range;
            track[QStringLiteral("azimuth")]      = tgt.Azimuth;
            track[QStringLiteral("elevation")]    = tgt.Elevation;
            track[QStringLiteral("radialSpeed")]  = tgt.RadialSpeed;
            track[QStringLiteral("rcs")]          = tgt.RCS;
            track[QStringLiteral("altitude")]     = tgt.Altitude;
            track[QStringLiteral("x")]            = tgt.x;
            track[QStringLiteral("y")]            = tgt.y;
            track[QStringLiteral("z")]            = tgt.z;
            track[QStringLiteral("vx")]           = tgt.vx;
            track[QStringLiteral("vy")]           = tgt.vy;
            track[QStringLiteral("vz")]           = tgt.vz;
            track[QStringLiteral("timestamp")]    = static_cast<double>(pkt->timeStamp);

            msg[QStringLiteral("track")] = track;

            // bus 回调在 worker 线程，broadcast 操作 QWebSocket 必须回主线程
            QMetaObject::invokeMethod(this, [this, msg]() {
                broadcast(msg);
            }, Qt::QueuedConnection);
        }
    );

    qDebug() << QStringLiteral("[HttpServer] 已订阅 SystemBus (target=UI_OUTPUTWIDGET)，航迹将推送到 WebSocket");
}
