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

struct OptionalMs {
    bool hasValue = false;
    bool parseError = false;
    qint64 value = 0;
};

QJsonObject fail(const QString& msg)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}};
}

OptionalMs parseOptionalMs(const QString& raw)
{
    if (raw.isEmpty())
        return {};
    bool ok = false;
    const qint64 v = raw.toLongLong(&ok);
    if (!ok)
        return {false, true, 0};
    return {true, false, v};
}

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
        const OptionalMs startMs = parseOptionalMs(query.queryItemValue(QStringLiteral("startMs")));
        const OptionalMs endMs = parseOptionalMs(query.queryItemValue(QStringLiteral("endMs")));
        if (startMs.parseError)
            return fail(QStringLiteral("startMs 无效"));
        if (endMs.parseError)
            return fail(QStringLiteral("endMs 无效"));
        if (startMs.hasValue && endMs.hasValue && startMs.value > endMs.value)
            return fail(QStringLiteral("startMs 不能大于 endMs"));

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
        if (startMs.hasValue) {
            sql += QStringLiteral(" AND ts_ms >= ?");
            params.append(startMs.value);
        }
        if (endMs.hasValue) {
            sql += QStringLiteral(" AND ts_ms <= ?");
            params.append(endMs.value);
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
