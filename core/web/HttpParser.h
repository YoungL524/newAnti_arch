/**
 * @file HttpParser.h
 * @brief 定义无阻塞 HTTP/1.x 请求解析和响应序列化的数据契约。
 * @details 解析器只消费缓冲区开头的一条完整请求，支持 Content-Length 和分块正文，并对头部/正文设置上限；
 *          数据不完整时不消费字节，调用方继续接收。它不负责 socket、路由、鉴权或业务 JSON 校验。
 */

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
/**
 * @brief 服务当前支持的 HTTP 方法；其他方法解析为 UNKNOWN 并由调用方返回 405。
 */
enum class Method : uint8_t {
    UNKNOWN = 0, ///< 缺失、畸形或未支持的方法。
    GET,         ///< 读取资源。
    POST,        ///< 创建资源或触发命令。
    PUT,         ///< 替换/更新资源。
    DELETE,      ///< 删除资源。
};

// ============================================================
// Request / Response structures
// ============================================================
/**
 * @brief 已去除 HTTP 传输分帧的一条请求。
 */
struct HttpRequest {
    Method method = Method::UNKNOWN;   ///< 已识别的方法。
    QString path;                      ///< 不含查询串的 URL 路径。
    QString queryString;               ///< 不含问号的原始查询串。
    QMap<QString, QString> headers;    ///< 以小写键保存的请求头。
    QByteArray body;                   ///< 解码后的正文；长度受解析器上限约束。

    /**
 * @brief 大小写无关查询请求头；不存在时返回空 optional。
 */
    std::optional<QString> headerValue(const QString& key) const;
    /**
 * @brief 返回已解析 Content-Length；缺失或无效时为 0。
 */
    int contentLength() const;
    /**
 * @brief 根据 HTTP 版本默认值和 Connection 头判断连接复用意图。
 */
    bool isKeepAlive() const;
};

/**
 * @brief 路由处理器填写、由 serializeResponse 统一分帧的响应值。
 */
struct HttpResponse {
    int statusCode = 200;              ///< HTTP 数字状态码。
    QMap<QString, QString> headers;     ///< 业务响应头；序列化器补齐长度和连接头。
    QByteArray body;                    ///< 原样发送的响应正文。
    bool keepAlive = false;             ///< 是否在响应后保留 TCP 连接。

    /**
 * @brief 只设置状态码，不清理已有正文或响应头。
 */
    void setStatus(int code) noexcept;
    /**
 * @brief 同时设置正文和 Content-Type。
 */
    void setContent(const QByteArray& content, const QString& mimeType);
    /**
 * @brief 设置 JSON 正文及 UTF-8 application/json 类型。
 */
    void setJson(const QByteArray& jsonBody);
    void applyKeepAlive();  ///< 按 keepAlive 写入 Connection/Keep-Alive 响应头。
};

// ============================================================
// Parse / Serialize
// ============================================================
/**
 * @brief 流缓冲区开头一条完整请求的解析结果。
 */
struct ParseResult {
    HttpRequest request;    ///< 完整请求值。
    size_t consumed = 0;    ///< 应从连接缓冲区移除的原始字节数。
    bool keepAlive = false; ///< 本请求完成后是否保留连接。
};

/**
 * @brief 尝试解析缓冲区开头的一条有界请求。
 * @return 完整且合法时返回结果；数据不完整或畸形时返回空，调用方结合缓冲上限决定等待或关闭。
 */
std::optional<ParseResult> tryParseRequest(const QByteArray& buffer);
/**
 * @brief 生成状态行、响应头、空行和正文组成的 HTTP/1.1 字节串。
 */
QByteArray serializeResponse(const HttpResponse& resp);
/**
 * @brief 根据文件扩展名选择静态资源 Content-Type，未知类型返回 application/octet-stream。
 */
QString mimeType(const QString& filePath);

} // namespace web
