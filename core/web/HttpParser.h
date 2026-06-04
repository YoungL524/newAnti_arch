#pragma once

#include <QByteArray>
#include <QString>
#include <QMap>
#include <optional>
#include <functional>

namespace web {

// ============================================================
// HTTP method
// ============================================================
enum class Method : uint8_t {
    UNKNOWN = 0,
    GET,
    POST,
};

// ============================================================
// Request / Response structures
// ============================================================
struct HttpRequest {
    Method                 method  = Method::UNKNOWN;
    QString                path;
    QString                queryString;
    QMap<QString, QString> headers;
    QByteArray             body;

    std::optional<QString> headerValue(const QString& key) const;
    int contentLength() const;
    bool isKeepAlive() const;
};

struct HttpResponse {
    int                    statusCode = 200;
    QMap<QString, QString> headers;
    QByteArray             body;
    bool                   keepAlive = false;  ///< if true, serialized with Connection: keep-alive + Keep-Alive: timeout=5

    void setStatus(int code) noexcept;
    void setContent(const QByteArray& content, const QString& mimeType);
    void setJson(const QByteArray& jsonBody);
    void applyKeepAlive();  ///< set Connection and Keep-Alive headers
};

// ============================================================
// Parse / Serialize
// ============================================================
struct ParseResult {
    HttpRequest request;
    size_t      consumed;   ///< 0 = incomplete; non-zero = bytes consumed from buffer
    bool        keepAlive = false;  ///< true if Connection is keep-alive
};

std::optional<ParseResult> tryParseRequest(const QByteArray& buffer);
QByteArray serializeResponse(const HttpResponse& resp);
QString mimeType(const QString& filePath);

} // namespace web