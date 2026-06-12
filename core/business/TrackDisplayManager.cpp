#include "TrackDisplayManager.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>

static constexpr int kMaxPointsPerTrack = 500;
static constexpr qint64 kTimeWindowMs = 60 * 1000;  // 60s
static constexpr int kMaxSnapshotPoints = 5000;
static constexpr int kSnapshotIntervalMs = 1000;

TrackDisplayManager::TrackDisplayManager(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        emit snapshotReady(snapshot());
    });
    m_timer->start(kSnapshotIntervalMs);
}

void TrackDisplayManager::addPoint(const TrackPoint& pt)
{
    TrackKey key{pt.deviceId, pt.trackId};
    if (!m_interactions.contains(key))
        m_interactions[key] = TrackDisplayPoint::Interaction{};

    auto& cache = m_caches[key].points;
    cache.append(pt);

    // 按时间窗口裁剪（60s）
    const qint64 cutoff = pt.timestampMs - kTimeWindowMs;
    while (!cache.isEmpty() && cache.first().timestampMs < cutoff)
        cache.removeFirst();

    // 按数量裁剪（500 点）
    while (cache.size() > kMaxPointsPerTrack)
        cache.removeFirst();
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

    // 收集所有缓存点，带上当前 interaction
    struct Candidate {
        TrackDisplayPoint dp;
        qint64 ts;
    };
    QVector<Candidate> candidates;

    for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
        const auto& key = it.key();
        const auto& cache = it.value();
        auto interaction = m_interactions.value(key);
        uint32_t idx = 0;
        for (const auto& pt : cache.points) {
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
            dp.pointIndex   = idx++;  // 相对序号，从 0 开始
            dp.interaction  = interaction;
            applyStyle(dp, dp.pointIndex, interaction);
            candidates.append({dp, pt.timestampMs});
        }
    }

    // 按 timestamp 降序，保留最新点
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.ts > b.ts; });

    if (candidates.size() > kMaxSnapshotPoints)
        candidates.resize(kMaxSnapshotPoints);

    // 升序输出（前端从左到右时间演进）
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
    // interaction 改变后，下次 snapshot() 会自动合并到所有点
}
