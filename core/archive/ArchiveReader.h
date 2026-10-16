/**
 * @file ArchiveReader.h
 * @brief 定义从多个强类型 Topic 按独立 committed cursor 批量归档的存储 Consumer。
 * @details 本模块位于 CoreDataDomain 与 IArchiveRepository 之间：它读取 TrackTopic 和 EventTopic 的固定
 * ScratchBuffer，Repository 确认事务提交后才推进对应 cursor。Doorbell 只向共享存储 ExecutionLane 置位；
 * 本类不创建线程、不保存第二份长期历史、不统一包装载荷，也不决定数据库表结构。
 */

#pragma once

#include "core/archive/IArchiveRepository.h"
#include "core/datadomain/EventTopic.h"
#include "core/datadomain/HistoryTopic.h"
#include "core/execution/ExecutionLane.h"

#include <QObject>
#include <QtGlobal>

namespace core::archive {

/**
 * @brief 串行归档现有航迹与系统事件 Topic 的有状态 Consumer。
 * @details 两个 Topic 各自拥有 cursor、ScratchBuffer、gap 和失败计数；一个 Topic 的提交失败不会阻止另一个
 * Topic 在同一轮提交。Reader、Repository 与 ExecutionLane 必须位于同一存储线程，两个 Topic 生命周期
 * 必须覆盖 Reader。
 */
class ArchiveReader final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 绑定两个强类型 Topic、共享存储 Lane 与事务 Repository。
     * @param trackTopic CoreDataDomain 拥有的航迹历史。
     * @param eventTopic CoreDataDomain 拥有的统一事件日志。
     * @param executionLane 存储线程上的共享调度通道，生命周期必须覆盖 Reader。
     * @param repository 同一存储线程使用的强类型事务端口，生命周期必须覆盖 Reader。
     * @param trackBatchCapacity 单轮最多提交的航迹数，必须大于零。
     * @param eventBatchCapacity 单轮最多提交的事件数，必须大于零。
     * @param parent Qt 生命周期父对象，不得早于 Topic、Lane 或 Repository 析构。
     * @throws std::invalid_argument 任一批量容量小于等于零时由 ScratchBuffer 拒绝构造。
     */
    ArchiveReader(
        const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
        const core::domain::EventTopic<core::model::SystemEvent> &eventTopic,
        core::execution::ExecutionLane &executionLane,
        IArchiveRepository &repository,
        qsizetype trackBatchCapacity,
        qsizetype eventBatchCapacity,
        QObject *parent = nullptr);

    /**
     * @brief 断开 Topic/Lane 通知并释放存储 Lane slot。
     * @warning 必须在 executionLane 所属线程销毁，且所有依赖对象仍然存活。
     */
    ~ArchiveReader() override;

    /** @brief 禁止复制 QObject、两个 cursor、ScratchBuffer 与 Lane slot。 */
    ArchiveReader(const ArchiveReader&) = delete;

    /** @brief 禁止复制赋值，防止 committed cursor 与数据库事务归属分裂。 */
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    /**
     * @brief 分别尝试归档两个 Topic 的一个有界批次。
     * @return 任一成功批次在读取水位内仍有后续记录时返回 true。
     * @note 提交失败的 Topic cursor 保持不变且不会立即自旋重试；另一个 Topic 仍可独立提交。
     */
    bool consumeOneTurn();

    /** @brief 返回最后成功提交的 TrackTopic sequence，尚未提交时为零。 */
    core::domain::TopicSequence trackCommittedCursor() const noexcept;

    /** @brief 返回最后成功提交的 EventTopic sequence，尚未提交时为零。 */
    core::domain::TopicSequence eventCommittedCursor() const noexcept;

    /** @brief 返回 TrackTopic 历史覆盖导致的累计 gap 次数。 */
    quint64 trackGapCount() const noexcept;

    /** @brief 返回 EventTopic 历史覆盖导致的累计 gap 次数。 */
    quint64 eventGapCount() const noexcept;

    /** @brief 返回航迹批次事务提交失败累计次数。 */
    quint64 trackCommitFailureCount() const noexcept;

    /** @brief 返回事件批次事务提交失败累计次数。 */
    quint64 eventCommitFailureCount() const noexcept;

public Q_SLOTS:
    /**
     * @brief 主动请求一次存储 Lane 调度，用于首次接入或外部退避结束后的重试。
     * @note 必须在存储线程调用；解除 Lane 故障暂停，不读取数据、不创建定时器。
     */
    void refresh();

Q_SIGNALS:
    /** @brief 本轮任一事务失败后在存储线程请求外部安排一次退避；Reader 已暂停 Lane slot，失败 cursor 不变。 */
    void retryRequired();

    /** @brief 存储线程内同步报告三位归档结果；仅变化时发出，不携带历史 Sample，不得排队到主线程。 */
    void archiveStatusChanged(quint32 degradationFlags);

    /** @brief 存储线程内报告本 Reader 首次观察的指定 Topic 缺口闭区间；每个 Topic 最多一次。 */
    void firstGapDetected(bool eventTopic, quint64 firstLost, quint64 lastLost);

private Q_SLOTS:
    /**
     * @brief 接收任一 Topic Doorbell 并仅向存储 Lane 置本 Reader 的 pending 位。
     * @param newestSequence 被刻意忽略的 Topic 水位；两个 Topic sequence 空间不能互相比较。
     */
    void onTopicAdvanced(quint64 newestSequence);

    /**
     * @brief 在共享 Lane 位集合命中本 Reader 时执行一个归档轮次。
     * @param pendingMask 本轮 Consumer 调度位，不包含任何业务数据。
     */
    void onExecutionRequested(quint64 pendingMask);

private:
    /**
     * @brief 读取并提交一个航迹批次，成功后推进 track committed cursor。
     * @return 提交成功且同一读取水位仍有后续航迹时返回 true。
     */
    bool consumeTrackBatch();

    /**
     * @brief 读取并提交一个事件批次，成功后推进 event committed cursor。
     * @return 提交成功且同一读取水位仍有后续事件时返回 true。
     */
    bool consumeEventBatch();

    const core::domain::HistoryTopic<core::model::TrackPacket> &m_trackTopic; ///< 借用的唯一航迹历史。
    const core::domain::EventTopic<core::model::SystemEvent> &m_eventTopic; ///< 借用的统一事件日志。
    core::execution::ExecutionLane &m_executionLane; ///< 借用的存储线程合并调度通道。
    IArchiveRepository &m_repository; ///< 借用的同步事务端口，只在 Reader/Lane 线程调用。
    core::domain::TopicScratchBuffer<core::model::TrackPacket> m_trackScratch; ///< 固定航迹提交批次。
    core::domain::TopicScratchBuffer<core::model::SystemEvent> m_eventScratch; ///< 固定事件提交批次。
    const core::execution::ExecutionSlot m_executionSlot; ///< 本 Reader 在存储 Lane 中的唯一调度位。
    core::domain::TopicSequence m_trackCommittedCursor = 0; ///< 最后成功提交的航迹 Topic sequence。
    core::domain::TopicSequence m_eventCommittedCursor = 0; ///< 最后成功提交的事件 Topic sequence。
    quint64 m_trackGapCount = 0; ///< 航迹读取发现 retention gap 的累计次数。
    quint64 m_eventGapCount = 0; ///< 事件读取发现 retention gap 的累计次数。
    quint64 m_trackCommitFailureCount = 0; ///< 航迹事务失败累计次数，不推进 cursor。
    quint64 m_eventCommitFailureCount = 0; ///< 事件事务失败累计次数，不推进 cursor。
    bool m_trackFailed = false; ///< 航迹最近一次非空事务是否失败，仅同类成功提交可清除。
    bool m_eventFailed = false; ///< 事件最近一次非空事务是否失败，另一类提交不能掩盖它。
};

} // namespace core::archive
