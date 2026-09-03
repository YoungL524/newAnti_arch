/**
 * @file EventTopic.h
 * @brief 定义不可合并业务事件的有界共享日志接口。
 * @details 上游事件 Producer 在事实已经发生后调用 publish；下游 LogAlarmReader、WebReader 或 StorageReader
 * 使用独立 cursor 顺序读取。实现复用 HistoryTopic 的单份不可变对象、容量覆盖、gap 和 Doorbell 机制，
 * 但刻意不暴露 latest()，防止把最后一条事件误当当前 State；本类不进行告警状态机或持久化审计保证。
 */

#pragma once

#include "core/datadomain/HistoryTopic.h"

#include <utility>

namespace core::domain {

/**
 * @brief 保存一种不可合并事件的有界、非破坏式共享日志。
 * @tparam T 强类型事件模型；第一版 SystemEvent 的 severity 不拆分成多个 Topic。
 * @details retention 与 Reader cursor 无关；事件被覆盖时 readAfter 返回 SequenceGap。需要法规级不丢失时
 * 必须由后续持久 journal/WAL 承担，不能通过把本 Topic 改成无界容器解决。
 */
template<typename T>
class EventTopic final
{
public:
    /**
     * @brief 创建固定容量、尚无事件的共享日志。
     * @param capacity 最多保留的事件数，必须大于零。
     * @throws std::invalid_argument capacity 小于等于零时由底层 HistoryTopic 抛出。
     */
    explicit EventTopic(qsizetype capacity)
        : m_history(capacity)
    {
    }

    /**
     * @brief 追加一条已经发生且不可修改的事件事实。
     * @param event 完整事件 Sample；函数取得其所有权。
     * @return 该 EventTopic 独立 sequence 空间中的新序号。
     * @throws std::overflow_error sequence 即将耗尽时由底层 HistoryTopic 抛出。
     */
    TopicSequence publish(core::model::Sample<T> event)
    {
        return m_history.publish(std::move(event));
    }

    /**
     * @brief 非破坏式读取 lastSequence 之后仍驻留的连续事件。
     * @param lastSequence Consumer 已成功处理的最后事件序号。
     * @param output Reader 构造期定容并跨轮次复用的事件 ScratchBuffer。
     * @return 连续事件、读取水位、分页状态以及可能存在的覆盖 gap。
     */
    HistoryReadResult<T> readAfter(
        TopicSequence lastSequence,
        TopicScratchBuffer<T> &output) const
    {
        return m_history.readAfter(lastSequence, output);
    }

    /** @brief 返回当前仍可读取的事件序号闭区间，空日志使用 empty=true。 */
    TopicRange range() const
    {
        return m_history.range();
    }

    /** @brief 返回事件日志的固定容量、驻留量、覆盖累计值和有效范围。 */
    TopicStats stats() const
    {
        return m_history.stats();
    }

    /**
     * @brief 返回只携带 newestSequence 水位的非拥有型 Doorbell 指针。
     * @return 指针在 EventTopic 生命周期内有效；Consumer 收到通知后仍须调用 readAfter()。
     */
    const TopicDoorbell* doorbell() const noexcept
    {
        return m_history.doorbell();
    }

private:
    HistoryTopic<T> m_history; ///< 提供共享有界历史机制；EventTopic 有意不转发 latest()。
};

} // namespace core::domain
