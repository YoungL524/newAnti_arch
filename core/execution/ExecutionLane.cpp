#include "core/execution/ExecutionLane.h"

#include <QMetaObject>
#include <QThread>

#include <limits>
#include <stdexcept>

namespace core::execution {

/**
 * @brief 仅建立 Qt 线程亲和性和空原子位集合，不创建或占用工作线程。
 */
ExecutionLane::ExecutionLane(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 从低位到高位原子占用首个空闲 slot，使注册顺序具有确定性。
 * @details 注册不是实时热路径；线程检查防止 Consumer 在不同 Executor 上误共享一个 Lane。
 */
ExecutionSlot ExecutionLane::acquireSlot()
{
    if (QThread::currentThread() != thread()) {
        throw std::logic_error("ExecutionLane slots must be acquired on the lane thread");
    }

    quint64 observed = m_claimedMask.load(std::memory_order_acquire);
    for (;;) {
        for (ExecutionSlot slot = 0; slot < 64; ++slot) {
            const quint64 bit = quint64(1) << slot;
            if ((observed & bit) != 0) {
                continue;
            }
            if (m_claimedMask.compare_exchange_weak(observed,
                                                    observed | bit,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                return slot;
            }
            break;
        }
        if (observed == std::numeric_limits<quint64>::max()) {
            throw std::overflow_error("ExecutionLane has no free consumer slot");
        }
    }
}

/**
 * @brief 清除注册位与 pending 位，确保已销毁 Consumer 不会在后续轮次被调度。
 * @details 调用方线程错误或 slot 未占用时安全忽略；析构路径不得抛出异常。
 */
void ExecutionLane::releaseSlot(ExecutionSlot slot) noexcept
{
    if (slot >= 64 || QThread::currentThread() != thread()) {
        return;
    }
    const quint64 bit = quint64(1) << slot;
    m_pendingMask.fetch_and(~bit, std::memory_order_acq_rel);
    m_suspendedMask.fetch_and(~bit, std::memory_order_acq_rel);
    m_claimedMask.fetch_and(~bit, std::memory_order_acq_rel);
}

/**
 * @brief 仅对仍已注册的 slot 置 pending 位，并竞争唯一 Qt 唤醒令牌。
 * @details 重复请求的 fetch_or 不增加队列项；即使请求发生在 drainPending 发信期间，也会由结束时复查捕获。
 */
void ExecutionLane::request(ExecutionSlot slot) noexcept
{
    if (slot >= 64) {
        return;
    }
    const quint64 bit = quint64(1) << slot;
    if ((m_claimedMask.load(std::memory_order_acquire) & bit) == 0) {
        return;
    }
    m_pendingMask.fetch_or(bit, std::memory_order_release);
    scheduleWake();
}

/**
 * @brief 读取已执行唤醒轮数，不建立额外跨线程同步关系。
 */
quint64 ExecutionLane::executedWakeCount() const noexcept
{
    return m_executedWakeCount.load(std::memory_order_relaxed);
}

/**
 * @brief 暂停位只在执行线程修改；保留 pending，让故障期间任意数量的 Doorbell 合并而不触发 SQL。
 */
void ExecutionLane::suspend(ExecutionSlot slot) noexcept
{
    if (slot >= 64 || QThread::currentThread() != thread()) return;
    const quint64 bit = quint64(1) << slot;
    if ((m_claimedMask.load(std::memory_order_acquire) & bit) != 0)
        m_suspendedMask.fetch_or(bit, std::memory_order_acq_rel);
}

/**
 * @brief 解除暂停后主动补偿一次请求，无新 Doorbell 的尾部数据也可重试；不持有业务数据。
 */
void ExecutionLane::resume(ExecutionSlot slot) noexcept
{
    if (slot >= 64 || QThread::currentThread() != thread()) return;
    m_suspendedMask.fetch_and(~(quint64(1) << slot), std::memory_order_acq_rel);
    request(slot);
}

/**
 * @brief 原子取走本轮可执行 pending 位，暂停位保留；同一线程通知 Consumer，再补偿运行期间的新请求。
 * @details executeRequested 不携带数据；Consumer 自己读取 Topic。先释放唤醒令牌再复查 pending，避免请求
 * 落在“已取走旧位但尚未允许新唤醒”的竞态窗口中。
 */
void ExecutionLane::drainPending()
{
    const quint64 runnable = m_claimedMask.load(std::memory_order_acquire)
        & ~m_suspendedMask.load(std::memory_order_acquire);
    const quint64 pending = m_pendingMask.fetch_and(~runnable, std::memory_order_acq_rel) & runnable;
    m_executedWakeCount.fetch_add(1, std::memory_order_relaxed);
    if (pending != 0) {
        Q_EMIT executeRequested(pending);
    }

    m_wakeScheduled.store(false, std::memory_order_release);
    if (m_pendingMask.load(std::memory_order_acquire) != 0) {
        scheduleWake();
    }
}

/**
 * @brief 通过 queued invoke 把一次合并运行切到 Lane 所属线程。
 * @details 原子令牌在 invoke 前取得，确保并发 request 只能共享该唤醒；对象销毁期安排失败时释放令牌。
 */
void ExecutionLane::scheduleWake() noexcept
{
    if ((m_pendingMask.load(std::memory_order_acquire)
         & m_claimedMask.load(std::memory_order_acquire)
         & ~m_suspendedMask.load(std::memory_order_acquire)) == 0) return;
    bool expected = false;
    if (!m_wakeScheduled.compare_exchange_strong(expected,
                                                 true,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
        return;
    }

    if (!QMetaObject::invokeMethod(this, "drainPending", Qt::QueuedConnection)) {
        m_wakeScheduled.store(false, std::memory_order_release);
    }
}

} // namespace core::execution
