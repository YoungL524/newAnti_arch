/**
 * @file TopicTypes.h
 * @brief 定义共享有界历史的序号、游标关系、只读记录和统计结果。
 * @details 本文件是 Consumer 主动读取 CoreDataDomain 时使用的公共合同：上游 HistoryTopic 填充这些
 * 结果，下游 ArchiveReader、WebReader 与 PluginReader 各自维护 cursor 并解释 gap。
 * 本文件不保存 Consumer 状态、不执行数据分发，也不定义业务载荷；新人应从 CursorRelation 与
 * HistoryReadResult 的组合语义开始阅读。
 */

#pragma once

#include "core/model/Sample.h"

#include <QtGlobal>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace core::domain {

/** @brief HistoryTopic 内部使用、从 1 开始单调递增的位置序号。 */
using TopicSequence = quint64;

/** @brief LatestState 每次成功替换完整状态时从 1 开始递增的修订号。 */
using StateRevision = quint64;

/**
 * @brief Topic 当前仍可读取的闭区间。
 * @note empty 为 true 时 oldest/newest 均为零，Consumer 不应把零当作已发布记录。
 */
struct TopicRange
{
    TopicSequence oldest = 0; ///< 当前仍驻留在 Topic 中的最老序号。
    TopicSequence newest = 0; ///< 当前仍驻留在 Topic 中的最新序号。
    bool empty = true;        ///< true 表示该 Topic 从未成功发布记录。
};

/** @brief Consumer 因落后于共享历史窗口而无法恢复的连续序号区间。 */
struct SequenceGap
{
    TopicSequence firstLost = 0; ///< cursor 后第一条已被覆盖的期望记录。
    TopicSequence lastLost = 0;  ///< 恢复连续读取前最后一条已被覆盖的记录。
};

/** @brief 描述 Consumer cursor 与读取瞬间 Topic 有效窗口的相对位置。 */
enum class CursorRelation : quint8
{
    Continuous = 0, ///< cursor 后的数据仍连续可读，或 Consumer 已追上最新水位。
    Gap = 1,        ///< cursor 后至少一条记录已被容量覆盖，结果从当前 oldest 开始恢复。
    Ahead = 2       ///< cursor 大于当前 newest，通常来自已重建 Topic 或旧进程。
};

/**
 * @brief HistoryTopic 槽位和 Reader 临时缓冲区共用的定长记录。
 * @tparam T Sample 中的强类型业务模型。
 */
template<typename T>
struct TopicRecord
{
    TopicSequence sequence = 0; ///< Topic 追加时分配的位置序号，与来源序号相互独立。
    core::model::Sample<T> sample; ///< 内嵌事实；Topic 保存原件，Reader 只取得有界短期副本。
};

/**
 * @brief 由 Reader 构造一次并跨读取轮次复用的固定容量记录缓冲区。
 * @tparam T 对应强类型 Topic 的业务模型。
 * @details 内部 vector 只在构造时分配并定容，readAfter 不调整容量。对象只属于一个 Reader，不是第二份
 * 长期历史、Consumer Queue 或 Topic retention 的组成部分。
 */
template<typename T>
class TopicScratchBuffer final
{
public:
    /**
     * @brief 一次性分配 Reader 单批最多处理的记录槽位。
     * @param capacity 固定批量上限，必须大于零，构造后不可改变。
     * @throws std::invalid_argument capacity 小于等于零时抛出。
     */
    explicit TopicScratchBuffer(qsizetype capacity)
        : m_records(static_cast<std::size_t>(validateCapacity(capacity)))
    {
    }

    /** @brief 返回构造时冻结的最大批量记录数。 */
    qsizetype capacity() const noexcept
    {
        return static_cast<qsizetype>(m_records.size());
    }

    /**
     * @brief 返回本 Reader 为记录槽位一次性分配的有效字节预算。
     * @return capacity() 与 sizeof(TopicRecord<T>) 的乘积，不含 vector 对象固定开销。
     */
    quint64 storageBytes() const noexcept
    {
        return static_cast<quint64>(m_records.size()) * sizeof(TopicRecord<T>);
    }

    /**
     * @brief 返回供 HistoryTopic 写入的连续槽位首地址。
     * @return 生命周期与本缓冲区一致且不会因 readAfter 失效的地址。
     */
    TopicRecord<T>* data() noexcept
    {
        return m_records.data();
    }

    /**
     * @brief 返回供强类型归档接口读取的连续槽位首地址。
     * @return 只读地址在本缓冲区生命周期内稳定，下一次 readAfter 会覆盖其中内容。
     */
    const TopicRecord<T>* data() const noexcept
    {
        return m_records.data();
    }

    /**
     * @brief 按下标读取当前批次中由 readAfter 填充的记录。
     * @param index 必须小于本次 HistoryReadResult::count。
     * @return 对缓冲区内短期副本的只读引用。
     */
    const TopicRecord<T>& operator[](qsizetype index) const noexcept
    {
        return m_records[static_cast<std::size_t>(index)];
    }

private:
    /** @brief 在 size_t 转换和内存分配前拒绝非法容量。 */
    static qsizetype validateCapacity(qsizetype capacity)
    {
        if (capacity <= 0) {
            throw std::invalid_argument("TopicScratchBuffer capacity must be greater than zero");
        }
        return capacity;
    }

    std::vector<TopicRecord<T>> m_records; ///< 构造期定容，运行期间禁止 resize 的连续记录槽位。
};

/** @brief LatestState 当前实现用于延长完整状态快照生命周期的只读句柄。 */
template<typename T>
using SamplePtr = std::shared_ptr<const core::model::Sample<T>>;

/**
 * @brief LatestState 在同一短锁快照中返回的修订号和不可变完整状态。
 * @tparam T Sample 中的强类型状态模型；T 必须描述完整状态而不是局部补丁。
 */
template<typename T>
struct StateRecord
{
    StateRevision revision = 0; ///< 本次完整状态的单调修订号；有效记录从 1 开始。
    SamplePtr<T> sample;         ///< 当前完整状态的只读共享句柄；有效记录中不为空。
};

/**
 * @brief 返回 cursor 之后有界记录、缺口信息和一致读取水位。
 * @tparam T Sample 中的强类型业务模型。
 * @details ScratchBuffer 前 count 项始终按 Topic sequence 升序且连续；Consumer 只有在自身处理成功后
 * 才推进 cursor。newestAtRead 是取得 Topic 短锁时的水位，不承诺返回后 Producer 没有继续发布。
 */
template<typename T>
struct HistoryReadResult
{
    qsizetype count = 0; ///< 本次写入调用方 ScratchBuffer 的有效记录数。
    CursorRelation relation = CursorRelation::Continuous; ///< 输入 cursor 与读取窗口的关系。
    std::optional<SequenceGap> gap; ///< relation 为 Gap 时存在，其他关系必须为空。
    TopicSequence newestAtRead = 0; ///< 本次短锁内观察到的最新 Topic sequence，空 Topic 为零。
    bool hasMore = false; ///< true 表示受 ScratchBuffer 容量限制，本次水位内仍有后续记录未返回。
};

/** @brief HistoryTopic 的固定容量、槽位内存、当前驻留量、覆盖累计值和有效范围。 */
struct TopicStats
{
    qsizetype capacity = 0;         ///< 构造后不变的 RingBuffer 槽位数，必须大于零。
    qsizetype size = 0;             ///< 当前有效记录数，范围为 [0, capacity]。
    quint64 slotStorageBytes = 0;   ///< 预分配记录槽位字节数，不含 vector、互斥量和 Doorbell 固定开销。
    quint64 overwrittenRecords = 0; ///< 因容量已满而被覆盖的累计记录数，不因 Reader 读取而变化。
    TopicRange range;               ///< 与同一次短锁快照对应的当前有效序号范围。
};

} // namespace core::domain
