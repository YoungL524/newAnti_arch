/**
 * @file TopicDoorbell.h
 * @brief 定义共享数据对象更新后的轻量 Qt 通知边界。
 * @details 上游 Topic 在完成写入并释放数据锁后调用 ring，下游 Consumer 收到 advanced 后仍需主动调用
 * latest() 或 readAfter() 取数。Doorbell 允许合并、重复和延迟，不携带业务载荷、不保存历史，也不是
 * Consumer Mailbox；新人应结合 HistoryTopic::publish() 的解锁后调用顺序阅读。
 */

#pragma once

#include <QObject>
#include <QtGlobal>

#include <atomic>

namespace core::domain {

/**
 * @brief 将任意 Producer 线程报告的最新水位合并后，在对象所属 Qt 线程发出提示。
 * @details 对象通常由组合根在线程具有事件循环时创建。数据正确性不能依赖每次 publish 都对应一次
 * advanced；Consumer 必须把信号视为“可能有更新”的 Doorbell。
 */
class TopicDoorbell final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 创建尚未报告任何水位的 Doorbell。
     * @param parent Qt 生命周期父对象；为空时由所属 Topic 的生命周期管理。
     */
    explicit TopicDoorbell(QObject* parent = nullptr);

    /**
     * @brief 从任意线程报告已经完成写入的最新 sequence 或 revision。
     * @param newestVersion 已经可从共享数据对象读取的水位，必须单调增加；过期或重复值被忽略。
     * @note 本函数不读取业务数据，只以原子操作合并水位并向 Doorbell 所在线程安排一次 flush。
     */
    void ring(quint64 newestVersion) noexcept;

Q_SIGNALS:
    /**
     * @brief 提醒 Consumer 共享数据对象可能已经推进到指定水位。
     * @param newestVersion 已合并的最新 sequence 或 revision；Consumer 仍应以实际读取结果为准。
     */
    void advanced(quint64 newestVersion);

private:
    /**
     * @brief 至多安排一个等待事件循环执行的 flush。
     * @note 可从任意线程调用；安排失败时只丢失提示，不影响 Topic 中已经提交的数据。
     */
    void scheduleFlush() noexcept;

    /**
     * @brief 在 Doorbell 所在线程发出当前最大水位，并补偿发信期间到达的新水位。
     * @warning 只允许由已排入本对象事件循环的调用执行。
     */
    void flush();

    std::atomic<quint64> m_newestVersion {0}; ///< 已报告的最大水位，不包含任何业务数据。
    std::atomic_bool m_flushScheduled {false}; ///< true 表示事件循环中已有一次合并通知等待执行。
};

} // namespace core::domain

