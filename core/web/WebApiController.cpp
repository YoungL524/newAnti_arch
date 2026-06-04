#include "WebApiController.h"
#include "WebService.h"
#include "bus/SystemBus.h"
#include "bus/GenericPackets.h"
#include "protocol/StructureReceive.h"
#include "RadarWorkModeController.h"
#include "log/Logger.h"
#include <QDateTime>
#include <QJsonArray>
#include <QTimer>

WebApiController::WebApiController(web::WebService* ws,
                                   RadarWorkModeController* wmc,
                                   HealthProvider healthProvider,
                                   QObject* parent)
    : QObject(parent)
    , m_ws(ws)
    , m_wmc(wmc)
    , m_health(std::move(healthProvider))
{
}

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
        QJsonArray devices;
        if (m_wmc) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
            dev[QStringLiteral("state")] = RadarWorkModeController::stateName(m_wmc->state());
            auto st = m_wmc->state();
            dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                               || st == RadarWorkModeController::State::Working);
            devices.append(dev);
        }
        s[QStringLiteral("devices")] = devices;
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
            static qint64 lastCmdMs = 0;
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastCmdMs < 500)
                return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("throttled"), true}};
            lastCmdMs = now;
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            bool ok = ids.isEmpty();
            for (const auto& v : ids)
                if (v.toString() == QStringLiteral("radar_cpu")) { ok = true; break; }
            if (ok) m_wmc->requestWork();
            return QJsonObject{{QStringLiteral("ok"), true}};
        });
        m_ws->handlePost(QStringLiteral("/api/stop"), [this](const QJsonObject& body) {
            static qint64 lastCmdMs = 0;
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastCmdMs < 500)
                return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("throttled"), true}};
            lastCmdMs = now;
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            bool ok = ids.isEmpty();
            for (const auto& v : ids)
                if (v.toString() == QStringLiteral("radar_cpu")) { ok = true; break; }
            if (ok) m_wmc->requestStandby();
            return QJsonObject{{QStringLiteral("ok"), true}};
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
        QJsonArray devices;
        if (wmc) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
            dev[QStringLiteral("state")] = RadarWorkModeController::stateName(wmc->state());
            auto st = wmc->state();
            dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                               || st == RadarWorkModeController::State::Working);
            devices.append(dev);
        }
        msg[QStringLiteral("devices")] = devices;
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
