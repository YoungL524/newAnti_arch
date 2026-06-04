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
};

struct HttpResponse {
    int                    statusCode = 200;
    QMap<QString, QString> headers;
    QByteArray             body;

    void setStatus(int code) noexcept;
    void setContent(const QByteArray& content, const QString& mimeType);
    void setJson(const QByteArray& jsonBody);
};

// ============================================================
// Parse / Serialize
// ============================================================
struct ParseResult {
    HttpRequest request;
    size_t      consumed;  ///< 0 = incomplete request
};

std::optional<ParseResult> tryParseRequest(const QByteArray& buffer);
QByteArray serializeResponse(const HttpResponse& resp);
QString mimeType(const QString& filePath);

} // namespace web