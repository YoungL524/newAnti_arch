/**
 * @file tst_ArchiveReader.cpp
 * @brief 验证归档 Reader 的双强类型 cursor、事务后推进、gap 和存储 Lane 调度合同。
 * @details 使用本文件内同步 Fake Repository 模拟整批提交与回滚，不提前冻结 SQLite 表结构；测试防止归档
 * 重新引入 DataPacket、第二级消息队列、失败时提前推进 cursor 或一个 Topic 故障阻塞另一个 Topic。
 */

#include "core/archive/ArchiveReader.h"
#include "core/datadomain/CoreDataDomain.h"

#include <QtTest>

#include <vector>

namespace {

/** @brief 记录两个强类型提交入口收到的 sequence，并可独立模拟整批事务失败。 */
class FakeArchiveRepository final : public core::archive::IArchiveRepository
{
public:
    bool failTrack = false; ///< true 时航迹批次整批回滚，不写入 trackSequences。
    bool failEvent = false; ///< true 时事件批次整批回滚，不写入 eventSequences。
    std::vector<core::domain::TopicSequence> trackSequences; ///< 已成功提交的航迹 sequence。
    std::vector<core::domain::TopicSequence> eventSequences; ///< 已成功提交的事件 sequence。

    /**
     * @brief 模拟航迹事务：失败时不产生副作用，成功时按传入顺序保存 sequence。
     */
    bool appendTrackBatch(
        const core::domain::TopicRecord<core::model::TrackPacket> *records,
        qsizetype count) override
    {
        if (failTrack) {
            return false;
        }
        for (qsizetype index = 0; index < count; ++index) {
            trackSequences.push_back(records[index].sequence);
        }
        return true;
    }

    /**
     * @brief 模拟事件事务：失败时不产生副作用，成功时按传入顺序保存 sequence。
     */
    bool appendEventBatch(
        const core::domain::TopicRecord<core::model::SystemEvent> *records,
        qsizetype count) override
    {
        if (failEvent) {
            return false;
        }
        for (qsizetype index = 0; index < count; ++index) {
            eventSequences.push_back(records[index].sequence);
        }
        return true;
    }
};

/**
 * @brief 构造带厂家航迹号的最小归档事实；归档测试不重复验证 TrackPublisher 门禁。
 * @param trackId 用于区分测试记录的厂家航迹号。
 * @return 可直接写入测试 TrackTopic 的定长 Sample。
 */
core::model::Sample<core::model::TrackPacket> makeTrackSample(quint32 trackId)
{
    core::model::Sample<core::model::TrackPacket> sample;
    sample.value.trackId = trackId;
    return sample;
}

/**
 * @brief 构造一个稳定事件码的最小归档事实；字段门禁由 SystemEventPublisher 测试负责。
 * @param argument 用于区分测试事件的定长参数。
 * @return 可直接写入测试 EventTopic 的定长 Sample。
 */
core::model::Sample<core::model::SystemEvent> makeEventSample(qint64 argument)
{
    core::model::Sample<core::model::SystemEvent> sample;
    sample.value.eventCode = core::model::EventCode::DeviceMalformedFrame;
    sample.value.argument0 = argument;
    return sample;
}

} // namespace

/** @brief 汇总 ArchiveReader 第一阶段强类型归档边界验收。 */
class ArchiveReaderTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收航迹和事件使用独立 cursor 与批量上限逐轮提交。 */
    void commitsEachTopicWithIndependentCursor();

    /** @brief 验收航迹事务失败不推进 cursor，也不阻止事件批次成功提交。 */
    void advancesCursorOnlyAfterCommit();

    /** @brief 验收两个 Topic 的覆盖 gap 分别计数并归档当前保留窗口。 */
    void recordsIndependentSequenceGaps();

    /** @brief 验收两个 Doorbell 通过一个存储 Lane 驱动 Reader 分批追平。 */
    void drainsFromSharedStorageLane();
};

/**
 * @brief 以 Track 批量二、Event 批量一读取 3/2 条事实，检查两个 cursor 按各自提交推进。
 */
void ArchiveReaderTest::commitsEachTopicWithIndependentCursor()
{
    core::domain::CoreDataDomain domain(8, 8);
    core::execution::ExecutionLane lane;
    FakeArchiveRepository repository;
    core::archive::ArchiveReader reader(
        domain.trackTopic(), domain.eventTopic(), lane, repository, 2, 1);
    for (quint32 id = 1; id <= 3; ++id) {
        domain.trackTopic().publish(makeTrackSample(id));
    }
    for (qint64 code = 1; code <= 2; ++code) {
        domain.eventTopic().publish(makeEventSample(code));
    }

    QVERIFY(reader.consumeOneTurn());
    QCOMPARE(reader.trackCommittedCursor(), core::domain::TopicSequence(2));
    QCOMPARE(reader.eventCommittedCursor(), core::domain::TopicSequence(1));
    QVERIFY(!reader.consumeOneTurn());
    QCOMPARE(reader.trackCommittedCursor(), core::domain::TopicSequence(3));
    QCOMPARE(reader.eventCommittedCursor(), core::domain::TopicSequence(2));
    QCOMPARE(repository.trackSequences.size(), std::size_t(3));
    QCOMPARE(repository.eventSequences.size(), std::size_t(2));
}

/**
 * @brief 首轮令航迹事务回滚而事件提交，第二轮恢复航迹 Repository 后从原 cursor 重读。
 * @details 事件 cursor 不受航迹失败影响，航迹记录也不能在失败批次中留下部分副作用。
 */
void ArchiveReaderTest::advancesCursorOnlyAfterCommit()
{
    core::domain::CoreDataDomain domain(8, 8);
    core::execution::ExecutionLane lane;
    FakeArchiveRepository repository;
    core::archive::ArchiveReader reader(
        domain.trackTopic(), domain.eventTopic(), lane, repository, 8, 8);
    for (quint32 id = 1; id <= 3; ++id) {
        domain.trackTopic().publish(makeTrackSample(id));
    }
    domain.eventTopic().publish(makeEventSample(1));

    repository.failTrack = true;
    QVERIFY(!reader.consumeOneTurn());
    QCOMPARE(reader.trackCommittedCursor(), core::domain::TopicSequence(0));
    QCOMPARE(reader.eventCommittedCursor(), core::domain::TopicSequence(1));
    QCOMPARE(reader.trackCommitFailureCount(), quint64(1));
    QVERIFY(repository.trackSequences.empty());

    repository.failTrack = false;
    QVERIFY(!reader.consumeOneTurn());
    QCOMPARE(reader.trackCommittedCursor(), core::domain::TopicSequence(3));
    QCOMPARE(repository.trackSequences.size(), std::size_t(3));
}

/**
 * @brief 容量二分别发布四条事实，使两个 committed cursor 都从零落后到窗口 [3,4]。
 */
void ArchiveReaderTest::recordsIndependentSequenceGaps()
{
    core::domain::CoreDataDomain domain(2, 2);
    core::execution::ExecutionLane lane;
    FakeArchiveRepository repository;
    core::archive::ArchiveReader reader(
        domain.trackTopic(), domain.eventTopic(), lane, repository, 4, 4);
    for (quint32 id = 1; id <= 4; ++id) {
        domain.trackTopic().publish(makeTrackSample(id));
        domain.eventTopic().publish(makeEventSample(id));
    }

    QVERIFY(!reader.consumeOneTurn());
    QCOMPARE(reader.trackGapCount(), quint64(1));
    QCOMPARE(reader.eventGapCount(), quint64(1));
    QCOMPARE(reader.trackCommittedCursor(), core::domain::TopicSequence(4));
    QCOMPARE(reader.eventCommittedCursor(), core::domain::TopicSequence(4));
    QCOMPARE(repository.trackSequences.front(), core::domain::TopicSequence(3));
    QCOMPARE(repository.eventSequences.front(), core::domain::TopicSequence(3));
}

/**
 * @brief 突发发布五条航迹和三条事件，仅依赖两个 Doorbell 与一个 Lane slot 追平两个 cursor。
 * @details 两个 Topic 的通知允许合并，Reader 必须通过实际 readAfter 和 hasMore 自主完成分页。
 */
void ArchiveReaderTest::drainsFromSharedStorageLane()
{
    core::domain::CoreDataDomain domain(8, 8);
    core::execution::ExecutionLane lane;
    FakeArchiveRepository repository;
    core::archive::ArchiveReader reader(
        domain.trackTopic(), domain.eventTopic(), lane, repository, 2, 2);
    for (quint32 id = 1; id <= 5; ++id) {
        domain.trackTopic().publish(makeTrackSample(id));
    }
    for (qint64 code = 1; code <= 3; ++code) {
        domain.eventTopic().publish(makeEventSample(code));
    }

    QTRY_COMPARE_WITH_TIMEOUT(reader.trackCommittedCursor(), core::domain::TopicSequence(5), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(reader.eventCommittedCursor(), core::domain::TopicSequence(3), 1000);
    QCOMPARE(repository.trackSequences.size(), std::size_t(5));
    QCOMPARE(repository.eventSequences.size(), std::size_t(3));
    QVERIFY(lane.executedWakeCount() >= quint64(3));
}

/**
 * @brief 创建带事件循环的非 GUI QtTest 入口，供 Topic Doorbell 与存储 Lane queued 唤醒运行。
 */
QTEST_GUILESS_MAIN(ArchiveReaderTest)

#include "tst_ArchiveReader.moc"
