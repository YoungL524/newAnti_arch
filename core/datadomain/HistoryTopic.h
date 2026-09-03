/**
 * @file HistoryTopic.h
 * @brief 实现单一强类型业务事实的共享有界 RingBuffer 与游标读取接口。
 * @details 上游 Producer 把已经校验和映射完成的 Sample 发布到本 Topic；下游 WebReader、
 * StorageReader、WebReader 与 PluginReader 通过 latest() 或 readAfter() 把有界记录复制到自身短期缓冲区。
 * 本类不登记 Consumer、不保存其 cursor、不执行业务回调，也不负责协议解析、数据库、显示或 Command；
 * 新人应先阅读 publish() 的锁外 Doorbell 顺序和 readAfter() 的 Gap/Ahead 判定。
 */

#pragma once

#include "core/datadomain/TopicDoorbell.h"
#include "core/datadomain/TopicTypes.h"

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace core::domain {

/**
 * @brief 保存一种业务模型的有界、非破坏式共享历史。
 * @tparam T Sample 中的强类型业务模型。
 * @details Topic 构造时一次性分配全部内嵌槽位；publish 不创建 Sample 堆对象，Reader 把有界记录复制到
 * 自己固定且复用的 TopicScratchBuffer。慢 Reader 不阻塞覆盖，落后时通过 SequenceGap 得知丢失范围。
 * @warning Topic、其 Doorbell 及所有并发调用者的生命周期必须由组合根协调；销毁时不得仍有 publish 或
 * readAfter 正在执行，且应在 Doorbell 的创建线程销毁对象。
 */
template<typename T>
class HistoryTopic final
{
public:
    /**
     * @brief 创建固定容量且尚无记录的共享历史。
     * @param capacity Topic 自身最多持有的记录数，必须大于零，构造后不可改变。
     * @throws std::invalid_argument capacity 小于等于零时抛出；不会创建退化的无界或零容量 Topic。
     */
    explicit HistoryTopic(qsizetype capacity)
        : m_capacity(validateCapacity(capacity))
        , m_slots(static_cast<std::size_t>(m_capacity))
    {
    }

    /** @brief 禁止复制包含互斥量、RingBuffer 和 QObject Doorbell 的 Topic。 */
    HistoryTopic(const HistoryTopic&) = delete;

    /** @brief 禁止复制赋值，避免两个 Topic 错误共享序号空间。 */
    HistoryTopic& operator=(const HistoryTopic&) = delete;

    /** @brief 禁止移动，保证 Doorbell 的地址和 Qt 线程亲和性在生命周期内稳定。 */
    HistoryTopic(HistoryTopic&&) = delete;

    /** @brief 禁止移动赋值，防止运行中的 Reader 持有失效 Doorbell 地址。 */
    HistoryTopic& operator=(HistoryTopic&&) = delete;

    /**
     * @brief 追加一个不可变 Sample，并在容量已满时覆盖当前最老槽位。
     * @param sample 已完成协议校验、标准映射和同步处理的业务事实；函数取得其所有权。
     * @return 本次追加分配的 Topic 内部 sequence，从 1 开始且仅属于该 Topic。
     * @throws std::overflow_error sequence 即将耗尽时抛出，防止回绕后破坏 cursor 关系。
     * @note 锁内只覆盖一个预分配槽位并更新水位，解锁后才报告 Doorbell。T 必须遵守数据域定长合同，
     * 不得通过动态成员把隐藏分配或复杂析构带入 publish。
     */
    TopicSequence publish(core::model::Sample<T> sample)
    {
        TopicSequence publishedSequence = 0;

        {
            QMutexLocker locker(&m_mutex);
            if (m_nextSequence == std::numeric_limits<TopicSequence>::max()) {
                throw std::overflow_error("HistoryTopic sequence exhausted");
            }

            publishedSequence = m_nextSequence++;
            TopicRecord<T> record {publishedSequence, std::move(sample)};

            if (m_size < m_capacity) {
                const qsizetype writeIndex = (m_head + m_size) % m_capacity;
                m_slots[static_cast<std::size_t>(writeIndex)] = std::move(record);
                ++m_size;
            } else {
                m_slots[static_cast<std::size_t>(m_head)] = std::move(record);
                m_head = (m_head + 1) % m_capacity;
                ++m_overwrittenRecords;
            }
        }

        m_doorbell.ring(publishedSequence);
        return publishedSequence;
    }

    /**
     * @brief 把读取瞬间最新记录复制到调用方提供的短期对象。
     * @param output 接收最新记录；Topic 为空时保持原值不变。
     * @return Topic 有记录时返回 true，否则返回 false。
     */
    bool latest(TopicRecord<T> &output) const
    {
        QMutexLocker locker(&m_mutex);
        if (m_size == 0) {
            return false;
        }

        const qsizetype latestIndex = (m_head + m_size - 1) % m_capacity;
        output = m_slots[static_cast<std::size_t>(latestIndex)];
        return true;
    }

    /**
     * @brief 非破坏式读取 lastSequence 之后仍驻留在窗口中的连续记录。
     * @param lastSequence Consumer 已成功处理的最后一个 Topic sequence；函数不会保存或修改它。
     * @param output Reader 构造期定容并跨轮次复用的缓冲区；其容量就是本批硬上限。
     * @return 按序记录、读取瞬间水位、分页标志以及可能的 Gap/Ahead 关系。
     * @note Topic 为空且 cursor 为零返回 Continuous；空 Topic 的非零 cursor 与大于 newest 的 cursor
     * 返回 Ahead；落后窗口时报告完整缺口并从当前 oldest 开始返回。
     */
    HistoryReadResult<T> readAfter(
        TopicSequence lastSequence,
        TopicScratchBuffer<T> &output) const
    {
        QMutexLocker locker(&m_mutex);
        HistoryReadResult<T> result;
        if (m_size == 0) {
            result.relation = lastSequence == 0 ? CursorRelation::Continuous : CursorRelation::Ahead;
            return result;
        }

        const TopicSequence oldestSequence = m_slots[static_cast<std::size_t>(m_head)].sequence;
        const TopicSequence newestSequence = m_nextSequence - 1;
        result.newestAtRead = newestSequence;

        if (lastSequence > newestSequence) {
            result.relation = CursorRelation::Ahead;
            return result;
        }

        if (lastSequence == newestSequence) {
            return result;
        }

        TopicSequence firstSequence = lastSequence + 1;
        if (firstSequence < oldestSequence) {
            result.relation = CursorRelation::Gap;
            result.gap = SequenceGap {firstSequence, oldestSequence - 1};
            firstSequence = oldestSequence;
        }

        if (firstSequence > newestSequence) {
            return result;
        }

        const qsizetype available = static_cast<qsizetype>(newestSequence - firstSequence + 1);
        const qsizetype returnCount = std::min(available, output.capacity());

        const qsizetype firstOffset = static_cast<qsizetype>(firstSequence - oldestSequence);
        for (qsizetype index = 0; index < returnCount; ++index) {
            const qsizetype slotIndex = (m_head + firstOffset + index) % m_capacity;
            output.data()[index] = m_slots[static_cast<std::size_t>(slotIndex)];
        }

        result.count = returnCount;
        result.hasMore = returnCount < available;
        return result;
    }

    /**
     * @brief 返回同一次短锁快照中的当前有效序号闭区间。
     * @return 从未发布时 empty=true 且边界为零；否则为 [oldest, newest]。
     */
    TopicRange range() const
    {
        QMutexLocker locker(&m_mutex);
        return rangeLocked();
    }

    /**
     * @brief 返回容量、槽位内存、驻留量、累计覆盖数和有效范围的一致快照。
     * @return 只描述 Topic 自身 retention，不包含任何 Reader cursor 或积压量。
     */
    TopicStats stats() const
    {
        QMutexLocker locker(&m_mutex);
        return TopicStats {
            m_capacity,
            m_size,
            static_cast<quint64>(m_capacity) * sizeof(TopicRecord<T>),
            m_overwrittenRecords,
            rangeLocked()};
    }

    /**
     * @brief 返回仅用于更新提示的非拥有型 Doorbell 指针。
     * @return 指针在 HistoryTopic 生命周期内有效；不得删除、缓存业务数据或据此假设逐条通知。
     */
    const TopicDoorbell* doorbell() const noexcept
    {
        return &m_doorbell;
    }

private:
    /**
     * @brief 在分配 RingBuffer 前验证容量，避免负数转换为巨大 size_t。
     * @param capacity 调用者请求的固定槽位数。
     * @return 原样返回合法正容量，供构造初始化列表复用。
     */
    static qsizetype validateCapacity(qsizetype capacity)
    {
        if (capacity <= 0) {
            throw std::invalid_argument("HistoryTopic capacity must be greater than zero");
        }
        return capacity;
    }

    /**
     * @brief 在调用者已经持有 m_mutex 时构造有效范围，避免 stats() 二次加锁。
     * @return 与当前槽位、水位和 m_size 一致的范围快照。
     * @pre 当前线程已持有 m_mutex。
     */
    TopicRange rangeLocked() const
    {
        if (m_size == 0) {
            return TopicRange {};
        }
        return TopicRange {
            m_slots[static_cast<std::size_t>(m_head)].sequence,
            m_nextSequence - 1,
            false};
    }

    const qsizetype m_capacity;              ///< Topic 固定槽位数；构造后不随 Reader 或负载改变。
    mutable QMutex m_mutex;                  ///< 仅保护槽位、索引、sequence 和覆盖统计的短临界区。
    std::vector<TopicRecord<T>> m_slots;     ///< 构造期预分配的内嵌记录槽位，运行期容量不改变。
    qsizetype m_head = 0;                    ///< 当前最老有效记录所在槽位下标。
    qsizetype m_size = 0;                    ///< 当前有效槽位数量，最大为 m_capacity。
    TopicSequence m_nextSequence = 1;        ///< 下一次成功 publish 预留的 Topic 内部序号。
    quint64 m_overwrittenRecords = 0;        ///< 容量满后覆盖最老记录的累计次数。
    TopicDoorbell m_doorbell;                ///< 解锁后触发的 best-effort 水位提示，不承载 T。
};

} // namespace core::domain
