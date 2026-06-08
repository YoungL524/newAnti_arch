#include "WebApiController.h"
#include "WebService.h"
#include "bus/SystemBus.h"
#include "bus/GenericPackets.h"
#include "protocol/StructureReceive.h"
#include "RadarWorkModeController.h"
#include "business/DeviceRegistry.h"
#include "log/Logger.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <QTimer>

WebApiController::WebApiController(web::WebService* ws,
                                   RadarWorkModeController* wmc,
                                   DeviceRegistry* deviceRegistry,
                                   HealthProvider healthProvider,
                                   QObject* parent)
    : QObject(parent)
    , m_ws(ws)
    , m_wmc(wmc)
    , m_deviceReg(deviceRegistry)
    , m_health(std::move(healthProvider))
{
}

WebApiController::~WebApiController() { shutdown(); }

void WebApiController::registerRestRoutes()
{
    // GET /api/status
    m_ws->handleGet(QStringLiteral("/api/status"), [this](const QJsonObject&) {
        QJsonObject s;
        auto health = m_health();
        s[QStringLiteral("ok")] = true;
        s[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        s[QStringLiteral("uptime")] = QDateTime::currentSecsSinceEpoch();
        s[QStringLiteral("endpoints")] = health.comm.openEndpoints;
        s[QStringLiteral("dbConnected")] = health.database.connected;
        s[QStringLiteral("devices")] = buildDeviceStatusArray();
        auto st2 = m_wmc ? m_wmc->state() : RadarWorkModeController::State::Standby;
        s[QStringLiteral("acquisition")] = (st2 == RadarWorkModeController::State::Starting
                                         || st2 == RadarWorkModeController::State::Working);
        s[QStringLiteral("state")] = m_wmc ? RadarWorkModeController::stateName(m_wmc->state()) : QStringLiteral("Unknown");
        return s;
    });

    // GET /api/logs
    m_ws->handleGet(QStringLiteral("/api/logs"), [](const QJsonObject&) {
        QJsonObject r; r[QStringLiteral("ok")] = true;
        QJsonArray arr;
        for (const auto& p : Logger::instance().recentLogs(100)) {
            QJsonObject item;
            item[QStringLiteral("level")] = p.first;
            item[QStringLiteral("message")] = p.second;
            item[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
            arr.append(item);
        }
        r[QStringLiteral("logs")] = arr;
        return r;
    });

    if (m_wmc) {
        m_ws->handlePost(QStringLiteral("/api/start"), [this](const QJsonObject& body) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastStartMs < 500)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("throttled"), true}};
            m_lastStartMs = now;
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            QJsonArray accepted;
            if (ids.isEmpty()) { accepted.append(QStringLiteral("radar_cpu")); }
            else { auto c = radarCpuDeviceIds(); for (const auto& v : ids) if (c.contains(v.toString())) accepted.append(v.toString()); }
            QJsonArray rejected;
            for (const auto& v : ids) if (!accepted.contains(v)) rejected.append(v);
            if (!ids.isEmpty() && accepted.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("不支持的设备 id")}};
            for (const auto& v : accepted) if (radarCpuDeviceIds().contains(v.toString())) { m_wmc->requestWork(); break; }
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("acceptedDeviceIds"), accepted}, {QStringLiteral("rejectedDeviceIds"), rejected}};
        });
        m_ws->handlePost(QStringLiteral("/api/stop"), [this](const QJsonObject& body) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastStopMs < 500)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("throttled"), true}};
            m_lastStopMs = now;
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            QJsonArray accepted;
            if (ids.isEmpty()) { accepted.append(QStringLiteral("radar_cpu")); }
            else { auto c = radarCpuDeviceIds(); for (const auto& v : ids) if (c.contains(v.toString())) accepted.append(v.toString()); }
            QJsonArray rejected;
            for (const auto& v : ids) if (!accepted.contains(v)) rejected.append(v);
            if (!ids.isEmpty() && accepted.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("不支持的设备 id")}};
            for (const auto& v : accepted) if (radarCpuDeviceIds().contains(v.toString())) { m_wmc->requestStandby(); break; }
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("acceptedDeviceIds"), accepted}, {QStringLiteral("rejectedDeviceIds"), rejected}};
        });
    }

    // ── Device Registry API ──
    if (m_deviceReg) {
        m_ws->handleGet(QStringLiteral("/api/devices"), [this](const QJsonObject&) {
            QJsonArray arr;
            for (const auto& d : m_deviceReg->list()) {
                QJsonObject item;
                item[QStringLiteral("id")]                  = d.id;
                item[QStringLiteral("name")]                = d.name;
                item[QStringLiteral("brand")]               = d.brand;
                item[QStringLiteral("kind")]                = static_cast<int>(d.kind);
                item[QStringLiteral("lng")]                 = d.lng;
                item[QStringLiteral("lat")]                 = d.lat;
                item[QStringLiteral("altitude")]            = d.altitude;
                item[QStringLiteral("maxRange")]            = d.maxRange;
                item[QStringLiteral("facesJson")]           = d.facesJson;
                item[QStringLiteral("endpointId")]          = d.endpointId;
                item[QStringLiteral("linkedRadarId")]       = d.linkedRadarId;
                item[QStringLiteral("mountOffsetAzimuth")]  = d.mountOffsetAzimuth;
                item[QStringLiteral("coordinateSystem")]    = d.coordinateSystem;
                item[QStringLiteral("status")]              = static_cast<int>(d.status);
                arr.append(item);
            }
            QJsonObject r; r[QStringLiteral("ok")] = true; r[QStringLiteral("devices")] = arr;
            return r;
        });

        auto deviceFromBody = [](const QJsonObject& body) -> DeviceInfo {
            DeviceInfo d;
            d.id           = body[QStringLiteral("id")].toString();
            d.name         = body[QStringLiteral("name")].toString();
            d.brand        = body[QStringLiteral("brand")].toString();
            d.kind         = static_cast<DeviceKind>(body[QStringLiteral("kind")].toInt());
            d.lng          = body[QStringLiteral("lng")].toDouble();
            d.lat          = body[QStringLiteral("lat")].toDouble();
            d.altitude     = body[QStringLiteral("altitude")].toDouble();
            d.maxRange     = body[QStringLiteral("maxRange")].toDouble();
            d.facesJson    = body[QStringLiteral("facesJson")].toString(QStringLiteral("[]"));
            d.endpointId   = body[QStringLiteral("endpointId")].toString();
            d.linkedRadarId = body[QStringLiteral("linkedRadarId")].toString();
            d.mountOffsetAzimuth = body[QStringLiteral("mountOffsetAzimuth")].toDouble();
            d.coordinateSystem = body[QStringLiteral("coordinateSystem")].toString(QStringLiteral("WGS-84"));
            return d;
        };

        auto validate = [](const DeviceInfo& d) -> QString {
            if (d.id.isEmpty() || d.name.isEmpty())
                return QStringLiteral("id 和 name 不能为空");
            if (d.kind < DeviceKind::Radar || d.kind > DeviceKind::Gps)
                return QStringLiteral("kind 无效 (0=Radar,1=Opto,2=Level,3=Gps)");
            if (d.lng < -180 || d.lng > 180)
                return QStringLiteral("longitude 必须在 [-180, 180] 范围内");
            if (d.lat < -90 || d.lat > 90)
                return QStringLiteral("latitude 必须在 [-90, 90] 范围内");
            if (d.kind == DeviceKind::Radar && !d.facesJson.isEmpty()) {
                QJsonParseError err;
                QJsonDocument::fromJson(d.facesJson.toUtf8(), &err);
                if (err.error != QJsonParseError::NoError)
                    return QStringLiteral("facesJson JSON 格式错误");
            }
            return {};
        };

        m_ws->handlePost(QStringLiteral("/api/devices"), [this, validate, deviceFromBody](const QJsonObject& body) {
            auto d = deviceFromBody(body);
            QString err = validate(d);
            if (!err.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            bool ok = m_deviceReg->create(d);
            return QJsonObject{{QStringLiteral("ok"), ok}};
        });

        m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), [this, validate, deviceFromBody](const QString& suffix, const QJsonObject& body) {
            auto d = deviceFromBody(body);
            if (!d.id.isEmpty() && d.id != suffix)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("路径 id 与 body id 不匹配")}};
            if (d.id.isEmpty()) d.id = suffix;
            if (!m_deviceReg->get(d.id).has_value())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("设备不存在")}};
            QString err = validate(d);
            if (!err.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            bool ok = m_deviceReg->update(d);
            return QJsonObject{{QStringLiteral("ok"), ok}};
        });

        m_ws->handleDeletePrefix(QStringLiteral("/api/devices/"), [this](const QString& suffix, const QJsonObject&) {
            if (suffix.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("id 不能为空")}};
            if (!m_deviceReg->get(suffix).has_value())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("设备不存在")}};
            bool ok = m_deviceReg->remove(suffix);
            return QJsonObject{{QStringLiteral("ok"), ok}};
        });
    }
}

void WebApiController::connectWebPush()
{
    auto* ws = m_ws;
    auto* wmc = m_wmc;

    // HTTP 日志转发
    QObject::connect(ws, &web::WebService::serverLog,
                     [](const QString& level, const QString& msg) {
        if (level == QStringLiteral("error"))
            qWarning().noquote() << QStringLiteral("[HTTP]") << msg;
        else qDebug().noquote() << QStringLiteral("[HTTP]") << msg;
    });

    // SystemBus → WS 航迹推送
    m_trackSubId = SystemBus::getInstance().subscribeByTarget(UI_OUTPUTWIDGET,
        [ws](const DataPacket* pkt) {
            if (!pkt || pkt->payloadType != PAYLOAD_TYPE_GENERAL_PACKAGE) return;
            if (pkt->payloadSize < sizeof(GeneralPackage)) return;
            GeneralPackage gp;
            memcpy(&gp, pkt->payload.rawData, sizeof(GeneralPackage));
            const TargetType& tgt = gp.h_c.TrackInfo;
            QJsonObject msg;
            msg[QStringLiteral("type")] = QStringLiteral("tracks");
            msg[QStringLiteral("deviceType")] = static_cast<int>(pkt->sourceType);
            QJsonObject track;
            track[QStringLiteral("targetId")]    = static_cast<int>(tgt.TargetID);
            track[QStringLiteral("range")]       = tgt.Range;
            track[QStringLiteral("azimuth")]     = tgt.Azimuth;
            track[QStringLiteral("elevation")]   = tgt.Elevation;
            track[QStringLiteral("radialSpeed")] = tgt.RadialSpeed;
            track[QStringLiteral("rcs")]         = tgt.RCS;
            track[QStringLiteral("altitude")]    = tgt.Altitude;
            track[QStringLiteral("x")]           = tgt.x;
            track[QStringLiteral("y")]           = tgt.y;
            track[QStringLiteral("z")]           = tgt.z;
            track[QStringLiteral("vx")]          = tgt.vx;
            track[QStringLiteral("vy")]          = tgt.vy;
            track[QStringLiteral("vz")]          = tgt.vz;
            track[QStringLiteral("timestamp")]   = static_cast<double>(pkt->timeStamp);
            msg[QStringLiteral("track")] = track;
            QMetaObject::invokeMethod(ws, [ws, msg]() { ws->broadcast(msg); }, Qt::QueuedConnection);
        });
    // Logger → WS 日志推送
    Logger::instance().setExternalCallback([ws](const QString& level, const QString& msg) {
        if (level == QStringLiteral("DEBUG")) return;
        QJsonObject m;
        m[QStringLiteral("type")] = QStringLiteral("log");
        m[QStringLiteral("level")] = level;
        m[QStringLiteral("message")] = msg;
        m[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
        QMetaObject::invokeMethod(ws, [ws, m]() { ws->broadcast(m); }, Qt::QueuedConnection);
    });

    // WS 定时状态推送
    auto* timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, [this, wmc, ws]() {
        auto health = m_health();
        QJsonObject msg;
        msg[QStringLiteral("type")] = QStringLiteral("status");
        msg[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        msg[QStringLiteral("upTime")] = health.timestampMs;
        msg[QStringLiteral("devices")] = buildDeviceStatusArray();
        auto st3 = wmc ? wmc->state() : RadarWorkModeController::State::Standby;
        msg[QStringLiteral("acquisition")] = (st3 == RadarWorkModeController::State::Starting
                                           || st3 == RadarWorkModeController::State::Working);
        msg[QStringLiteral("state")] = wmc ? RadarWorkModeController::stateName(wmc->state()) : QStringLiteral("Unknown");
        ws->broadcast(msg);
    });
    timer->start(1000);
}

void WebApiController::shutdown()
{
    if (m_trackSubId != 0) {
        SystemBus::getInstance().unsubscribe(m_trackSubId);
        m_trackSubId = 0;
    }
    Logger::instance().setExternalCallback(nullptr);
}

QJsonArray WebApiController::buildDeviceStatusArray() const
{
    QJsonArray arr;
    bool wmcMerged = false;

    if (m_deviceReg) {
        for (const auto& d : m_deviceReg->list()) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = d.id;
            dev[QStringLiteral("name")] = d.name;
            dev[QStringLiteral("kind")] = static_cast<int>(d.kind);

            if (m_wmc && mapsToRadarCpu(d.id, d.endpointId)) {
                dev[QStringLiteral("state")] = RadarWorkModeController::stateName(m_wmc->state());
                auto st = m_wmc->state();
                dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                                   || st == RadarWorkModeController::State::Working);
                wmcMerged = true;
            } else {
                dev[QStringLiteral("state")] = QStringLiteral("Offline");
                dev[QStringLiteral("acquisition")] = false;
            }
            arr.append(dev);
        }
    }

    if (m_wmc && !wmcMerged) {
        QJsonObject dev;
        dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
        dev[QStringLiteral("state")] = RadarWorkModeController::stateName(m_wmc->state());
        auto st = m_wmc->state();
        dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                           || st == RadarWorkModeController::State::Working);
        arr.append(dev);
    }

    return arr;
}

bool WebApiController::mapsToRadarCpu(const QString& deviceId, const QString& endpointId) const
{
    return deviceId == QStringLiteral("radar_cpu")
        || endpointId == QStringLiteral("radar_cpu");
}

QSet<QString> WebApiController::radarCpuDeviceIds() const
{
    QSet<QString> ids;
    ids.insert(QStringLiteral("radar_cpu"));
    if (m_deviceReg) {
        for (const auto& d : m_deviceReg->list()) {
            if (mapsToRadarCpu(d.id, d.endpointId))
                ids.insert(d.id);
        }
    }
    return ids;
}
