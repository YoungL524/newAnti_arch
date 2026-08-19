/**
 * @file TrackStorage.h
 * @brief 声明权威 TrackPacket 总线之后的有界异步归档分支。
 * @details TrackStorage 订阅 DataCommand::Track，但总线回调只检查类型和任务归属后尝试非阻塞入队；
 *          独立 std::thread 拥有自己的数据库连接，按数量或等待时间组成批次写入 tracks 表。
 *          队列满只丢弃归档副本并计数，不阻塞解析、识别、显示或 Web 实时主链路。
 */

#pragma once

#include "GenericPackets.h"
#include "TrackTypes.h"

#include <QJsonObject>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>

/**
 * @brief 标准航迹的异步、有界持久化消费者。
 * @details SystemBus 回调只做类型检查和非阻塞入队；数据库连接、攒批和写入
 *          全部发生在专属线程。存储拥塞只会丢弃归档副本并计数，不会反压
 *          解析与显示主链路。
 */
class TrackStorage
{
public:
    struct Options {
        size_t queueCapacity = 4096; ///< 最大待落库航迹数；满时新归档副本被丢弃。
        size_t batchSize = 100;      ///< 达到该数量立即提交一次数据库批量写入。
        int flushIntervalMs = 200;   ///< 未满批时允许首条记录等待的最长毫秒数。
    };

    struct Stats {
        uint64_t accepted = 0;        ///< 总线回调成功放入存储队列的累计数量。
        uint64_t persisted = 0;       ///< 数据库确认批量写入成功的累计行数。
        uint64_t queueFullDrops = 0;  ///< 因队列容量耗尽而放弃归档的累计数量。
        uint64_t unassignedSkips = 0; ///< taskId 为空、无法按任务归档而跳过的累计数量。
        uint64_t writeFailures = 0;   ///< 批量事务失败的累计批次数。
        size_t queueDepth = 0;        ///< 采样时仍等待工作线程处理的记录数。
        size_t queueCapacity = 0;     ///< 当前实例配置的固定队列容量。
        size_t highWatermark = 0;     ///< 本次运行达到过的最大队列深度。
    };

    /** @brief 使用默认容量、批量和刷新周期创建未启动的归档器。 */
    TrackStorage();
    /** @brief 使用显式背压参数创建未启动的归档器。 */
    explicit TrackStorage(Options options);
    /** @brief 调用 stop()，确保订阅取消、队列唤醒且数据库线程已回收。 */
    ~TrackStorage();

    TrackStorage(const TrackStorage&) = delete;
    TrackStorage& operator=(const TrackStorage&) = delete;

    /**
     * @brief 创建存储线程、在线程内连接数据库，成功后再订阅 SystemBus。
     * @return 数据库线程初始化和总线订阅均成功时返回 true；失败时不留下后台线程。
     */
    bool start(const QJsonObject& resolvedDatabaseConfig);
    /** @brief 先取消订阅阻止新记录，再唤醒并等待工作线程处理关闭流程；可重复调用。 */
    void stop();

    /** @brief 返回工作线程是否仍接受归档记录。 */
    bool isRunning() const;
    /** @brief 返回原子计数和锁内队列深度组成的一次性诊断快照。 */
    Stats stats() const;

private:
    struct State;
    static void storageLoop(const std::shared_ptr<State>& state,
                            const QJsonObject& databaseConfig,
                            std::shared_ptr<std::promise<bool>> initialized);

    Options m_options;                  ///< 构造时固定的队列和批量策略。
    std::shared_ptr<State> m_state;
    std::thread m_worker;
    uint32_t m_subscriptionId = 0;      ///< Track 总线订阅 ID；0 表示未启动。
    Stats m_lastStats;                  ///< stop 后保留最后快照，供线程销毁后继续诊断。
};
