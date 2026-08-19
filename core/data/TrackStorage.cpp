#include "TrackStorage.h"

#include "DataStore.h"
#include "SystemBus.h"
#include "ThreadSafeQueue.h"

#include <QDebug>
#include <QVariantMap>
#include <algorithm>

namespace {
using TrackPtr = std::shared_ptr<const TrackPacket>;

/**
 * @brief 把不可变 TrackPacket 投影为 tracks 表的一行，并保持字段单位不变。
 * @details 该函数是航迹模型到数据库列名的唯一映射点；新增持久字段时应同步迁移表结构和历史查询 DTO。
 */
QVariantMap toTrackRow(const TrackPacket& track)
{
    QVariantMap row;
    row.insert(QStringLiteral("ts_ms"), track.receivedAtMs);
    row.insert(QStringLiteral("source"), track.endpointId);
    row.insert(QStringLiteral("task_id"), track.taskId);
    row.insert(QStringLiteral("device_id"), track.deviceId);
    row.insert(QStringLiteral("scene_num"), track.sceneNumber);
    row.insert(QStringLiteral("target_id"), track.trackId);
    row.insert(QStringLiteral("range_m"), track.rangeM);
    row.insert(QStringLiteral("azimuth_rad"), track.azimuthRad);
    row.insert(QStringLiteral("elevation_rad"), track.elevationRad);
    row.insert(QStringLiteral("radial_speed"), track.radialSpeedMps);
    row.insert(QStringLiteral("altitude_m"), track.altitudeM);
    row.insert(QStringLiteral("x"), track.xM);
    row.insert(QStringLiteral("y"), track.yM);
    row.insert(QStringLiteral("z"), track.zM);
    return row;
}

/**
 * @brief 使用 compare-exchange 原子地把队列历史高水位提升到当前深度。
 */
void updateHighWatermark(std::atomic<size_t>& highWatermark, size_t depth)
{
    size_t previous = highWatermark.load(std::memory_order_relaxed);
    while (depth > previous
           && !highWatermark.compare_exchange_weak(previous, depth,
                                                   std::memory_order_relaxed)) {
    }
}
}

struct TrackStorage::State {
    explicit State(const Options& value)
        : options(value)
        , queue(value.queueCapacity)
    {
    }

    Options options;
    ThreadSafeQueue<TrackPtr> queue;
    std::atomic<bool> accepting{false};
    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> persisted{0};
    std::atomic<uint64_t> queueFullDrops{0};
    std::atomic<uint64_t> unassignedSkips{0};
    std::atomic<uint64_t> writeFailures{0};
    std::atomic<size_t> highWatermark{0};
};

/**
 * @brief 规范化队列容量、批量大小和刷新周期，避免零值导致永不处理或无限循环。
 */
TrackStorage::TrackStorage(Options options)
    : m_options(options)
{
    m_options.queueCapacity = std::max<size_t>(1, m_options.queueCapacity);
    m_options.batchSize = std::max<size_t>(1, m_options.batchSize);
    m_options.flushIntervalMs = std::max(1, m_options.flushIntervalMs);
}

/**
 * @brief 委托到默认 Options 创建 4096 容量、100 行批量、200 ms 刷新的归档器。
 */
TrackStorage::TrackStorage()
    : TrackStorage(Options{})
{
}

/** @brief 通过 stop() 取消订阅并 join 存储线程，禁止后台线程越过对象生命周期。 */
TrackStorage::~TrackStorage()
{
    stop();
}

/**
 * @brief 先启动并验证线程私有数据库连接，再订阅 Track 消息。
 * @details promise/future 使调用线程等待初始化结果；只有数据库可用才登记总线回调。回调拒绝空任务归属，
 *          并在锁内检查容量后入队；队列满只增加 drops，绝不阻塞 BusWorker。
 */
bool TrackStorage::start(const QJsonObject& resolvedDatabaseConfig)
{
    if (isRunning())
        return true;
    if (resolvedDatabaseConfig.isEmpty())
        return false;

    m_lastStats = Stats{};
    m_state = std::make_shared<State>(m_options);
    auto initialized = std::make_shared<std::promise<bool>>();
    auto initializedFuture = initialized->get_future();
    m_worker = std::thread(&TrackStorage::storageLoop, m_state,
                           resolvedDatabaseConfig, initialized);

    if (!initializedFuture.get()) {
        m_worker.join();
        m_state.reset();
        qWarning() << QStringLiteral("[TrackStorage] 线程私有数据库连接失败，航迹归档已禁用");
        return false;
    }

    const std::weak_ptr<State> weakState = m_state;
    m_state->accepting.store(true, std::memory_order_release);
    m_subscriptionId = SystemBus::getInstance().subscribeAll(
        [weakState](const DataPacket* packet) {
            const auto state = weakState.lock();
            if (!state || !state->accepting.load(std::memory_order_acquire)
                || !packet || packet->command != DataCommand::Track) {
                return;
            }

            const TrackPtr track = packet->sharedPayloadAs<TrackPacket>();
            if (!track)
                return;
            if (track->taskId.isEmpty()) {
                state->unassignedSkips.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (!state->queue.enqueue(track)) {
                const uint64_t dropped = state->queueFullDrops.fetch_add(
                                             1, std::memory_order_relaxed) + 1;
                if (dropped == 1 || dropped % 100 == 0) {
                    qWarning() << QStringLiteral("[TrackStorage] 存储队列已满，丢弃归档副本: dropped=%1 capacity=%2")
                                      .arg(dropped).arg(state->options.queueCapacity);
                }
                return;
            }

            state->accepted.fetch_add(1, std::memory_order_relaxed);
            updateHighWatermark(state->highWatermark, state->queue.size());
        });

    if (m_subscriptionId == 0) {
        m_state->accepting.store(false, std::memory_order_release);
        m_state->stopping.store(true, std::memory_order_release);
        m_worker.join();
        m_state.reset();
        qWarning() << QStringLiteral("[TrackStorage] SystemBus 订阅失败，航迹归档已禁用");
        return false;
    }

    qDebug() << QStringLiteral("[TrackStorage] 已启动: capacity=%1 batch=%2 flush=%3ms")
                    .arg(m_options.queueCapacity).arg(m_options.batchSize)
                    .arg(m_options.flushIntervalMs);
    return true;
}

/**
 * @brief 先取消总线订阅阻止新记录，再设置停止标志、唤醒并 join 工作线程。
 * @details join 前保存共享 State，线程退出后将最终统计复制到 m_lastStats，随后释放线程状态和数据库连接。
 */
void TrackStorage::stop()
{
    const auto state = m_state;
    if (!state)
        return;

    state->accepting.store(false, std::memory_order_release);
    if (m_subscriptionId != 0) {
        SystemBus::getInstance().unsubscribe(m_subscriptionId);
        m_subscriptionId = 0;
    }
    state->stopping.store(true, std::memory_order_release);

    if (m_worker.joinable())
        m_worker.join();

    const Stats finalStats = stats();
    m_lastStats = finalStats;
    qDebug() << QStringLiteral("[TrackStorage] 已停止: accepted=%1 persisted=%2 queueDrops=%3 writeFailures=%4")
                    .arg(finalStats.accepted).arg(finalStats.persisted)
                    .arg(finalStats.queueFullDrops).arg(finalStats.writeFailures);
    m_state.reset();
}

/**
 * @brief 原子读取共享状态的 running 标志；未启动或 stop 后返回 false。
 */
bool TrackStorage::isRunning() const
{
    return m_state && m_subscriptionId != 0
        && m_state->accepting.load(std::memory_order_acquire);
}

/**
 * @brief 组合原子累计值与互斥锁保护的即时队列深度，形成诊断快照。
 */
TrackStorage::Stats TrackStorage::stats() const
{
    Stats result;
    const auto state = m_state;
    if (!state)
        return m_lastStats;

    result.accepted = state->accepted.load(std::memory_order_relaxed);
    result.persisted = state->persisted.load(std::memory_order_relaxed);
    result.queueFullDrops = state->queueFullDrops.load(std::memory_order_relaxed);
    result.unassignedSkips = state->unassignedSkips.load(std::memory_order_relaxed);
    result.writeFailures = state->writeFailures.load(std::memory_order_relaxed);
    result.queueDepth = state->queue.size();
    result.queueCapacity = state->options.queueCapacity;
    result.highWatermark = state->highWatermark.load(std::memory_order_relaxed);
    return result;
}

/**
 * @brief 在线程私有 DataStore 上按“首条等待时间或批量上限”持续归档队列记录。
 * @details 初始化结果先通过 promise 返回 start()；SQLite 设置 1 秒 busy_timeout，仅阻塞可选存储线程。
 *          停止时继续排空队列和当前批次后退出。批量写失败会丢弃该批归档副本并计数，不重试阻塞实时链路。
 */
void TrackStorage::storageLoop(const std::shared_ptr<State>& state,
                               const QJsonObject& databaseConfig,
                               std::shared_ptr<std::promise<bool>> initialized)
{
    DataStore store;
    const bool connected = store.connectFromConfig(databaseConfig);
    if (connected && store.database().driverName().compare(
                         QStringLiteral("QSQLITE"), Qt::CaseInsensitive) == 0) {
        // 阻塞只发生在可选存储线程，不会拖住显示主链路。
        store.execute(QStringLiteral("PRAGMA busy_timeout=1000"));
    }
    initialized->set_value(connected);
    if (!connected)
        return;

    QVector<QVariantMap> rows;
    rows.reserve(static_cast<int>(state->options.batchSize));

    while (true) {
        TrackPtr track;
        const bool received = state->queue.dequeue(track, state->options.flushIntervalMs);
        if (received && track)
            rows.append(toTrackRow(*track));

        TrackPtr additional;
        while (rows.size() < static_cast<int>(state->options.batchSize)
               && state->queue.tryDequeue(additional)) {
            if (additional)
                rows.append(toTrackRow(*additional));
        }

        const bool timedFlush = !received && !rows.isEmpty();
        const bool fullBatch = rows.size() >= static_cast<int>(state->options.batchSize);
        const bool draining = state->stopping.load(std::memory_order_acquire);
        if ((timedFlush || fullBatch || draining) && !rows.isEmpty()) {
            const int count = rows.size();
            if (store.insertBatch(QStringLiteral("tracks"), rows)) {
                state->persisted.fetch_add(static_cast<uint64_t>(count),
                                           std::memory_order_relaxed);
            } else {
                state->writeFailures.fetch_add(static_cast<uint64_t>(count),
                                               std::memory_order_relaxed);
                qWarning() << QStringLiteral("[TrackStorage] 批量写入失败，已丢弃本批归档副本: %1 条")
                                  .arg(count);
            }
            rows.clear();
        }

        if (draining && state->queue.isEmpty() && rows.isEmpty())
            break;
    }

    store.disconnect();
}
