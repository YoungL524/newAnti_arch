#include "SystemEventApiRoutes.h"
#include "WebService.h"
#include "DataStore.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <QVariant>

namespace SystemEventApiRoutes {

namespace {

constexpr int kDefaultEventLimit = 500;
constexpr int kMinEventLimit = 1;
constexpr int kMaxEventLimit = 1000;

int clampEventLimit(const QString& raw)
{
    if (raw.isEmpty())
        return kDefaultEventLimit;
    bool ok = false;
    const int n = raw.toInt(&ok);
    if (!ok)
        return kDefaultEventLimit;
    return qBound(kMinEventLimit, n, kMaxEventLimit);
}

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
        const QString category = query.queryItemValue(QStringLiteral("category"));
        const int limit = clampEventLimit(query.queryItemValue(QStringLiteral("limit")));

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
        if (!category.isEmpty()) {
            sql += QStringLiteral(" AND category = ?");
            params.append(category);
        }
        sql += QStringLiteral(" ORDER BY ts_ms DESC LIMIT ?");
        params.append(limit);

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
