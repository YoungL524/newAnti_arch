#include "core/datadomain/TopicDoorbell.h"

#include <QMetaObject>

namespace core::domain {

/**
 * @brief 将 QObject 线程亲和性固定在创建线程，供该线程的事件循环发送合并通知。
 * @details parent 只参与 Qt 生命周期；Doorbell 不拥有 Topic，也不会创建工作线程。
 */
TopicDoorbell::TopicDoorbell(QObject* parent)
    : QObject(parent)
{
}

/**
 * @brief 仅在水位确实前进时更新原子最大值，并安排一次异步通知。
 * @details 先发布最大水位再安排 flush，确保 Consumer 收到提示时对应数据已经完成提交；并发 Producer
 * 只竞争两个原子量，不会进入 Topic 的数据临界区。
 */
void TopicDoorbell::ring(quint64 newestVersion) noexcept
{
    quint64 observed = m_newestVersion.load(std::memory_order_relaxed);
    bool advanced = false;
    while (observed < newestVersion) {
        if (m_newestVersion.compare_exchange_weak(observed,
                                                  newestVersion,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
            advanced = true;
            break;
        }
    }

    if (advanced) {
        scheduleFlush();
    }
}

/**
 * @brief 使用 queued invoke 把信号发送切回 Doorbell 所在线程，并合并尚未执行的重复安排。
 * @details invoke 安排失败通常表示对象正在销毁；此时恢复标记，让后续仍存活的调用可以重试。数据仍
 * 保存在 Topic 中，Doorbell 的 best-effort 失败不会改变读取正确性。
 */
void TopicDoorbell::scheduleFlush() noexcept
{
    bool expected = false;
    if (!m_flushScheduled.compare_exchange_strong(expected,
                                                  true,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        return;
    }

    const bool scheduled = QMetaObject::invokeMethod(
        this,
        [this]() {
            flush();
        },
        Qt::QueuedConnection);

    if (!scheduled) {
        m_flushScheduled.store(false, std::memory_order_release);
    }
}

/**
 * @brief 发出读取前已合并的最大水位，并在通知期间又有写入时重新安排一次提示。
 * @details 先发送再释放 scheduled 标记，可最大限度合并突发写入；释放后重新检查水位用于修复 Producer
 * 在发信窗口内只更新版本、未能自行安排下一次 flush 的竞态。
 */
void TopicDoorbell::flush()
{
    const quint64 emittedVersion = m_newestVersion.load(std::memory_order_acquire);
    Q_EMIT advanced(emittedVersion);

    m_flushScheduled.store(false, std::memory_order_release);
    if (m_newestVersion.load(std::memory_order_acquire) > emittedVersion) {
        scheduleFlush();
    }
}

} // namespace core::domain

