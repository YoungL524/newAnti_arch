#include "HttpParser.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrlQuery>

namespace web {

// Lowercase header name for case-insensitive lookup
static QString normalizeHeaderKey(const QString& raw) {
    return raw.toLower();
}

// ============================================================
// HttpRequest helpers (case-insensitive header lookup)
// ============================================================
std::optional<QString> HttpRequest::headerValue(const QString& key) const {
    auto it = headers.find(key.toLower());
    if (it != headers.end())
        return it.value();
    return std::nullopt;
}

int HttpRequest::contentLength() const {
    auto v = headerValue("content-length");
    return v ? v->toInt() : 0;
}

bool HttpRequest::isKeepAlive() const {
    auto conn = headerValue("connection");
    return conn.has_value() && conn->compare("keep-alive", Qt::CaseInsensitive) == 0;
}

// ============================================================
// Response helpers
// ============================================================
void HttpResponse::setStatus(int code) noexcept { statusCode = code; }

void HttpResponse::setContent(const QByteArray& content, const QString& mimeType) {
    body = content;
    headers["content-type"]   = mimeType;
    headers["content-length"] = QString::number(body.size());
}

void HttpResponse::setJson(const QByteArray& jsonBody) {
    setContent(jsonBody, "application/json");
}

void HttpResponse::applyKeepAlive() {
    keepAlive = true;
    headers["connection"] = QStringLiteral("keep-alive");
    if (!headers.contains("keep-alive"))
        headers["keep-alive"] = QStringLiteral("timeout=5");
}

// ============================================================
// Parse
// ============================================================
static Method methodFromString(const QString& s) {
    if (s == "GET")  return Method::GET;
    if (s == "POST") return Method::POST;
    return Method::UNKNOWN;
}

enum class ChunkResult { Ok, Incomplete, Malformed };

/**
 * @brief Decode HTTP chunked body.
 * @param data   Raw buffer
 * @param offset Offset where chunked body starts
 * @param dataSize Total bytes available in data
 * @param outBody [out] Decoded body
 * @param endPos  [out] Position after the last byte consumed (complete/final trail)
 * @return Ok=body complete, Incomplete=wait for more, Malformed=bad data
 */
static ChunkResult decodeChunked(const QByteArray& data, int offset, int dataSize,
                                  QByteArray& outBody, int& endPos) {
    outBody.clear();
    endPos = offset;
    int pos = offset;

    while (pos < dataSize) {
        // chunk-size line ends with CRLF
        int lf = data.indexOf('\n', pos);
        if (lf < 0) return ChunkResult::Incomplete;
        if (lf > pos && data[lf - 1] != '\r') return ChunkResult::Malformed;

        QByteArray chunkLine = data.mid(pos, lf - pos - 1).trimmed();  // strip \r\n
        int semi = chunkLine.indexOf(';');
        if (semi >= 0) chunkLine = chunkLine.left(semi);

        bool ok = false;
        uint32_t chunkSize = chunkLine.toUInt(&ok, 16);
        if (!ok) return ChunkResult::Malformed;

        if (chunkSize == 0) {
            // Parse trailer section: zero or more headers, terminated by empty line
            int trailerPos = lf + 1;
            while (true) {
                // Need at least CRLF to terminate a trailer / empty line
                if (trailerPos + 1 >= dataSize) return ChunkResult::Incomplete;
                if (data[trailerPos] == '\r' && data[trailerPos + 1] == '\n') {
                    // Empty line = end of trailer
                    endPos = trailerPos + 2;
                    return ChunkResult::Ok;
                }
                // Non-empty trailer header — scan for its CRLF
                int tl = data.indexOf('\n', trailerPos);
                if (tl < 0) return ChunkResult::Incomplete;
                if (tl > trailerPos && data[tl - 1] != '\r') return ChunkResult::Malformed;
                trailerPos = tl + 1;
            }
        }

        // Validate CRLF after chunk data
        int chunkStart = lf + 1;
        if (chunkStart + static_cast<int>(chunkSize) + 2 > dataSize)
            return ChunkResult::Incomplete;
        if (data[chunkStart + static_cast<int>(chunkSize)]     != '\r'
         || data[chunkStart + static_cast<int>(chunkSize) + 1] != '\n')
            return ChunkResult::Malformed;

        outBody.append(data.mid(chunkStart, static_cast<int>(chunkSize)));
        pos = chunkStart + static_cast<int>(chunkSize) + 2;
    }
    return ChunkResult::Incomplete;
}

std::optional<ParseResult> tryParseRequest(const QByteArray& buffer) {
    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return ParseResult{{}, 0, false};

    int firstLf = buffer.indexOf("\r\n");
    if (firstLf < 0) return ParseResult{{}, 0, false};

    QByteArray reqLine = buffer.left(firstLf);
    auto parts = reqLine.split(' ');
    if (parts.size() < 2) return std::nullopt;

    QString fullPath = QString::fromUtf8(parts[1]);
    QString path, queryString;
    int qpos = fullPath.indexOf('?');
    if (qpos >= 0) { path = fullPath.left(qpos); queryString = fullPath.mid(qpos + 1); }
    else           { path = fullPath; }

    // Parse headers — normalize keys to lowercase
    QByteArray headerSection = buffer.mid(firstLf + 2, headerEnd - firstLf - 2);
    QMap<QString, QString> headers;
    for (const auto& line : headerSection.split('\n')) {
        auto trimmed = line.trimmed();
        int colon = trimmed.indexOf(':');
        if (colon > 0) {
            QString key = normalizeHeaderKey(QString::fromUtf8(trimmed.left(colon)).trimmed());
            QString val = QString::fromUtf8(trimmed.mid(colon + 1)).trimmed();
            headers.insert(std::move(key), std::move(val));
        }
    }

    // Detect body encoding
    int bodyStart = headerEnd + 4;
    QByteArray body;
    size_t consumed = 0;
    bool keepAlive = false;

    auto teIt = headers.find("transfer-encoding");
    bool isChunked = (teIt != headers.end() &&
                      teIt->compare("chunked", Qt::CaseInsensitive) == 0);

    if (isChunked) {
        int endPos = 0;
        ChunkResult cr = decodeChunked(buffer, bodyStart, buffer.size(), body, endPos);
        if (cr == ChunkResult::Incomplete)
            return ParseResult{{}, 0, false};
        if (cr == ChunkResult::Malformed)
            return std::nullopt;  // 400 Bad Request
        consumed = static_cast<size_t>(endPos);
    } else {
        auto clIt = headers.find("content-length");
        if (clIt != headers.end()) {
            bool ok = false;
            int bodyLen = clIt->toInt(&ok);
            if (!ok || bodyLen < 0)
                return std::nullopt;  // 400 Bad Request
            if (bodyStart + bodyLen > buffer.size())
                return ParseResult{{}, 0, false};
            body = buffer.mid(bodyStart, bodyLen);
            consumed = static_cast<size_t>(bodyStart + bodyLen);
        } else {
            // GET / HEAD / no body
            consumed = static_cast<size_t>(headerEnd + 4);
        }
    }

    // Keep-alive detection
    auto connIt = headers.find("connection");
    if (connIt != headers.end()) {
        keepAlive = (connIt->compare("keep-alive", Qt::CaseInsensitive) == 0);
    } else if (parts.size() >= 3) {
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
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 503: return "Service Unavailable";
    default:  return "Error";
    }
}

QByteArray serializeResponse(const HttpResponse& resp) {
    QByteArray result;
    result += "HTTP/1.1 " + QByteArray::number(resp.statusCode) + " "
            + statusText(resp.statusCode) + "\r\n";

    QMap<QString, QString> h = resp.headers;
    if (!h.contains("content-type"))
        h["content-type"] = resp.body.isEmpty() ? "text/plain" : "application/octet-stream";
    if (!h.contains("content-length"))
        h["content-length"] = QByteArray::number(resp.body.size());
    if (!h.contains("connection"))
        h["connection"] = "close";
    if (!h.contains("access-control-allow-origin"))
        h["access-control-allow-origin"] = "*";

    for (auto it = h.begin(); it != h.end(); ++it)
        result += it.key().toUtf8() + ": " + it.value().toUtf8() + "\r\n";
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
