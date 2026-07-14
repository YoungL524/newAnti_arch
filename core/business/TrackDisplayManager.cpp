#include "TrackDisplayManager.h"
#include "TaskService.h"
#include "bus/BusEnvelope.h"
#include "bus/SystemBus.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <memory>

static constexpr int kMaxSnapshotPoints = 600;
static constexpr int kMaxCachedPointsAcrossTracks = 50000;
static constexpr int kSnapshotIntervalMs = 1000;
static constexpr qint64 kTrackStaleMs = 5000;

TrackDisplayManager::TrackDisplayManager(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        syncActiveTaskContext();
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
    resetCacheState();
}

void TrackDisplayManager::resetCacheState()
{
    m_caches.clear();
    m_interactions.clear();
    m_pointOrder.clear();
    m_nextPointSequence = 0;
    m_cachedPointCount = 0;
}

void TrackDisplayManager::setTaskService(TaskService* taskService)
{
    m_taskService = taskService;
    syncActiveTaskContext();
}

void TrackDisplayManager::syncActiveTaskContext()
{
    QString activeId;
    QSet<QString> activeDevices;
    if (m_taskService) {
        if (const auto ctx = m_taskService->getActiveTask()) {
            activeId = ctx->id;
            for (const auto& devId : ctx->deviceIds)
                activeDevices.insert(devId);
        }
    }
    if (activeId == m_activeTaskId && activeDevices == m_activeDeviceIds)
        return;
    m_activeTaskId = activeId;
    m_activeDeviceIds = activeDevices;
    resetCacheState();
}

bool TrackDisplayManager::isPointVisible(const TrackPoint& pt) const
{
    if (pt.deviceId.isEmpty() || m_activeTaskId.isEmpty())
        return false;
    if (pt.taskId.isEmpty())
        return false;
    return pt.taskId == m_activeTaskId && m_activeDeviceIds.contains(pt.deviceId);
}

void TrackDisplayManager::addPoint(const TrackPoint& pt)
{
    syncActiveTaskContext();

    TrackPoint scoped = pt;
    if (scoped.taskId.isEmpty() && m_taskService)
        scoped.taskId = m_taskService->resolveTaskIdForDevice(scoped.deviceId);
    if (!isPointVisible(scoped))
        return;

    TrackKey key{scoped.deviceId, scoped.trackId};
    if (!m_interactions.contains(key))
        m_interactions[key] = TrackDisplayPoint::Interaction{};

    auto& cache = m_caches[key];
    const uint64_t sequence = m_nextPointSequence++;
    cache.points.push_back({scoped, cache.nextPointIndex++, sequence});
    cache.lastUpdateMs = scoped.timestampMs;
    m_pointOrder.push_back({key, sequence});
    ++m_cachedPointCount;
    enforceGlobalCacheBudget();

}

void TrackDisplayManager::enforceGlobalCacheBudget()
{
    while (m_cachedPointCount > kMaxCachedPointsAcrossTracks && !m_pointOrder.empty()) {
        const PointOrderEntry oldest = m_pointOrder.front();
        m_pointOrder.pop_front();

        auto cacheIt = m_caches.find(oldest.key);
        if (cacheIt == m_caches.end() || cacheIt->points.empty()
            || cacheIt->points.front().sequence != oldest.sequence) {
            continue;
        }

        cacheIt->points.pop_front();
        --m_cachedPointCount;
        if (cacheIt->points.empty()) {
            m_caches.erase(cacheIt);
            m_interactions.remove(oldest.key);
        }
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
        auto cacheIt = m_caches.find(key);
        if (cacheIt != m_caches.end()) {
            m_cachedPointCount -= static_cast<int>(cacheIt->points.size());
            m_caches.erase(cacheIt);
        }
        m_interactions.remove(key);
    }
    if (!toRemove.isEmpty()) {
        QSet<TrackKey> removed;
        for (const auto& key : toRemove)
            removed.insert(key);
        std::deque<PointOrderEntry> kept;
        for (const auto& entry : m_pointOrder) {
            if (!removed.contains(entry.key))
                kept.push_back(entry);
        }
        m_pointOrder.swap(kept);
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
    candidates.reserve(m_cachedPointCount);

    for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
        const auto& key = it.key();
        const auto& cache = it.value();
        auto interaction = m_interactions.value(key);

        for (const auto& cp : cache.points) {
            TrackPoint pt = cp.point;
            if (pt.taskId.isEmpty() && m_taskService)
                pt.taskId = m_taskService->resolveTaskIdForDevice(pt.deviceId);
            if (!isPointVisible(pt))
                continue;

            TrackDisplayPoint dp;
            dp.trackId      = pt.trackId;
            dp.deviceId     = pt.deviceId;
            dp.taskId       = pt.taskId;
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

    if (candidates.size() > kMaxSnapshotPoints) {
        std::nth_element(candidates.begin(),
                         candidates.begin() + kMaxSnapshotPoints,
                         candidates.end(),
                         [](const Candidate& a, const Candidate& b) { return a.ts > b.ts; });
        candidates.resize(kMaxSnapshotPoints);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.ts < b.ts; });

    for (auto& c : candidates)
        snap.tracks.append(std::move(c.dp));

    return snap;
}

void TrackDisplayManager::setInteraction(
    const QString& taskId, const QString& deviceId, uint32_t trackId,
    bool selected, bool trackingEnabled)
{
    syncActiveTaskContext();
    if (taskId.isEmpty() || taskId != m_activeTaskId
        || !m_activeDeviceIds.contains(deviceId)) {
        qWarning() << "[TrackDisplayManager] 忽略非当前任务的航迹交互命令:"
                   << taskId << deviceId << trackId;
        return;
    }

    TrackKey key{deviceId, trackId};
    const auto cacheIt = m_caches.constFind(key);
    if (cacheIt == m_caches.constEnd() || cacheIt->points.empty()) {
        qWarning() << "[TrackDisplayManager] 忽略不存在航迹的交互命令:"
                   << taskId << deviceId << trackId;
        return;
    }

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
        guard->setInteraction(cmdPtr->taskId, cmdPtr->deviceId, cmdPtr->trackId,
                              cmdPtr->selected, cmdPtr->trackingEnabled);
    }, Qt::QueuedConnection);
}

void TrackDisplayManager::deliverSnapshot(TrackDisplaySnapshot&& snap)
{
    auto shared = std::make_shared<TrackDisplaySnapshot>(std::move(snap));
    SystemBus::getInstance().publish(
        BusEnvelope::makeTrackDisplaySnapshot(std::move(shared)));
}
