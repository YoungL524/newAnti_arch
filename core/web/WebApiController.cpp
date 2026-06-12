#include "WebApiController.h"
#include "WebService.h"
#include "bus/SystemBus.h"
#include "bus/GenericPackets.h"
#include "RadarWorkModeController.h"
#include "business/DeviceRegistry.h"
#include "business/RuntimeSettingsService.h"
#include "business/TrackDisplayManager.h"
#include "log/Logger.h"
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <QTimer>

WebApiController::WebApiController(web::WebService* ws,
                                   RadarWorkModeController* wmc,
                                   DeviceRegistry* deviceRegistry,
                                   RuntimeSettingsService* runtimeSettings,
                                   RuntimeStatusProvider runtimeStatusProvider,
                                   HealthProvider healthProvider,
                                   TrackDisplayManager* trackDisplayManager,
                                   QObject* parent)
    : QObject(parent)
    , m_ws(ws)
    , m_wmc(wmc)
    , m_deviceReg(deviceRegistry)
    , m_runtimeSettings(runtimeSettings)
    , m_runtimeStatusProvider(std::move(runtimeStatusProvider))
    , m_health(std::move(healthProvider))
    , m_trackMgr(trackDisplayManager)
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
        auto handleCommand = [this](const QJsonObject& body, bool work) -> QJsonObject {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            qint64& lastMs = work ? m_lastStartMs : m_lastStopMs;
            if (now - lastMs < 500)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("throttled"), true}};
            lastMs = now;

            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            // Cache mapping result once per request
            auto mappedIds = radarCpuDeviceIds();

            QJsonArray accepted;
            if (ids.isEmpty()) { accepted.append(QStringLiteral("radar_cpu")); }
            else { for (const auto& v : ids) if (mappedIds.contains(v.toString())) accepted.append(v.toString()); }

            QJsonArray rejected;
            for (const auto& v : ids) if (!accepted.contains(v)) rejected.append(v);
            if (!ids.isEmpty() && accepted.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("不支持的设备 id")}};

            for (const auto& v : accepted)
                if (mappedIds.contains(v.toString())) { if (work) m_wmc->requestWork(); else m_wmc->requestStandby(); break; }

            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("acceptedDeviceIds"), accepted}, {QStringLiteral("rejectedDeviceIds"), rejected}};
        };
        m_ws->handlePost(QStringLiteral("/api/start"), [this, handleCommand](const QJsonObject& body) { return handleCommand(body, true); });
        m_ws->handlePost(QStringLiteral("/api/stop"),  [this, handleCommand](const QJsonObject& body) { return handleCommand(body, false); });
    }

    auto handleTrackInteraction = std::make_shared<std::function<std::optional<QJsonObject>(const QString&, const QJsonObject&)>>(
        [this](const QString& suffix, const QJsonObject& body) -> std::optional<QJsonObject> {
        if (!m_trackMgr)
            return std::nullopt;

        const auto parts = suffix.split(QChar('/'), Qt::SkipEmptyParts);
        if (parts.size() != 4
         || parts[1] != QStringLiteral("tracks")
         || parts[3] != QStringLiteral("interaction"))
            return std::nullopt;

        const QString deviceId = parts[0];
        bool ok = false;
        quint64 trackId64 = parts[2].toULongLong(&ok);
        if (deviceId.isEmpty() || !ok || trackId64 > std::numeric_limits<uint32_t>::max())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("deviceId 或 trackId 无效")}};

        if (!body.contains(QStringLiteral("selected")) || !body.value(QStringLiteral("selected")).isBool())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("selected 必须是 bool")}};
        if (!body.contains(QStringLiteral("trackingEnabled")) || !body.value(QStringLiteral("trackingEnabled")).isBool())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("trackingEnabled 必须是 bool")}};

        const bool selected = body.value(QStringLiteral("selected")).toBool();
        const bool trackingEnabled = body.value(QStringLiteral("trackingEnabled")).toBool();
        const uint32_t trackId = static_cast<uint32_t>(trackId64);
        m_trackMgr->setInteraction(deviceId, trackId, selected, trackingEnabled);

        QJsonObject interaction;
        interaction[QStringLiteral("deviceId")] = deviceId;
        interaction[QStringLiteral("trackId")] = static_cast<qint64>(trackId);
        interaction[QStringLiteral("selected")] = selected;
        interaction[QStringLiteral("trackingEnabled")] = trackingEnabled;
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("interaction"), interaction}};
        }
    );

    // ── Device Registry API ──
    if (m_deviceReg) {
        auto deviceInfoToJson = [](const DeviceInfo& dev) -> QJsonObject {
            QJsonObject j;
            j[QStringLiteral("id")] = dev.id;
            j[QStringLiteral("name")] = dev.name;
            j[QStringLiteral("brand")] = dev.brand;
            j[QStringLiteral("kind")] = static_cast<int>(dev.kind);
            j[QStringLiteral("lng")] = dev.lng;
            j[QStringLiteral("lat")] = dev.lat;
            j[QStringLiteral("altitude")] = dev.altitude;
            j[QStringLiteral("maxRange")] = dev.maxRange;
            j[QStringLiteral("facesJson")] = dev.facesJson;
            j[QStringLiteral("endpointId")] = dev.endpointId;
            j[QStringLiteral("linkedRadarId")] = dev.linkedRadarId;
            j[QStringLiteral("mountOffsetAzimuth")] = dev.mountOffsetAzimuth;
            j[QStringLiteral("coordinateSystem")] = dev.coordinateSystem;
            return j;
        };

        m_ws->handleGet(QStringLiteral("/api/devices"), [this, deviceInfoToJson](const QJsonObject&) {
            QJsonArray arr;
            for (const auto& d : m_deviceReg->list()) {
                auto item = deviceInfoToJson(d);
                item[QStringLiteral("status")] = static_cast<int>(d.status);
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
            if (d.name.isEmpty())
                return QStringLiteral("name 不能为空");
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

        m_ws->handlePost(QStringLiteral("/api/devices"), [this, validate, deviceFromBody, deviceInfoToJson](const QJsonObject& body) {
            auto d = deviceFromBody(body);
            QString err = validate(d);
            if (!err.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            auto created = m_deviceReg->createAssigningId(d);
            if (!created.has_value())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("创建设备失败")}};
            QJsonObject r;
            r[QStringLiteral("ok")] = true;
            r[QStringLiteral("device")] = deviceInfoToJson(created.value());
            return r;
        });

        m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), [this, validate, deviceFromBody, handleTrackInteraction](const QString& suffix, const QJsonObject& body) {
            if (auto r = (*handleTrackInteraction)(suffix, body))
                return *r;

            if (suffix.contains(QChar('/')))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("无效路径")}};

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

    // ── Track Interaction API ──
    if (!m_deviceReg && m_trackMgr) {
        m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), [this, handleTrackInteraction](const QString& suffix, const QJsonObject& body) {
            if (auto r = (*handleTrackInteraction)(suffix, body))
                return *r;
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("无效路径")}};
        });
    }

    // ── Runtime Settings API ──
    auto buildStatusJson = [this]() -> QJsonObject {
        QJsonObject st;
        if (m_runtimeStatusProvider) {
            auto ds = m_runtimeStatusProvider();
            st[QStringLiteral("currentSceneNumber")] = static_cast<int>(ds.currentSceneNumber);
            st[QStringLiteral("sendCount")] = static_cast<qint64>(ds.sendCount);
        }
        return st;
    };

    m_ws->handleGet(QStringLiteral("/api/runtime-settings"), [this, buildStatusJson](const QJsonObject&) {
        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        QJsonObject settingsJson;
        if (m_runtimeSettings) {
            auto s = m_runtimeSettings->get();
            settingsJson[QStringLiteral("frequencyControl")] = static_cast<int>(s.frequencyControl);
            settingsJson[QStringLiteral("waveControl")] = static_cast<int>(s.waveControl);
        }
        r[QStringLiteral("settings")] = settingsJson;
        r[QStringLiteral("status")] = buildStatusJson();
        return r;
    });

    if (m_runtimeSettings) {
        m_ws->handlePut(QStringLiteral("/api/runtime-settings"), [this, buildStatusJson](const QJsonObject& body) {
            auto parseField = [&](const QString& key, uint32_t* out, QString* err) -> bool {
                if (!body.contains(key)) { *err = key + QStringLiteral(" 不能为空"); return false; }
                const auto v = body.value(key);
                if (!v.isDouble()) { *err = key + QStringLiteral(" 必须是数字"); return false; }
                const double d = v.toDouble();
                if (!std::isfinite(d) || std::floor(d) != d) { *err = key + QStringLiteral(" 必须是整数"); return false; }
                if (d < 0) { *err = key + QStringLiteral(" 必须是非负整数"); return false; }
                if (d > static_cast<double>(std::numeric_limits<uint32_t>::max())) { *err = key + QStringLiteral(" 超出 uint32 范围"); return false; }
                *out = static_cast<uint32_t>(d);
                return true;
            };
            RadarRuntimeSettings s;
            QString err;
            if (!parseField(QStringLiteral("frequencyControl"), &s.frequencyControl, &err)
             || !parseField(QStringLiteral("waveControl"), &s.waveControl, &err))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            // service validate (含 0..255 等业务规则)
            if (!m_runtimeSettings->validate(s, &err))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            // Publish first, then commit
            DataPacket packet;
            packet.sourceType = SOURCE_RUNTIME_SETTINGS;
            packet.dispatchTarget = 0;
            static uint64_t seq = 0;
            packet.packetSequence = ++seq;
            packet.timeStamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            packDataToPayload(packet, s);
            if (!SystemBus::getInstance().publish(std::move(packet)))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("运行时参数事件发布失败")}};
            if (!m_runtimeSettings->set(s, &err))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            QJsonObject settingsJson;
            settingsJson[QStringLiteral("frequencyControl")] = static_cast<int>(s.frequencyControl);
            settingsJson[QStringLiteral("waveControl")] = static_cast<int>(s.waveControl);
            QJsonObject r;
            r[QStringLiteral("ok")] = true;
            r[QStringLiteral("settings")] = settingsJson;
            r[QStringLiteral("status")] = buildStatusJson();
            return r;
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
