#include "HttpParser.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrlQuery>

namespace web {

// ============================================================
// HttpRequest helpers
// ============================================================
std::optional<QString> HttpRequest::headerValue(const QString& key) const {
    auto it = headers.find(key);
    if (it != headers.end())
        return it.value();
    return std::nullopt;
}

int HttpRequest::contentLength() const {
    auto v = headerValue("Content-Length");
    return v ? v->toInt() : 0;
}

bool HttpRequest::isKeepAlive() const {
    auto conn = headerValue("Connection");
    if (!conn) return false;
    return conn->compare("keep-alive", Qt::CaseInsensitive) == 0
        || conn->compare("Keep-Alive") == 0;
}

// ============================================================
// Response helpers
// ============================================================
void HttpResponse::setStatus(int code) noexcept {
    statusCode = code;
}

void HttpResponse::setContent(const QByteArray& content, const QString& mimeType) {
    body = content;
    headers["Content-Type"]   = mimeType;
    headers["Content-Length"] = QString::number(body.size());
}

void HttpResponse::setJson(const QByteArray& jsonBody) {
    setContent(jsonBody, "application/json");
}

void HttpResponse::applyKeepAlive() {
    keepAlive = true;
    headers["Connection"] = QStringLiteral("keep-alive");
    if (!headers.contains("Keep-Alive"))
        headers["Keep-Alive"] = QStringLiteral("timeout=5");
}

// ============================================================
// Parse
// ============================================================
static Method methodFromString(const QString& s) {
    if (s == "GET")  return Method::GET;
    if (s == "POST") return Method::POST;
    return Method::UNKNOWN;
}

/**
 * @brief Decode HTTP chunked body starting at `data[offset]`.
 * @return Decoded body, or empty QByteArray if incomplete.
 */
static QByteArray decodeChunked(const QByteArray& data, int offset, int dataSize, bool& complete) {
    complete = false;
    QByteArray body;
    int pos = offset;

    while (pos < dataSize) {
        // Find chunk-size line ending
        int lf = data.indexOf('\n', pos);
        if (lf < 0) return {};

        QByteArray chunkLine = data.mid(pos, lf - pos).trimmed();
        // Split off optional chunk-extensions (semicolon)
        int semi = chunkLine.indexOf(';');
        if (semi >= 0) chunkLine = chunkLine.left(semi);

        bool ok = false;
        uint32_t chunkSize = chunkLine.toUInt(&ok, 16);
        if (!ok) return {};  // malformed

        if (chunkSize == 0) {
            complete = true;
            // Skip trailing CRLF after last-chunk (and optional trailer)
            return body;
        }

        int chunkStart = lf + 1;
        if (chunkStart + static_cast<int>(chunkSize) + 2 > dataSize)
            return {};  // chunk body + CRLF not yet complete

        body.append(data.mid(chunkStart, static_cast<int>(chunkSize)));
        pos = chunkStart + static_cast<int>(chunkSize) + 2;  // skip body + CRLF
    }

    return {};
}

std::optional<ParseResult> tryParseRequest(const QByteArray& buffer) {
    // Locate header terminator
    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return ParseResult{{}, 0, false};  // incomplete

    // Parse request line: "METHOD /path HTTP/1.1\r\n"
    int firstLf = buffer.indexOf("\r\n");
    if (firstLf < 0)
        return ParseResult{{}, 0, false};

    QByteArray reqLine = buffer.left(firstLf);
    auto parts = reqLine.split(' ');
    if (parts.size() < 2)
        return std::nullopt;  // malformed

    // Path + query string
    QString fullPath = QString::fromUtf8(parts[1]);
    QString path, queryString;
    int qpos = fullPath.indexOf('?');
    if (qpos >= 0) {
        path        = fullPath.left(qpos);
        queryString = fullPath.mid(qpos + 1);
    } else {
        path = fullPath;
    }

    // Parse headers
    QByteArray headerSection = buffer.mid(firstLf + 2, headerEnd - firstLf - 2);
    QMap<QString, QString> headers;
    for (const auto& line : headerSection.split('\n')) {
        auto trimmed = line.trimmed();
        int colon = trimmed.indexOf(':');
        if (colon > 0) {
            QString key = QString::fromUtf8(trimmed.left(colon)).trimmed();
            QString val = QString::fromUtf8(trimmed.mid(colon + 1)).trimmed();
            headers.insert(std::move(key), std::move(val));
        }
    }

    // Detect body encoding
    int bodyStart = headerEnd + 4;
    QByteArray body;

    auto teIt = headers.find("Transfer-Encoding");
    bool isChunked = (teIt != headers.end() &&
                      teIt->compare("chunked", Qt::CaseInsensitive) == 0);
    auto clIt = headers.find("Content-Length");

    size_t consumed = 0;

    if (isChunked) {
        bool complete = false;
        body = decodeChunked(buffer, bodyStart, buffer.size(), complete);
        if (!complete)
            return ParseResult{{}, 0, false};  // chunked body not yet complete
        consumed = static_cast<size_t>(bodyStart + (buffer.size() - bodyStart));
    } else if (clIt != headers.end()) {
        int bodyLen = clIt->toInt();
        if (bodyStart + bodyLen > buffer.size())
            return ParseResult{{}, 0, false};  // body not yet complete
        body = buffer.mid(bodyStart, bodyLen);
        consumed = static_cast<size_t>(bodyStart + bodyLen);
    } else {
        // No body (GET, HEAD, etc.)
        consumed = static_cast<size_t>(headerEnd + 4);
    }

    // Detect keep-alive
    bool keepAlive = false;
    auto connIt = headers.find("Connection");
    if (connIt != headers.end()) {
        keepAlive = connIt->compare("keep-alive", Qt::CaseInsensitive) == 0;
    }
    // HTTP/1.1 defaults to keep-alive unless Connection: close
    else if (parts.size() >= 3) {
        QString version = QString::fromUtf8(parts[2]);
        keepAlive = version.contains("HTTP/1.1");
    }

    HttpRequest req;
    req.method      = methodFromString(QString::fromUtf8(parts[0]));
    req.path        = path;
    req.queryString = queryString;
    req.headers     = std::move(headers);
    req.body        = std::move(body);

    return ParseResult{std::move(req), consumed, keepAlive};
}

// ============================================================
// Serialize
// ============================================================
static QByteArray statusText(int code) {
    switch (code) {
    case 200: return QByteArrayLiteral("OK");
    case 400: return QByteArrayLiteral("Bad Request");
    case 404: return QByteArrayLiteral("Not Found");
    case 503: return QByteArrayLiteral("Service Unavailable");
    default:  return QByteArrayLiteral("Error");
    }
}

QByteArray serializeResponse(const HttpResponse& resp) {
    QByteArray result;
    result += "HTTP/1.1 " + QByteArray::number(resp.statusCode) + " "
            + statusText(resp.statusCode) + "\r\n";

    QMap<QString, QString> h = resp.headers;
    if (!h.contains("Content-Type"))
        h["Content-Type"] = resp.body.isEmpty() ? "text/plain" : "application/octet-stream";
    if (!h.contains("Content-Length"))
        h["Content-Length"] = QByteArray::number(resp.body.size());
    if (!h.contains("Connection"))
        h["Connection"] = "close";
    if (!h.contains("Access-Control-Allow-Origin"))
        h["Access-Control-Allow-Origin"] = "*";

    for (auto it = h.begin(); it != h.end(); ++it) {
        result += it.key().toUtf8() + ": " + it.value().toUtf8() + "\r\n";
    }
    result += "\r\n";
    result += resp.body;
    return result;
}

// ============================================================
// MIME
// ============================================================
QString mimeType(const QString& filePath) {
    if (filePath.endsWith(".html"))  return "text/html; charset=utf-8";
    if (filePath.endsWith(".css"))   return "text/css; charset=utf-8";
    if (filePath.endsWith(".js"))    return "application/javascript";
    if (filePath.endsWith(".json"))  return "application/json";
    if (filePath.endsWith(".png"))   return "image/png";
    if (filePath.endsWith(".svg"))   return "image/svg+xml";
    if (filePath.endsWith(".ico"))   return "image/x-icon";
    if (filePath.endsWith(".woff2")) return "font/woff2";
    if (filePath.endsWith(".woff"))  return "font/woff";
    if (filePath.endsWith(".ttf"))   return "font/ttf";
    return "application/octet-stream";
}

} // namespace web
