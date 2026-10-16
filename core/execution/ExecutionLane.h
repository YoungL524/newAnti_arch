/**
 * @file ExecutionLane.h
 * @brief 定义在一个 Qt Executor 上合并多个 Consumer 运行请求的轻量调度通道。
 * @details 上游 Doorbell 或 Consumer 续读只调用 request() 置位；ExecutionLane 在所属事件循环中合并唤醒并
 * 发出待运行位集合，下游 Consumer 只响应自己的 slot。它不保存 Sample、cursor、业务回调或 Consumer
 * Queue，也不创建线程；新人应从 acquireSlot()、request() 和 executeRequested() 的位语义开始阅读。
 */

#pragma once

#include <QObject>
#include <QtGlobal>

#include <atomic>

namespace core::execution {

/** @brief 标识一个 ExecutionLane 内的 Consumer 调度位，取值范围为 0 至 63。 */
using ExecutionSlot = quint8;

/**
 * @brief 把任意线程的 Consumer 运行请求合并到所属 Qt 事件循环的一次唤醒中。
 * @details 一个 Lane 可串行调度最多 64 个有状态 Consumer；Consumer 必须与 Lane 具有相同线程亲和性，
 * Producer 和 Doorbell 可以从其他线程调用 request()。对象销毁前必须停止新的请求。
 */
class ExecutionLane final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 创建尚未注册 Consumer 且没有待运行位的调度通道。
     * @param parent Qt 生命周期父对象；Lane 不拥有线程或 Consumer。
     */
    explicit ExecutionLane(QObject *parent = nullptr);

    /** @brief 禁止复制 QObject、原子调度状态和已分配 slot。 */
    ExecutionLane(const ExecutionLane&) = delete;

    /** @brief 禁止复制赋值，避免两个 Lane 共享不一致的 pending 位。 */
    ExecutionLane& operator=(const ExecutionLane&) = delete;

    /**
     * @brief 为一个 Consumer 分配当前 Lane 内唯一的调度位。
     * @return 范围为 [0,63] 的 slot；Consumer 析构时必须调用 releaseSlot()。
     * @throws std::overflow_error 64 个调度位全部占用时抛出。
     * @warning 只允许在 Lane 所属线程的组合或销毁阶段调用。
     */
    ExecutionSlot acquireSlot();

    /**
     * @brief 释放 Consumer 调度位并清除尚未执行的对应请求。
     * @param slot 先前由本 Lane 分配且尚未释放的 slot。
     * @warning 只允许在 Lane 所属线程调用；释放前 Consumer 应先断开 executeRequested 连接。
     */
    void releaseSlot(ExecutionSlot slot) noexcept;

    /**
     * @brief 从任意线程请求运行指定 Consumer，并合并重复请求。
     * @param slot 本 Lane 当前已分配的 Consumer slot；无效或已释放 slot 被忽略。
     * @note 本函数只设置原子位并至多安排一个 Qt 唤醒，不读取 Topic 或执行 Consumer。
     */
    void request(ExecutionSlot slot) noexcept;

    /** @brief 在 Lane 线程暂停指定 slot 的执行；跨线程 request 仍只合并一个 pending 位，不积压唤醒。 */
    void suspend(ExecutionSlot slot) noexcept;

    /** @brief 在 Lane 线程解除暂停并补偿一次读取；故障重试时机由外部运行时拥有，不由 Consumer 轮询。 */
    void resume(ExecutionSlot slot) noexcept;

    /**
     * @brief 返回已经进入 Lane 事件循环的累计唤醒轮数。
     * @return 单调观测值，用于验证突发请求是否被合并；不参与业务正确性。
     */
    quint64 executedWakeCount() const noexcept;

Q_SIGNALS:
    /**
     * @brief 在 Lane 所属线程通知 Consumer 执行一个有界轮次。
     * @param pendingMask 本轮取走的 Consumer 位集合；不包含 Sample、cursor 或 Topic 水位。
     * @note Consumer 仅在自身 slot 对应位存在时运行；同一 Lane 的连接必须保持 Direct/Auto 同线程执行。
     */
    void executeRequested(quint64 pendingMask);

private Q_SLOTS:
    /**
     * @brief 在 Lane 所属事件循环取走 pending 位并串行通知对应 Consumer。
     * @details 发信期间到达的新请求保留在下一轮位集合中；本轮结束后仅在确有 pending 位时再安排一次唤醒。
     */
    void drainPending();

private:
    /**
     * @brief 使用原子令牌保证事件循环内同时最多存在一个待执行 Lane 唤醒。
     * @note 安排失败会释放令牌；数据仍在 Topic 中，可由后续 Doorbell 或显式 refresh 再次请求。
     */
    void scheduleWake() noexcept;

    std::atomic<quint64> m_claimedMask {0}; ///< 已分配 slot 位集合，不包含 Consumer 指针或所有权。
    std::atomic<quint64> m_pendingMask {0}; ///< 尚未取走的运行请求；重复 slot 请求自动合并。
    std::atomic<quint64> m_suspendedMask {0}; ///< 故障退避中的调度位；只阻止执行，不清除已合并的更新提示。
    std::atomic_bool m_wakeScheduled {false}; ///< true 表示 Lane 事件循环已有一个待执行唤醒。
    std::atomic<quint64> m_executedWakeCount {0}; ///< 已进入 drainPending 的累计轮数，仅供指标和测试。
};

} // namespace core::execution
