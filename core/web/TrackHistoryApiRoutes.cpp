#include "TrackHistoryApiRoutes.h"
#include "WebService.h"
#include "DataStore.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <QVariant>

namespace TrackHistoryApiRoutes {

namespace {

constexpr int kDefaultTrackLimit = 1000;
constexpr int kMinTrackLimit = 1;
constexpr int kMaxTrackLimit = 5000;

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

int clampTrackLimit(const QString& raw)
{
    if (raw.isEmpty())
        return kDefaultTrackLimit;
    bool ok = false;
    const int n = raw.toInt(&ok);
    if (!ok)
        return kDefaultTrackLimit;
    return qBound(kMinTrackLimit, n, kMaxTrackLimit);
}

QJsonObject rowToJson(const QVariantMap& row)
{
    QJsonObject j;
    j[QStringLiteral("taskId")] = row.value(QStringLiteral("task_id")).toString();
    j[QStringLiteral("deviceId")] = row.value(QStringLiteral("device_id")).toString();
    j[QStringLiteral("tsMs")] = row.value(QStringLiteral("ts_ms")).toLongLong();
    j[QStringLiteral("sceneNum")] = row.value(QStringLiteral("scene_num")).toLongLong();
    j[QStringLiteral("targetId")] = row.value(QStringLiteral("target_id")).toLongLong();
    j[QStringLiteral("range")] = row.value(QStringLiteral("range_m")).toDouble();
    j[QStringLiteral("azimuth")] = row.value(QStringLiteral("azimuth_rad")).toDouble();
    j[QStringLiteral("elevation")] = row.value(QStringLiteral("elevation_rad")).toDouble();
    j[QStringLiteral("radialSpeed")] = row.value(QStringLiteral("radial_speed")).toDouble();
    j[QStringLiteral("altitude")] = row.value(QStringLiteral("altitude_m")).toDouble();
    j[QStringLiteral("x")] = row.value(QStringLiteral("x")).toDouble();
    j[QStringLiteral("y")] = row.value(QStringLiteral("y")).toDouble();
    j[QStringLiteral("z")] = row.value(QStringLiteral("z")).toDouble();
    return j;
}

} // namespace

void registerRoutes(web::WebService* ws, DataStore* store)
{
    if (!ws || !store)
        return;

    ws->handleGet(QStringLiteral("/api/tracks/history"), [store](const QJsonObject&, const QString& queryString) {
        if (!store->isConnected())
            return fail(QStringLiteral("数据库未连接"));

        QUrlQuery query(queryString);
        const QString taskId = query.queryItemValue(QStringLiteral("taskId"));
        if (taskId.isEmpty())
            return fail(QStringLiteral("taskId 不能为空"));

        const QString deviceId = query.queryItemValue(QStringLiteral("deviceId"));
        const OptionalMs startMs = parseOptionalMs(query.queryItemValue(QStringLiteral("startMs")));
        const OptionalMs endMs = parseOptionalMs(query.queryItemValue(QStringLiteral("endMs")));
        if (startMs.parseError)
            return fail(QStringLiteral("startMs 无效"));
        if (endMs.parseError)
            return fail(QStringLiteral("endMs 无效"));
        if (startMs.hasValue && endMs.hasValue && startMs.value > endMs.value)
            return fail(QStringLiteral("startMs 不能大于 endMs"));

        const int limit = clampTrackLimit(query.queryItemValue(QStringLiteral("limit")));
        const QString orderRaw = query.queryItemValue(QStringLiteral("order")).trimmed().toLower();
        if (!orderRaw.isEmpty()
            && orderRaw != QStringLiteral("asc")
            && orderRaw != QStringLiteral("desc")) {
            return fail(QStringLiteral("order 仅支持 asc 或 desc"));
        }
        const bool desc = (orderRaw == QStringLiteral("desc"));

        QString sql = QStringLiteral("SELECT * FROM tracks WHERE task_id = ?");
        QVariantList params;
        params.append(taskId);

        if (!deviceId.isEmpty()) {
            sql += QStringLiteral(" AND device_id = ?");
            params.append(deviceId);
        }
        if (startMs.hasValue) {
            sql += QStringLiteral(" AND ts_ms >= ?");
            params.append(startMs.value);
        }
        if (endMs.hasValue) {
            sql += QStringLiteral(" AND ts_ms <= ?");
            params.append(endMs.value);
        }

        sql += desc ? QStringLiteral(" ORDER BY ts_ms DESC LIMIT ?")
                    : QStringLiteral(" ORDER BY ts_ms ASC LIMIT ?");
        params.append(limit);

        QJsonArray arr;
        for (const auto& row : store->query(sql, params))
            arr.append(rowToJson(row));

        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        r[QStringLiteral("tracks")] = arr;
        return r;
    });
}

} // namespace TrackHistoryApiRoutes
