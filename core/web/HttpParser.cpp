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

// ============================================================
// Parse
// ============================================================
static Method methodFromString(const QString& s) {
    if (s == "GET")  return Method::GET;
    if (s == "POST") return Method::POST;
    return Method::UNKNOWN;
}

std::optional<ParseResult> tryParseRequest(const QByteArray& buffer) {
    // Locate header terminator
    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return ParseResult{{}, 0};  // incomplete

    // Parse request line: "METHOD /path HTTP/1.1\r\n"
    int firstLf = buffer.indexOf("\r\n");
    if (firstLf < 0)
        return ParseResult{{}, 0};

    QByteArray reqLine = buffer.left(firstLf);
    auto parts = reqLine.split(' ');
    if (parts.size() < 2)
        return std::nullopt;  // malformed

    // Parse path + query string
    QByteArray pathBytes = parts[1];
    QString fullPath = QString::fromUtf8(pathBytes);
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

    // Body
    int bodyLen = 0;
    if (auto cl = headers.find("Content-Length"); cl != headers.end())
        bodyLen = cl->toInt();

    int bodyStart = headerEnd + 4;
    if (bodyStart + bodyLen > buffer.size())
        return ParseResult{{}, 0};  // body not yet complete

    QByteArray body = buffer.mid(bodyStart, bodyLen);

    HttpRequest req;
    req.method      = methodFromString(QString::fromUtf8(parts[0]));
    req.path        = path;
    req.queryString = queryString;
    req.headers     = std::move(headers);
    req.body        = std::move(body);

    return ParseResult{std::move(req), static_cast<size_t>(bodyStart + bodyLen)};
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

    // Add default headers if not set
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