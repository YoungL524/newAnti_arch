/**
 * @file LocalWebAccess.h
 * @brief 为 Core HTTP 和 WebSocket 共用本机浏览器来源门禁。
 * @details 上游为代理保留的 Host/Origin，下游仅返回是否允许访问；不拥有业务状态、不执行命令。
 * 从 localAuthority/localOrigin 阅读。信任本机 HTTP 页面和无 Origin 的原生 HTTP 客户端，
 * 不把回环监听当作身份认证；远程浏览器来源、DNS 重绑定 Host 与 null 来源均拒绝。
 */
#pragma once

#include <QByteArray>
#include <QUrl>

namespace core::web {

/** @brief 只接受 localhost 或 127.0.0.1 的规范 authority，保留开发、预览与部署的实际端口。 */
inline bool localAuthority(const QByteArray &authority)
{
    const QUrl url(QStringLiteral("http://") + QString::fromLatin1(authority), QUrl::StrictMode);
    if (!url.isValid() || !url.userInfo().isEmpty() || !url.path().isEmpty()
        || url.hasQuery() || url.hasFragment()
        || (url.host() != QStringLiteral("localhost") && url.host() != QStringLiteral("127.0.0.1"))) return false;
    const QByteArray expected = url.host().toLatin1()
        + (url.port() < 0 ? QByteArray() : ':' + QByteArray::number(url.port()));
    return authority == expected && url.port(80) > 0;
}

/** @brief Origin 必须是无路径的本机 HTTP 来源；空值只由 HTTP 原生客户端兼容分支允许。 */
inline bool localOrigin(const QByteArray &origin)
{
    return origin.startsWith("http://") && localAuthority(origin.mid(7));
}

} // namespace core::web
