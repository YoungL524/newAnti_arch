#include "TrackDisplayManager.h"
#include "TaskService.h"
#include "bus/GenericPackets.h"
#include "bus/SystemBus.h"
#include "metrics/MonotonicClock.h"
#include "metrics/PipelineLatencyMonitor.h"
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <memory>

static constexpr int kMaxPublishedPoints = 600; //最大同时发布点数
static constexpr int kMaxCachedPointsAcrossTracks = 50000; // 最大缓存航迹
static constexpr int kDisplayUpdateIntervalMs = 1000; // 刷新间隔
static constexpr qint64 kTrackStaleMs = 5000; // 5s旧航迹回收

/**
 * @brief 创建一秒刷新定时器，并把超时处理固定在缓存所属 QObject 线程。
 */
TrackDisplayManager::TrackDisplayManager(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        syncActiveTaskContext();
        pruneStaleTracks(now);
        publishCurrent(m_forceSnapshot);
    });
}

/**
 * @brief 幂等订阅 Track 与交互命令；回调只复制载荷并排队回本对象线程。
 * @details 两项订阅都登记后才启动刷新，任一失败会保留 forceSnapshot 以便恢复后建立权威基线。
 */
void TrackDisplayManager::start()
{
    m_acceptCommands.store(true, std::memory_order_release);
    if (m_trackSubId == 0) {
        QPointer<TrackDisplayManager> guard(this);
        m_trackSubId = SystemBus::getInstance().subscribeByTarget(
            DISPATCH_TRACK_DISPLAY_MANAGER,
            [guard](const DataPacket* packet) {
                if (guard && packet && packet->command == DataCommand::Track)
                    guard->onTrackPacket(*packet);
            });
        if (m_trackSubId == 0)
            qWarning() << "[TrackDisplayManager] 订阅 DataCommand::Track 失败";
    }
    if (m_cmdSubId == 0) {
        QPointer<TrackDisplayManager> guard(this);
        m_cmdSubId = SystemBus::getInstance().subscribe(
            SOURCE_WEB_API,
            DISPATCH_TRACK_DISPLAY_MANAGER,
            [guard](const DataPacket* packet) {
                if (guard && packet)
                    guard->onInteractionCommand(*packet);
            });
        if (m_cmdSubId == 0)
            qWarning() << "[TrackDisplayManager] 订阅 TrackInteractionCommand 失败";
    }
    m_timer->start(kDisplayUpdateIntervalMs);
}

/**
 * @brief 先关闭跨线程接收闩锁，再取消订阅和定时器，防止关闭期间继续排队新任务。
 */
void TrackDisplayManager::stop()
{
    m_acceptCommands.store(false, std::memory_order_release);
    m_timer->stop();
    if (m_cmdSubId != 0) {
        SystemBus::getInstance().unsubscribe(m_cmdSubId);
        m_cmdSubId = 0;
    }
    if (m_trackSubId != 0) {
        SystemBus::getInstance().unsubscribe(m_trackSubId);
        m_trackSubId = 0;
    }
}

/**
 * @brief 在缓存线程清空任务投影并立即尝试发布空快照，使客户端删除旧点。
 */
void TrackDisplayManager::clear()
{
    m_timer->stop();
    resetCacheState();
    publishCurrent(true);
}

/**
 * @brief 清空缓存、FIFO 和交互状态，并把下一次发布标记为快照恢复。
 */
void TrackDisplayManager::resetCacheState()
{
    m_caches.clear();
    m_interactions.clear();
    m_pointOrder.clear();
    m_nextPointSequence = 0;
    m_cachedPointCount = 0;
    m_forceSnapshot = true;
}

/**
 * @brief 切回对象线程强制发布当前完整窗口，用于 Web 重连或 revision gap。
 */
void TrackDisplayManager::publishReset()
{
    publishCurrent(true);
}

/**
 * @brief 更新任务解析依赖后立即同步活动上下文，防止继续使用旧设备集合。
 */
void TrackDisplayManager::setTaskService(TaskService* taskService)
{
    m_taskService = taskService;
    syncActiveTaskContext();
}

/**
 * @brief 替换可选延时采集器；不改变现有缓存或显示修订。
 */
void TrackDisplayManager::setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor)
{
    m_latencyMonitor = latencyMonitor;
}

/**
 * @brief 从 TaskService 读取活动任务快照，并在任务 ID 或成员设备变化时重置显示窗口。
 */
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

/**
 * @brief 要求航迹已完成识别、taskId 等于活动任务且 deviceId 位于成员集合。
 */
bool TrackDisplayManager::isTrackVisible(const TrackPacket& track) const
{
    if (track.deviceId.isEmpty() || m_activeTaskId.isEmpty())
        return false;
    if (track.taskId.isEmpty())
        return false;
    return track.taskId == m_activeTaskId && m_activeDeviceIds.contains(track.deviceId);
}

/**
 * @brief 在总线线程验证 Track 命令和非空共享载荷，再复制到 QObject 线程处理。
 */
void TrackDisplayManager::onTrackPacket(const DataPacket& packet)
{
    if (!m_acceptCommands.load(std::memory_order_acquire))
        return;
    auto track = packet.sharedPayloadAs<TrackPacket>();
    if (!track || track->recognitionStatus != RecognitionStatus::Completed)
        return;

    QPointer<TrackDisplayManager> guard(this);
    QMetaObject::invokeMethod(this, [guard, track]() {
        if (guard && guard->m_acceptCommands.load(std::memory_order_acquire))
            guard->addTrack(*track);
    }, Qt::QueuedConnection);
}

/**
 * @brief 为可见航迹追加绝对点号和全局序号，记录显示就绪时间并执行全局容量淘汰。
 */
void TrackDisplayManager::addTrack(const TrackPacket& track)
{
    const qint64 processingStartedNs = monotonicNowNs();
    syncActiveTaskContext();

    TrackPacket scoped = track;
    if (scoped.taskId.isEmpty() && m_taskService)
        scoped.taskId = m_taskService->resolveTaskIdForDevice(scoped.deviceId);
    if (!isTrackVisible(scoped))
        return;

    TrackKey key{scoped.deviceId, scoped.trackId};
    if (!m_interactions.contains(key))
        m_interactions[key] = TrackDisplayPoint::Interaction{};

    auto& cache = m_caches[key];
    const uint64_t sequence = m_nextPointSequence++;
    cache.points.push_back({scoped, cache.nextPointIndex++, sequence, 0});
    cache.lastUpdateMs = scoped.receivedAtMs;
    m_pointOrder.push_back({key, sequence});
    ++m_cachedPointCount;
    enforceGlobalCacheBudget();

    const qint64 processingCompletedNs = monotonicNowNs();
    auto completedCache = m_caches.find(key);
    if (completedCache != m_caches.end() && !completedCache->points.empty())
        completedCache->points.back().displayReadySteadyNs = processingCompletedNs;
    if (m_latencyMonitor) {
        if (scoped.ingressSteadyNs > 0) {
            m_latencyMonitor->recordNs(PipelineLatencyStage::IngressToDisplay,
                                       processingCompletedNs - scoped.ingressSteadyNs);
        }
        m_latencyMonitor->recordNs(PipelineLatencyStage::DisplayProcessing,
                                   processingCompletedNs - processingStartedNs);
    }
}

/**
 * @brief 沿全局 FIFO 淘汰有效队首点；跳过因单航迹提前删除而失效的旧索引项。
 */
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

/**
 * @brief 按最后 Unix 更新时间删除过期航迹，并同步修正总点数和交互状态。
 */
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

TrackDisplayUpdate TrackDisplayManager::buildUpdate(
    qint64 nowMs, bool forceSnapshot,
    QHash<TrackDisplayPointKey, TrackDisplayPoint>& nextPublished) const
{
    const qint64 buildStartedNs = monotonicNowNs();
    TrackDisplayUpdate update;
    update.mode = forceSnapshot
        ? TrackDisplayUpdateMode::Snapshot
        : TrackDisplayUpdateMode::Delta;
    update.timestamp = nowMs;
    update.buildStartedSteadyNs = buildStartedNs;

    struct Candidate {
        TrackDisplayPoint dp;
        qint64 ts;
        qint64 ingressSteadyNs;
        qint64 displayReadySteadyNs;
    };
    QVector<Candidate> candidates;
    candidates.reserve(m_cachedPointCount);

    for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
        const auto& key = it.key();
        const auto& cache = it.value();
        auto interaction = m_interactions.value(key);

        for (const auto& cp : cache.points) {
            TrackPacket track = cp.track;
            if (track.taskId.isEmpty() && m_taskService)
                track.taskId = m_taskService->resolveTaskIdForDevice(track.deviceId);
            if (!isTrackVisible(track))
                continue;

            TrackDisplayPoint dp;
            dp.trackId      = track.trackId;
            dp.deviceId     = track.deviceId;
            dp.taskId       = track.taskId;
            dp.timestamp    = track.receivedAtMs;
            dp.range        = track.rangeM;
            dp.azimuth      = track.azimuthRad;
            dp.elevation    = track.elevationRad;
            dp.x            = track.xM;
            dp.y            = track.yM;
            dp.z            = track.zM;
            dp.radialSpeed  = track.radialSpeedMps;
            dp.rcs          = track.rcs;
            dp.altitude     = track.altitudeM;
            dp.pointIndex   = cp.pointIndex;
            dp.interaction  = interaction;
            applyStyle(dp, cp.pointIndex, interaction);
            candidates.append({dp, track.receivedAtMs, track.ingressSteadyNs,
                               cp.displayReadySteadyNs});
        }
    }

    if (candidates.size() > kMaxPublishedPoints) {
        std::nth_element(candidates.begin(),
                         candidates.begin() + kMaxPublishedPoints,
                         candidates.end(),
                         [](const Candidate& a, const Candidate& b) { return a.ts > b.ts; });
        candidates.resize(kMaxPublishedPoints);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.ts < b.ts; });

    for (auto& c : candidates) {
        const TrackDisplayPointKey key{c.dp.deviceId, c.dp.trackId, c.dp.pointIndex};
        nextPublished.insert(key, c.dp);
        const auto previous = m_publishedPoints.constFind(key);
        if (forceSnapshot || previous == m_publishedPoints.constEnd()
            || !sameDisplayPoint(previous.value(), c.dp)) {
            update.upserts.append(std::move(c.dp));
        }
        if (c.displayReadySteadyNs > update.latestDisplayReadySteadyNs) {
            update.latestDisplayReadySteadyNs = c.displayReadySteadyNs;
            update.latestIngressSteadyNs = c.ingressSteadyNs;
        }
    }

    if (!forceSnapshot) {
        for (auto it = m_publishedPoints.constBegin(); it != m_publishedPoints.constEnd(); ++it) {
            if (!nextPublished.contains(it.key()))
                update.removals.append(it.key());
        }
    }

    update.builtSteadyNs = monotonicNowNs();
    if (m_latencyMonitor) {
        m_latencyMonitor->recordNs(PipelineLatencyStage::DisplayUpdateBuild,
                                   update.builtSteadyNs - buildStartedNs);
    }

    return update;
}

bool TrackDisplayManager::sameDisplayPoint(const TrackDisplayPoint& a,
                                           const TrackDisplayPoint& b)
{
    return a.trackId == b.trackId && a.deviceId == b.deviceId && a.taskId == b.taskId
        && a.timestamp == b.timestamp && a.range == b.range && a.azimuth == b.azimuth
        && a.elevation == b.elevation && a.altitude == b.altitude
        && a.radialSpeed == b.radialSpeed && a.pointIndex == b.pointIndex
        && a.style.color == b.style.color && a.style.size == b.style.size
        && a.style.opacity == b.style.opacity
        && a.interaction.selected == b.interaction.selected
        && a.interaction.trackingEnabled == b.interaction.trackingEnabled;
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
    publishCurrent(false);
}

/**
 * @brief 验证交互命令共享载荷后排队执行，关闭闩锁置位时丢弃迟到回调。
 */
void TrackDisplayManager::onInteractionCommand(const DataPacket& packet)
{
    if (!m_acceptCommands.load(std::memory_order_acquire))
        return;

    if (packet.command != DataCommand::TrackInteractionCommand)
        return;
    auto cmdPtr = packet.sharedPayloadAs<TrackInteractionCommand>();
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

/**
 * @brief 构建下一修订并发布不可变更新；仅在总线成功接纳后提交 revision 和窗口基线。
 * @details 失败时不推进基线并置 forceSnapshot，避免下次增量建立在客户端从未收到的状态上。
 */
void TrackDisplayManager::publishCurrent(bool forceSnapshot)
{
    QHash<TrackDisplayPointKey, TrackDisplayPoint> nextPublished;
    TrackDisplayUpdate update = buildUpdate(QDateTime::currentMSecsSinceEpoch(),
                                            forceSnapshot, nextPublished);
    update.revision = m_displayRevision + 1;
    update.publishedSteadyNs = monotonicNowNs();
    auto shared = std::make_shared<TrackDisplayUpdate>(std::move(update));
    static std::atomic<uint32_t> seq{0};
    DataPacket packet;
    packet.sourceType = SOURCE_TRACK_DISPLAY;
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    packet.packetSequence = ++seq;
    packet.timeStamp = shared ? static_cast<uint64_t>(shared->timestamp) : 0;
    packet.command = DataCommand::TrackDisplayUpdate;
    packet.payload = std::shared_ptr<const TrackDisplayUpdate>(shared);
    if (SystemBus::getInstance().publish(std::move(packet))) {
        m_publishedPoints = std::move(nextPublished);
        m_displayRevision = shared->revision;
        m_forceSnapshot = false;
    } else {
        m_forceSnapshot = true;
        qWarning() << "[TrackDisplayManager] 显示更新发布失败，下周期强制全量重同步";
    }
}
