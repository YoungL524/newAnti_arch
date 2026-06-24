#include "SystemEventApiRoutes.h"
#include "WebService.h"
#include "DataStore.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <QVariant>

namespace SystemEventApiRoutes {

namespace {

QJsonObject rowToJson(const QVariantMap& row)
{
    QJsonObject j;
    j[QStringLiteral("tsMs")] = row.value(QStringLiteral("ts_ms")).toLongLong();
    j[QStringLiteral("category")] = row.value(QStringLiteral("category")).toString();
    j[QStringLiteral("source")] = row.value(QStringLiteral("source")).toString();
    j[QStringLiteral("detail")] = row.value(QStringLiteral("detail")).toString();
    const QString taskId = row.value(QStringLiteral("task_id")).toString();
    const QString deviceId = row.value(QStringLiteral("device_id")).toString();
    if (!taskId.isEmpty())
        j[QStringLiteral("taskId")] = taskId;
    if (!deviceId.isEmpty())
        j[QStringLiteral("deviceId")] = deviceId;
    return j;
}

} // namespace

void registerRoutes(web::WebService* ws, DataStore* store)
{
    if (!ws || !store)
        return;

    ws->handleGet(QStringLiteral("/api/system-events"), [store](const QJsonObject&, const QString& queryString) {
        if (!store->isConnected()) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("数据库未连接")}};
        }

        QUrlQuery query(queryString);
        const QString taskId = query.queryItemValue(QStringLiteral("taskId"));
        const QString deviceId = query.queryItemValue(QStringLiteral("deviceId"));

        QString sql = QStringLiteral("SELECT * FROM system_events WHERE 1=1");
        QVariantList params;
        if (!taskId.isEmpty()) {
            sql += QStringLiteral(" AND task_id = ?");
            params.append(taskId);
        }
        if (!deviceId.isEmpty()) {
            sql += QStringLiteral(" AND device_id = ?");
            params.append(deviceId);
        }
        sql += QStringLiteral(" ORDER BY ts_ms DESC LIMIT 500");

        QJsonArray arr;
        for (const auto& row : store->query(sql, params))
            arr.append(rowToJson(row));

        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        r[QStringLiteral("events")] = arr;
        return r;
    });
}

} // namespace SystemEventApiRoutes
