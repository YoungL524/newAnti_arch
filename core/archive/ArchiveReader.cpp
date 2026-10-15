#include "core/archive/ArchiveReader.h"

namespace core::archive {

/**
 * @brief 一次性分配两个固定 ScratchBuffer 和一个 Lane slot，再连接只携带水位的通知。
 * @details 构造不读取历史；组合根完成线程归属和 Repository 初始化后应调用 refresh()。
 */
ArchiveReader::ArchiveReader(
    const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
    const core::domain::EventTopic<core::model::SystemEvent> &eventTopic,
    core::execution::ExecutionLane &executionLane,
    IArchiveRepository &repository,
    qsizetype trackBatchCapacity,
    qsizetype eventBatchCapacity,
    QObject *parent)
    : QObject(parent)
    , m_trackTopic(trackTopic)
    , m_eventTopic(eventTopic)
    , m_executionLane(executionLane)
    , m_repository(repository)
    , m_trackScratch(trackBatchCapacity)
    , m_eventScratch(eventBatchCapacity)
    , m_executionSlot(executionLane.acquireSlot())
{
    QObject::connect(
        trackTopic.doorbell(),
        &core::domain::TopicDoorbell::advanced,
        this,
        &ArchiveReader::onTopicAdvanced, Qt::DirectConnection);
    QObject::connect(
        eventTopic.doorbell(),
        &core::domain::TopicDoorbell::advanced,
        this,
        &ArchiveReader::onTopicAdvanced, Qt::DirectConnection);
    QObject::connect(
        &executionLane,
        &core::execution::ExecutionLane::executeRequested,
        this,
        &ArchiveReader::onExecutionRequested);
}

/**
 * @brief 先断开所有更新来源，再清除 Lane 中可能尚未执行的归档位。
 */
ArchiveReader::~ArchiveReader()
{
    QObject::disconnect(m_trackTopic.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(m_eventTopic.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(&m_executionLane, nullptr, this, nullptr);
    m_executionLane.releaseSlot(m_executionSlot);
}

/**
 * @brief 每个 Topic 最多读取并同步提交一个固定批次，互不共享 cursor 或失败状态。
 * @details Track 失败后仍尝试 Event，避免一种存储映射故障饿死另一种事实；返回值只请求有确定剩余数据的
 * 成功 Topic 继续运行，失败 Topic 等待显式 refresh 或后续 Doorbell，防止数据库故障时忙循环。
 */
bool ArchiveReader::consumeOneTurn()
{
    const quint32 previous = (m_trackFailed || m_eventFailed ? 1U : 0U)
        | (m_trackGapCount ? 2U : 0U) | (m_eventGapCount ? 4U : 0U);
    const bool trackHasMore = consumeTrackBatch();
    const bool eventHasMore = consumeEventBatch();
    const quint32 current = (m_trackFailed || m_eventFailed ? 1U : 0U)
        | (m_trackGapCount ? 2U : 0U) | (m_eventGapCount ? 4U : 0U);
    if (previous != current) Q_EMIT archiveStatusChanged(current);
    return trackHasMore || eventHasMore;
}

/**
 * @brief 返回航迹事务已确认提交的最后序号，不读取 Topic 当前水位。
 */
core::domain::TopicSequence ArchiveReader::trackCommittedCursor() const noexcept
{
    return m_trackCommittedCursor;
}

/**
 * @brief 返回事件事务已确认提交的最后序号，不与航迹 sequence 比较。
 */
core::domain::TopicSequence ArchiveReader::eventCommittedCursor() const noexcept
{
    return m_eventCommittedCursor;
}

/**
 * @brief 返回航迹 cursor 落后 retention 窗口的累计检测次数。
 */
quint64 ArchiveReader::trackGapCount() const noexcept
{
    return m_trackGapCount;
}

/**
 * @brief 返回事件 cursor 落后 retention 窗口的累计检测次数。
 */
quint64 ArchiveReader::eventGapCount() const noexcept
{
    return m_eventGapCount;
}

/**
 * @brief 返回 Repository 航迹整批回滚或提交失败的累计次数。
 */
quint64 ArchiveReader::trackCommitFailureCount() const noexcept
{
    return m_trackCommitFailureCount;
}

/**
 * @brief 返回 Repository 事件整批回滚或提交失败的累计次数。
 */
quint64 ArchiveReader::eventCommitFailureCount() const noexcept
{
    return m_eventCommitFailureCount;
}

/**
 * @brief 无条件置位一次归档调度，使初始历史或退避后的失败批次能够重新读取。
 */
void ArchiveReader::refresh()
{
    m_executionLane.request(m_executionSlot);
}

/**
 * @brief 在Doorbell线程只调用线程安全Lane置位，不访问cursor或Repository，避免存储阻塞时累积通知事件。
 */
void ArchiveReader::onTopicAdvanced(quint64 newestSequence)
{
    Q_UNUSED(newestSequence)
    m_executionLane.request(m_executionSlot);
}

/**
 * @brief 位命中时执行一次双 Topic 有界归档，有成功分页时重新请求下一轮。
 */
void ArchiveReader::onExecutionRequested(quint64 pendingMask)
{
    const quint64 ownBit = quint64(1) << m_executionSlot;
    if ((pendingMask & ownBit) == 0) {
        return;
    }
    if (consumeOneTurn()) {
        m_executionLane.request(m_executionSlot);
    }
}

/**
 * @brief 从 track committed cursor 读取一个固定批次，并以 Repository 返回值作为唯一提交判据。
 * @details Gap 计数并同步报告首次缺口，再归档当前仍保留的记录；若 cursor Ahead，不擅自归零以避免重复写入，等待
 * 后续持久化会话合同决定恢复策略。空批次和 Ahead 均不调用 Repository。
 */
bool ArchiveReader::consumeTrackBatch()
{
    const auto result = m_trackTopic.readAfter(m_trackCommittedCursor, m_trackScratch);
    if (result.relation == core::domain::CursorRelation::Gap) {
        ++m_trackGapCount;
        if (m_trackGapCount == 1) Q_EMIT firstGapDetected(false, result.gap->firstLost, result.gap->lastLost);
    }
    if (result.relation == core::domain::CursorRelation::Ahead || result.count == 0) {
        return false;
    }
    if (!m_repository.appendTrackBatch(m_trackScratch.data(), result.count)) {
        ++m_trackCommitFailureCount;
        m_trackFailed = true;
        return false;
    }
    m_trackFailed = false;
    m_trackCommittedCursor = m_trackScratch[result.count - 1].sequence;
    return result.hasMore;
}

/**
 * @brief 从 event committed cursor 读取一个固定批次，并仅在整批事务提交后推进事件 cursor。
 * @details 事件 gap 与航迹 gap 分别计数；失败不自旋，防止存储异常占满 Lane 所在线程。
 */
bool ArchiveReader::consumeEventBatch()
{
    const auto result = m_eventTopic.readAfter(m_eventCommittedCursor, m_eventScratch);
    if (result.relation == core::domain::CursorRelation::Gap) {
        ++m_eventGapCount;
        if (m_eventGapCount == 1) Q_EMIT firstGapDetected(true, result.gap->firstLost, result.gap->lastLost);
    }
    if (result.relation == core::domain::CursorRelation::Ahead || result.count == 0) {
        return false;
    }
    if (!m_repository.appendEventBatch(m_eventScratch.data(), result.count)) {
        ++m_eventCommitFailureCount;
        m_eventFailed = true;
        return false;
    }
    m_eventFailed = false;
    m_eventCommittedCursor = m_eventScratch[result.count - 1].sequence;
    return result.hasMore;
}

} // namespace core::archive
