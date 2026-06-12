#include "TrackDisplayManager.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>

static constexpr int kMaxPointsPerTrack = 500;
static constexpr qint64 kTimeWindowMs = 60 * 1000;
static constexpr int kMaxSnapshotPoints = 5000;
static constexpr int kSnapshotIntervalMs = 1000;

TrackDisplayManager::TrackDisplayManager(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        auto snap = snapshot();
        if (!snap.tracks.isEmpty())
            emit snapshotReady(snap);
    });
}

void TrackDisplayManager::start() { m_timer->start(kSnapshotIntervalMs); }
void TrackDisplayManager::stop()  { m_timer->stop(); }

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

    const qint64 cutoff = pt.timestampMs - kTimeWindowMs;
    while (!cache.points.isEmpty() && cache.points.first().point.timestampMs < cutoff)
        cache.points.removeFirst();

    while (cache.points.size() > kMaxPointsPerTrack)
        cache.points.removeFirst();

    if (cache.points.isEmpty()) {
        m_caches.remove(key);
        m_interactions.remove(key);
    }
}

void TrackDisplayManager::applyStyle(
    TrackDisplayPoint& dp, uint32_t pointIndex,
    const TrackDisplayPoint::Interaction& interaction) const
{
    if (interaction.trackingEnabled) {
        dp.style.color = QStringLiteral("#f59e0b");
        dp.style.size = 5.0;
        dp.style.opacity = 1.0;
    } else if (interaction.selected) {
        dp.style.color = QStringLiteral("#3b82f6");
        dp.style.size = 4.5;
        dp.style.opacity = 1.0;
    } else if (pointIndex < 3) {
        dp.style.color = QStringLiteral("#22c55e");
        dp.style.size = 4.0;
        dp.style.opacity = 0.9;
    } else {
        dp.style.color = QStringLiteral("#22c55e");
        dp.style.size = 3.0;
        dp.style.opacity = 0.7;
    }
}

TrackDisplaySnapshot TrackDisplayManager::snapshot() const
{
    TrackDisplaySnapshot snap;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = snap.timestamp - kTimeWindowMs;

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
            if (pt.timestampMs < cutoff) continue;

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
            dp.pointIndex   = cp.pointIndex;  // 绝对递增序号
            dp.interaction  = interaction;
            applyStyle(dp, cp.pointIndex, interaction);
            candidates.append({dp, pt.timestampMs});
        }
    }

    // 降序保留最新点
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.ts > b.ts; });
    if (candidates.size() > kMaxSnapshotPoints)
        candidates.resize(kMaxSnapshotPoints);

    // 升序输出
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
}
