#include "TrackDisplayWebBridge.h"
#include "WebService.h"
#include "bus/SystemBus.h"
#include "TrackTypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QDebug>

TrackDisplayWebBridge::TrackDisplayWebBridge(web::WebService* ws, QObject* parent)
    : QObject(parent)
    , m_ws(ws)
{
}

TrackDisplayWebBridge::~TrackDisplayWebBridge()
{
    stop();
}

void TrackDisplayWebBridge::start()
{
    if (m_subId != 0 || !m_ws)
        return;

    m_active = true;
    QPointer<TrackDisplayWebBridge> guard(this);
    m_subId = SystemBus::getInstance().subscribeBySourceEnvelope(
        SOURCE_TRACK_DISPLAY,
        [guard](const BusEnvelope& env) {
            if (guard)
                guard->onBusEnvelope(env);
        });

    if (m_subId == 0) {
        m_active = false;
        qWarning() << "[TrackDisplayWebBridge] 订阅 SystemBus 失败";
    } else {
        qDebug() << "[TrackDisplayWebBridge] 已订阅 SOURCE_TRACK_DISPLAY";
    }
}

void TrackDisplayWebBridge::stop()
{
    m_active = false;
    if (m_subId == 0)
        return;
    SystemBus::getInstance().unsubscribe(m_subId);
    m_subId = 0;
}

void TrackDisplayWebBridge::onBusEnvelope(const BusEnvelope& env)
{
    if (!m_active || env.payloadKind != BusPayloadKind::TrackDisplaySnapshot)
        return;

    auto snapPtr = env.payloadAs<TrackDisplaySnapshot>();
    if (!snapPtr)
        return;

    QPointer<TrackDisplayWebBridge> guard(this);
    QMetaObject::invokeMethod(this, [guard, snapPtr]() {
        if (guard && guard->m_active)
            guard->pushSnapshot(*snapPtr);
    }, Qt::QueuedConnection);
}

void TrackDisplayWebBridge::pushSnapshot(const TrackDisplaySnapshot& snap)
{
    if (!m_ws)
        return;

    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("tracks");
    msg[QStringLiteral("mode")] = QStringLiteral("snapshot");
    msg[QStringLiteral("timestamp")] = snap.timestamp;
    QJsonArray tracks;
    for (const auto& dp : snap.tracks) {
        QJsonObject t;
        t[QStringLiteral("trackId")]     = static_cast<int>(dp.trackId);
        t[QStringLiteral("deviceId")]    = dp.deviceId;
        t[QStringLiteral("timestamp")]   = dp.timestamp;
        t[QStringLiteral("range")]       = dp.range;
        t[QStringLiteral("azimuth")]     = dp.azimuth;
        t[QStringLiteral("elevation")]   = dp.elevation;
        t[QStringLiteral("radialSpeed")] = dp.radialSpeed;
        t[QStringLiteral("rcs")]         = dp.rcs;
        t[QStringLiteral("altitude")]    = dp.altitude;
        t[QStringLiteral("x")]           = dp.x;
        t[QStringLiteral("y")]           = dp.y;
        t[QStringLiteral("z")]           = dp.z;
        t[QStringLiteral("pointIndex")]  = static_cast<int>(dp.pointIndex);
        QJsonObject style;
        style[QStringLiteral("color")]   = dp.style.color;
        style[QStringLiteral("size")]    = dp.style.size;
        style[QStringLiteral("opacity")] = dp.style.opacity;
        t[QStringLiteral("style")] = style;
        QJsonObject interaction;
        interaction[QStringLiteral("selected")]        = dp.interaction.selected;
        interaction[QStringLiteral("trackingEnabled")] = dp.interaction.trackingEnabled;
        t[QStringLiteral("interaction")] = interaction;
        tracks.append(t);
    }
    msg[QStringLiteral("tracks")] = tracks;
    m_ws->broadcast(msg);
}
