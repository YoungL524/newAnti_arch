#include "TrackDisplayManager.h"
#include <QDateTime>
#include <QDebug>

static constexpr int kMaxPointsPerTrack = 500;
static constexpr int kMaxSnapshotPoints = 5000;

TrackDisplayManager::TrackDisplayManager(QObject* parent)
    : QObject(parent) {}

void TrackDisplayManager::addPoint(const TrackPoint& pt)
{
    TrackKey key{pt.deviceId, pt.trackId};
    auto& cache = m_caches[key];
    auto& interaction = m_interactions[key];

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
    dp.pointIndex   = cache.nextPointIndex++;
    dp.style        = TrackDisplayPoint::Style();
    dp.interaction  = interaction;

    cache.points.append(dp);
    if (cache.points.size() > kMaxPointsPerTrack)
        cache.points.remove(0, cache.points.size() - kMaxPointsPerTrack);
}

TrackDisplaySnapshot TrackDisplayManager::snapshot() const
{
    TrackDisplaySnapshot snap;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();

    for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
        const auto& cache = it.value();
        for (const auto& dp : cache.points) {
            snap.tracks.append(dp);
            if (snap.tracks.size() >= kMaxSnapshotPoints) {
                qDebug() << "[TrackDisplayManager] snapshot 达到输出上限" << kMaxSnapshotPoints;
                return snap;
            }
        }
    }
    return snap;
}

void TrackDisplayManager::setInteraction(const QString& deviceId, uint32_t trackId,
                                         bool selected, bool trackingEnabled)
{
    TrackKey key{deviceId, trackId};
    auto& interaction = m_interactions[key];
    interaction.selected = selected;
    interaction.trackingEnabled = trackingEnabled;
}
