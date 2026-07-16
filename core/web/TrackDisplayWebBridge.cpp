#include "TrackDisplayWebBridge.h"
#include "WebService.h"
#include "bus/SystemBus.h"
#include "TrackTypes.h"
#include "metrics/MonotonicClock.h"
#include "metrics/PipelineLatencyMonitor.h"
#include <QJsonArray>
#include <QJsonDocument>
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

void TrackDisplayWebBridge::setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor)
{
    m_latencyMonitor = latencyMonitor;
}

void TrackDisplayWebBridge::start()
{
    if (m_subId != 0 || !m_ws)
        return;

    m_active.store(true, std::memory_order_release);
    QPointer<TrackDisplayWebBridge> guard(this);
    m_subId = SystemBus::getInstance().subscribeBySourceEnvelope(
        SOURCE_TRACK_DISPLAY,
        [guard](const BusEnvelope& env) {
            if (guard)
                guard->onBusEnvelope(env);
        });

    if (m_subId == 0) {
        m_active.store(false, std::memory_order_release);
        qWarning() << "[TrackDisplayWebBridge] 订阅 SystemBus 失败";
    } else {
        qDebug() << "[TrackDisplayWebBridge] 已订阅 SOURCE_TRACK_DISPLAY";
    }
}

void TrackDisplayWebBridge::stop()
{
    m_active.store(false, std::memory_order_release);
    if (m_subId == 0)
        return;
    SystemBus::getInstance().unsubscribe(m_subId);
    m_subId = 0;
}

void TrackDisplayWebBridge::onBusEnvelope(const BusEnvelope& env)
{
    if (!m_active.load(std::memory_order_acquire))
        return;

    auto snapPtr = env.payloadAs<TrackDisplaySnapshot>(BusPayloadKind::TrackDisplaySnapshot);
    if (!snapPtr)
        return;

    const qint64 queuedSteadyNs = monotonicNowNs();
    if (m_latencyMonitor && snapPtr->publishedSteadyNs > 0) {
        m_latencyMonitor->recordNs(PipelineLatencyStage::SnapshotBusDispatch,
                                   queuedSteadyNs - snapPtr->publishedSteadyNs);
    }

    QPointer<TrackDisplayWebBridge> guard(this);
    QMetaObject::invokeMethod(this, [guard, snapPtr, queuedSteadyNs]() {
        if (guard && guard->m_active.load(std::memory_order_acquire))
            guard->pushSnapshot(*snapPtr, queuedSteadyNs);
    }, Qt::QueuedConnection);
}

void TrackDisplayWebBridge::pushSnapshot(const TrackDisplaySnapshot& snap, qint64 queuedSteadyNs)
{
    if (!m_ws)
        return;

    const qint64 startedSteadyNs = monotonicNowNs();
    if (m_latencyMonitor) {
        m_latencyMonitor->recordNs(PipelineLatencyStage::WebQueue,
                                   startedSteadyNs - queuedSteadyNs);
    }

    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("tracks");
    msg[QStringLiteral("mode")] = QStringLiteral("snapshot");
    msg[QStringLiteral("timestamp")] = snap.timestamp;
    QJsonArray tracks;
    for (const auto& dp : snap.tracks) {
        QJsonObject t;
        t[QStringLiteral("trackId")]     = static_cast<qint64>(dp.trackId);
        t[QStringLiteral("deviceId")]    = dp.deviceId;
        t[QStringLiteral("taskId")]      = dp.taskId;
        t[QStringLiteral("timestamp")]   = dp.timestamp;
        t[QStringLiteral("range")]       = dp.range;
        t[QStringLiteral("azimuth")]     = dp.azimuth;
        t[QStringLiteral("elevation")]   = dp.elevation;
        t[QStringLiteral("radialSpeed")] = dp.radialSpeed;
        t[QStringLiteral("altitude")]    = dp.altitude;
        t[QStringLiteral("pointIndex")]  = static_cast<qint64>(dp.pointIndex);
        // 实时层不需要 x/y/z/rcs；style 仅绿点段下发，interaction 仅非默认时下发
        if (dp.pointIndex >= 3) {
            QJsonObject style;
            style[QStringLiteral("color")]   = dp.style.color;
            style[QStringLiteral("size")]    = dp.style.size;
            style[QStringLiteral("opacity")] = dp.style.opacity;
            t[QStringLiteral("style")] = style;
        }
        if (dp.interaction.selected || dp.interaction.trackingEnabled) {
            QJsonObject interaction;
            interaction[QStringLiteral("selected")]        = dp.interaction.selected;
            interaction[QStringLiteral("trackingEnabled")] = dp.interaction.trackingEnabled;
            t[QStringLiteral("interaction")] = interaction;
        }
        tracks.append(t);
    }
    msg[QStringLiteral("tracks")] = tracks;

    const int bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact).size();
    if (bytes > 48 * 1024) {
        static qint64 lastWarnMs = 0;
        const qint64 now = snap.timestamp;
        if (now - lastWarnMs > 5000) {
            lastWarnMs = now;
            qWarning() << "[TrackDisplayWebBridge] WS tracks 帧过大，已截断 risk"
                       << bytes << "bytes," << snap.tracks.size() << "points";
        }
    }
    m_ws->broadcast(msg);
    if (m_latencyMonitor) {
        const qint64 completedSteadyNs = monotonicNowNs();
        m_latencyMonitor->recordNs(PipelineLatencyStage::WebPrepareAndBroadcast,
                                   completedSteadyNs - startedSteadyNs);
        if (snap.latestIngressSteadyNs > 0) {
            const qint64 ingressToWebSendNs = completedSteadyNs - snap.latestIngressSteadyNs;
            m_latencyMonitor->recordNs(PipelineLatencyStage::IngressToWebSend,
                                       ingressToWebSendNs);

            if (snap.latestDisplayReadySteadyNs > 0
                && snap.snapshotBuildStartedSteadyNs >= snap.latestDisplayReadySteadyNs) {
                const qint64 snapshotWaitNs = snap.snapshotBuildStartedSteadyNs
                    - snap.latestDisplayReadySteadyNs;
                m_latencyMonitor->recordNs(PipelineLatencyStage::SnapshotWait,
                                           snapshotWaitNs);
                m_latencyMonitor->recordNs(PipelineLatencyStage::ActiveBackendToWebSend,
                                           ingressToWebSendNs - snapshotWaitNs);
            }
        }
    }
}
