#include "TrackDisplayManager.h"
#include "bus/BusEnvelope.h"
#include "bus/SystemBus.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <memory>

static constexpr int kMaxSnapshotPoints = 8000;
static constexpr int kSnapshotIntervalMs = 500;
static constexpr qint64 kTrackStaleMs = 5000;

TrackDisplayManager::TrackDisplayManager(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        pruneStaleTracks(now);
        deliverSnapshot(snapshot(now));
    });
}

void TrackDisplayManager::start()
{
    m_acceptCommands.store(true, std::memory_order_release);
    if (m_cmdSubId == 0) {
        QPointer<TrackDisplayManager> guard(this);
        m_cmdSubId = SystemBus::getInstance().subscribeEnvelope(
            SOURCE_WEB_API,
            DISPATCH_TRACK_DISPLAY_MANAGER,
            [guard](const BusEnvelope& env) {
                if (guard)
                    guard->onInteractionCommand(env);
            });
        if (m_cmdSubId == 0)
            qWarning() << "[TrackDisplayManager] 订阅 TrackInteractionCommand 失败";
    }
    m_timer->start(kSnapshotIntervalMs);
}

void TrackDisplayManager::stop()
{
    m_acceptCommands.store(false, std::memory_order_release);
    m_timer->stop();
    if (m_cmdSubId != 0) {
        SystemBus::getInstance().unsubscribe(m_cmdSubId);
        m_cmdSubId = 0;
    }
}

void TrackDisplayManager::clear()
{
    m_timer->stop();
    m_caches.clear();
    m_interactions.clear();
}

void TrackDisplayManager::addPoint(const TrackPoint& pt)
{
    TrackKey key{pt.deviceId, pt.trackId};
    if (!m_interactions.contains(key))
        m_interactions[key] = TrackDisplayPoint::Interaction{};

    auto& cache = m_caches[key];
    cache.points.append({pt, cache.nextPointIndex++});
    cache.lastUpdateMs = pt.timestampMs;

    if (cache.points.isEmpty()) {
        m_caches.remove(key);
        m_interactions.remove(key);
    }
}

void TrackDisplayManager::pruneStaleTracks(qint64 nowMs)
{
    QList<TrackKey> toRemove;
    for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
        if (nowMs - it.value().lastUpdateMs > kTrackStaleMs)
            toRemove.append(it.key());
    }
    for (const auto& key : toRemove) {
        m_caches.remove(key);
        m_interactions.remove(key);
    }
}

TrackDisplaySnapshot TrackDisplayManager::snapshot() const
{
    return snapshot(QDateTime::currentMSecsSinceEpoch());
}

void TrackDisplayManager::applyStyle(
    TrackDisplayPoint& dp, uint32_t pointIndex,
    const TrackDisplayPoint::Interaction& interaction) const
{
    if (interaction.trackingEnabled) {
        dp.style.color = QStringLiteral("#ea580c");
        dp.style.size = 5.0;
        dp.style.opacity = 1.0;
    } else if (interaction.selected) {
        dp.style.color = QStringLiteral("#ea580c");
        dp.style.size = 4.0;
        dp.style.opacity = 1.0;
    } else if (pointIndex < 3) {
        dp.style.color = QStringLiteral("#059669");
        dp.style.size = 3.5;
        dp.style.opacity = 0.95;
    } else {
        dp.style.color = QStringLiteral("#059669");
        dp.style.size = 2.5;
        dp.style.opacity = 0.75;
    }
}

TrackDisplaySnapshot TrackDisplayManager::snapshot(qint64 nowMs) const
{
    Q_UNUSED(nowMs);
    TrackDisplaySnapshot snap;
    snap.timestamp = nowMs;

    struct Candidate {
        TrackDisplayPoint dp;
        qint64 ts;
    };
    QVector<Candidate> candidates;

    for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
        const auto& key = it.key();
        const auto& cache = it.value();
        auto interaction = m_interactions.value(key);

        for (const auto& cp : cache.points) {
            const auto& pt = cp.point;

            TrackDisplayPoint dp;
            dp.trackId      = pt.trackId;
            dp.deviceId     = pt.deviceId;
            dp.timestamp    = pt.timestampMs;
            dp.range        = pt.range;
            dp.azimuth      = pt.azimuth;
            dp.elevation    = pt.elevation;
            dp.x            = pt.x;
            dp.y            = pt.y;
            dp.z            = pt.z;
            dp.radialSpeed  = pt.radialSpeed;
            dp.rcs          = pt.rcs;
            dp.altitude     = pt.altitude;
            dp.pointIndex   = cp.pointIndex;
            dp.interaction  = interaction;
            applyStyle(dp, cp.pointIndex, interaction);
            candidates.append({dp, pt.timestampMs});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.ts > b.ts; });
    if (candidates.size() > kMaxSnapshotPoints)
        candidates.resize(kMaxSnapshotPoints);

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.ts < b.ts; });

    for (auto& c : candidates)
        snap.tracks.append(std::move(c.dp));

    return snap;
}

void TrackDisplayManager::setInteraction(
    const QString& deviceId, uint32_t trackId,
    bool selected, bool trackingEnabled)
{
    TrackKey key{deviceId, trackId};
    auto& interaction = m_interactions[key];
    interaction.selected = selected;
    interaction.trackingEnabled = trackingEnabled;
    deliverSnapshot(snapshot(QDateTime::currentMSecsSinceEpoch()));
}

void TrackDisplayManager::onInteractionCommand(const BusEnvelope& env)
{
    if (!m_acceptCommands.load(std::memory_order_acquire))
        return;

    auto cmdPtr = env.payloadAs<TrackInteractionCommand>(
        BusPayloadKind::TrackInteractionCommand);
    if (!cmdPtr)
        return;

    QPointer<TrackDisplayManager> guard(this);
    QMetaObject::invokeMethod(this, [guard, cmdPtr]() {
        if (!guard || !guard->m_acceptCommands.load(std::memory_order_acquire))
            return;
        guard->setInteraction(cmdPtr->deviceId, cmdPtr->trackId,
                              cmdPtr->selected, cmdPtr->trackingEnabled);
    }, Qt::QueuedConnection);
}

void TrackDisplayManager::deliverSnapshot(TrackDisplaySnapshot&& snap)
{
    auto shared = std::make_shared<TrackDisplaySnapshot>(std::move(snap));
    SystemBus::getInstance().publish(
        BusEnvelope::makeTrackDisplaySnapshot(std::move(shared)));
}
