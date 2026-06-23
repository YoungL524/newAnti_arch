#include "EndpointApiRoutes.h"
#include "WebService.h"
#include "AppController.h"
#include <QJsonArray>

namespace EndpointApiRoutes {

void registerRoutes(web::WebService* ws, AppController* controller)
{
    if (!ws || !controller)
        return;

    ws->handleGet(QStringLiteral("/api/endpoints"), [controller](const QJsonObject&) {
        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        r[QStringLiteral("endpoints")] = controller->endpointsConfigJson()[QStringLiteral("endpoints")];
        return r;
    });

    ws->handlePut(QStringLiteral("/api/endpoints"), [controller](const QJsonObject& body) {
        if (!body.contains(QStringLiteral("endpoints")) || !body[QStringLiteral("endpoints")].isArray()) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("endpoints 必须是数组")},
            };
        }

        QJsonObject root;
        root[QStringLiteral("endpoints")] = body[QStringLiteral("endpoints")];

        QString error;
        if (!controller->updateEndpointsConfig(root, &error)) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), error.isEmpty() ? QStringLiteral("保存失败") : error},
            };
        }

        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("endpoints"), body[QStringLiteral("endpoints")]},
            {QStringLiteral("reloaded"), true},
        };
    });

    ws->handlePutPrefix(QStringLiteral("/api/endpoints/"), [controller](const QString& suffix, const QJsonObject& body) {
        if (suffix.isEmpty() || suffix.contains(QChar('/'))) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("无效端点 id")},
            };
        }

        QString error;
        if (!controller->updateEndpointConfig(suffix, body, &error)) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), error.isEmpty() ? QStringLiteral("保存失败") : error},
            };
        }

        QJsonObject endpointResp = body;
        endpointResp[QStringLiteral("id")] = suffix;
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("endpoint"), endpointResp},
            {QStringLiteral("reloaded"), true},
        };
    });
}

} // namespace EndpointApiRoutes
