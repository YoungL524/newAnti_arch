/**
 * @file tst_HistoryTopic.cpp
 * @brief 验收共享有界 HistoryTopic 的覆盖、内嵌槽位、固定 ScratchBuffer 和 Doorbell 契约。
 * @details 测试使用仅存在于本文件的 TestTrack，防止在真实 TrackPacket 字段合同尚未迁入前制造空壳模型。
 * 所有测试均使用 Topic 内部序号，不依赖墙上时钟；Doorbell 用 QCoreApplication 事件循环等待异步通知。
 */

#include "core/datadomain/HistoryTopic.h"

#include <QMetaMethod>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

/** @brief 为数据域基础设施测试提供最小强类型载荷，不代表生产 TrackPacket 合同。 */
struct TestTrack
{
    int identifier = 0; ///< 用于断言发布顺序和不可变值的测试编号，无物理单位。
};

static_assert(std::is_trivially_copyable<core::domain::TopicRecord<TestTrack>>::value,
              "定长测试记录必须可直接复制到 ScratchBuffer");

/**
 * @brief 构造带来源序号和有效性标记的测试事实。
 * @param identifier 同时写入业务值与 sourceSequence，便于区分来源序号和 Topic sequence。
 * @return 可移动发布的测试 Sample。
 */
core::model::Sample<TestTrack> makeSample(int identifier)
{
    core::model::Sample<TestTrack> sample;
    sample.meta.sourceSequence = static_cast<quint64>(identifier);
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence);
    sample.value.identifier = identifier;
    return sample;
}

/** @brief 汇总 HistoryTopic 第一阶段必须阻止的回归场景。 */
class HistoryTopicTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /** @brief 验证零容量和负容量不会退化成无界或异常分配。 */
    void rejectsInvalidCapacity();

    /** @brief 验证空 Topic 对新 cursor 与旧进程 cursor 给出不同关系。 */
    void reportsEmptyTopicCursorRelation();

    /** @brief 验证容量三发布五条后只保留 [3,5] 且覆盖统计准确。 */
    void overwritesOldestWithinFixedCapacity();

    /** @brief 验证落后 cursor 得到明确缺口并从当前 oldest 连续恢复。 */
    void reportsGapAndReturnsRetainedRecords();

    /** @brief 验证 maxItems 分页、hasMore 与追平后的空结果。 */
    void paginatesWithoutDestroyingRecords();

    /** @brief 验证大于 newest 的 cursor 返回 Ahead 而不是永久空等。 */
    void reportsCursorAhead();

    /** @brief 验证两个 Reader 的 cursor 互不影响且读取不会从 Topic 删除记录。 */
    void keepsReaderCursorsIndependent();

    /** @brief 验证 latest/readAfter 返回短期值副本且覆盖不会改变已读缓冲区。 */
    void copiesIntoReusableScratchBuffer();

    /** @brief 验证并发 Producer 获得唯一连续序号且 Topic 容量始终有界。 */
    void publishesConcurrentlyWithUniqueSequences();

    /** @brief 验证 Doorbell 只携带单一水位、跨线程回到所属线程且通知槽可立即读 Topic。 */
    void ringsCoalescedWatermarkDoorbell();
};

/**
 * @brief 对非法容量分别执行构造，防止负数转换为 size_t 后造成巨量分配。
 */
void HistoryTopicTest::rejectsInvalidCapacity()
{
    QVERIFY_EXCEPTION_THROWN(core::domain::HistoryTopic<TestTrack>(0), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(core::domain::HistoryTopic<TestTrack>(-1), std::invalid_argument);
}

/**
 * @brief 以 cursor=0 表示首次读取，以非零 cursor 模拟 Topic 重建后的旧 cursor。
 */
void HistoryTopicTest::reportsEmptyTopicCursorRelation()
{
    core::domain::HistoryTopic<TestTrack> topic(3);
    core::domain::TopicScratchBuffer<TestTrack> scratch(10);

    const auto initial = topic.readAfter(0, scratch);
    QVERIFY(initial.relation == core::domain::CursorRelation::Continuous);
    QCOMPARE(initial.count, qsizetype(0));
    QCOMPARE(initial.newestAtRead, quint64(0));

    const auto stale = topic.readAfter(7, scratch);
    QVERIFY(stale.relation == core::domain::CursorRelation::Ahead);
    QCOMPARE(stale.count, qsizetype(0));
    QVERIFY(!stale.gap.has_value());

    QVERIFY_EXCEPTION_THROWN(core::domain::TopicScratchBuffer<TestTrack>(0), std::invalid_argument);
}

/**
 * @brief 连续发布五条记录，检查 RingBuffer 覆盖顺序、latest 值和不依赖 Reader 的累计覆盖数。
 */
void HistoryTopicTest::overwritesOldestWithinFixedCapacity()
{
    core::domain::HistoryTopic<TestTrack> topic(3);
    for (int value = 1; value <= 5; ++value) {
        QCOMPARE(topic.publish(makeSample(value)), static_cast<quint64>(value));
    }

    const auto range = topic.range();
    QVERIFY(!range.empty);
    QCOMPARE(range.oldest, quint64(3));
    QCOMPARE(range.newest, quint64(5));

    core::domain::TopicRecord<TestTrack> latest;
    QVERIFY(topic.latest(latest));
    QCOMPARE(latest.sequence, quint64(5));
    QCOMPARE(latest.sample.value.identifier, 5);
    QVERIFY(core::model::hasSampleField(
        latest.sample.meta.validity,
        core::model::SampleFieldFlag::HasSourceSequence));
    QCOMPARE(latest.sample.meta.sourceSequence, quint64(5));

    const auto stats = topic.stats();
    QCOMPARE(stats.capacity, qsizetype(3));
    QCOMPARE(stats.size, qsizetype(3));
    QCOMPARE(stats.slotStorageBytes,
             quint64(3 * sizeof(core::domain::TopicRecord<TestTrack>)));
    QCOMPARE(stats.overwrittenRecords, quint64(2));
}

/**
 * @brief 从零 cursor 读取已经覆盖两条记录的窗口，要求缺口与返回区间都精确闭合。
 */
void HistoryTopicTest::reportsGapAndReturnsRetainedRecords()
{
    core::domain::HistoryTopic<TestTrack> topic(3);
    core::domain::TopicScratchBuffer<TestTrack> scratch(10);
    for (int value = 1; value <= 5; ++value) {
        topic.publish(makeSample(value));
    }

    const auto result = topic.readAfter(0, scratch);
    QVERIFY(result.relation == core::domain::CursorRelation::Gap);
    QVERIFY(result.gap.has_value());
    QCOMPARE(result.gap->firstLost, quint64(1));
    QCOMPARE(result.gap->lastLost, quint64(2));
    QCOMPARE(result.count, qsizetype(3));
    QCOMPARE(scratch[0].sequence, quint64(3));
    QCOMPARE(scratch[1].sequence, quint64(4));
    QCOMPARE(scratch[2].sequence, quint64(5));
    QCOMPARE(result.newestAtRead, quint64(5));
    QVERIFY(!result.hasMore);
}

/**
 * @brief 用单条批次读取 sequence=4，再从返回序号继续读取并确认 Topic 驻留量未变化。
 */
void HistoryTopicTest::paginatesWithoutDestroyingRecords()
{
    core::domain::HistoryTopic<TestTrack> topic(5);
    core::domain::TopicScratchBuffer<TestTrack> oneRecord(1);
    core::domain::TopicScratchBuffer<TestTrack> twoRecords(2);
    for (int value = 1; value <= 5; ++value) {
        topic.publish(makeSample(value));
    }

    const auto firstPage = topic.readAfter(3, oneRecord);
    QCOMPARE(firstPage.count, qsizetype(1));
    QCOMPARE(oneRecord[0].sequence, quint64(4));
    QVERIFY(firstPage.hasMore);

    const auto secondPage = topic.readAfter(oneRecord[0].sequence, twoRecords);
    QCOMPARE(secondPage.count, qsizetype(1));
    QCOMPARE(twoRecords[0].sequence, quint64(5));
    QVERIFY(!secondPage.hasMore);

    const auto caughtUp = topic.readAfter(5, twoRecords);
    QCOMPARE(caughtUp.count, qsizetype(0));
    QVERIFY(caughtUp.relation == core::domain::CursorRelation::Continuous);
    QCOMPARE(topic.stats().size, qsizetype(5));
}

/**
 * @brief 用超过最新水位的 cursor 模拟重启前保存的旧 cursor，要求显式返回 Ahead。
 */
void HistoryTopicTest::reportsCursorAhead()
{
    core::domain::HistoryTopic<TestTrack> topic(3);
    core::domain::TopicScratchBuffer<TestTrack> scratch(3);
    topic.publish(makeSample(1));
    topic.publish(makeSample(2));

    const auto result = topic.readAfter(9, scratch);
    QVERIFY(result.relation == core::domain::CursorRelation::Ahead);
    QCOMPARE(result.count, qsizetype(0));
    QVERIFY(!result.gap.has_value());
    QCOMPARE(result.newestAtRead, quint64(2));
}

/**
 * @brief 让两个逻辑 Reader 从不同 cursor 分批读取，确认 Topic 不保存或推进任何 Reader 状态。
 */
void HistoryTopicTest::keepsReaderCursorsIndependent()
{
    core::domain::HistoryTopic<TestTrack> topic(4);
    core::domain::TopicScratchBuffer<TestTrack> readerA(4);
    core::domain::TopicScratchBuffer<TestTrack> readerBBuffer(4);
    topic.publish(makeSample(1));
    topic.publish(makeSample(2));
    topic.publish(makeSample(3));

    core::domain::TopicScratchBuffer<TestTrack> readerAFirstBuffer(1);
    const auto readerAFirst = topic.readAfter(0, readerAFirstBuffer);
    const auto readerB = topic.readAfter(0, readerBBuffer);
    const auto readerASecond = topic.readAfter(readerAFirstBuffer[0].sequence, readerA);

    QCOMPARE(readerAFirst.count, qsizetype(1));
    QCOMPARE(readerB.count, qsizetype(3));
    QCOMPARE(readerASecond.count, qsizetype(2));
    QCOMPARE(readerA[0].sequence, quint64(2));
    QCOMPARE(topic.stats().size, qsizetype(3));
}

/**
 * @brief 比较 latest 与顺序读取的值，并在槽位覆盖后继续访问 Reader ScratchBuffer 中的副本。
 */
void HistoryTopicTest::copiesIntoReusableScratchBuffer()
{
    core::domain::HistoryTopic<TestTrack> topic(2);
    core::domain::TopicScratchBuffer<TestTrack> scratch(1);
    const auto *const fixedAddress = scratch.data();
    topic.publish(makeSample(1));
    topic.publish(makeSample(2));

    core::domain::TopicRecord<TestTrack> latest;
    QVERIFY(topic.latest(latest));
    const auto batch = topic.readAfter(1, scratch);
    QCOMPARE(batch.count, qsizetype(1));
    QCOMPARE(latest.sample.value.identifier, scratch[0].sample.value.identifier);

    topic.publish(makeSample(3));
    topic.publish(makeSample(4));

    QCOMPARE(scratch[0].sample.value.identifier, 2);
    QCOMPARE(scratch.data(), fixedAddress);
    QCOMPARE(scratch.capacity(), qsizetype(1));
    QCOMPARE(topic.range().oldest, quint64(3));
}

/**
 * @brief 四个 Producer 同时发布固定数量事实，排序返回值后验证没有重复、缺口或超出容量的驻留记录。
 */
void HistoryTopicTest::publishesConcurrentlyWithUniqueSequences()
{
    constexpr int producerCount = 4;
    constexpr int recordsPerProducer = 100;
    constexpr int totalRecords = producerCount * recordsPerProducer;

    core::domain::HistoryTopic<TestTrack> topic(totalRecords);
    std::vector<core::domain::TopicSequence> sequences(static_cast<std::size_t>(totalRecords));
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (int producer = 0; producer < producerCount; ++producer) {
        /** 每个线程只写自己在 sequences 中的区间，避免测试夹具额外引入共享锁。 */
        producers.emplace_back([producer, &topic, &sequences]() {
            const int offset = producer * recordsPerProducer;
            for (int index = 0; index < recordsPerProducer; ++index) {
                sequences[static_cast<std::size_t>(offset + index)] =
                    topic.publish(makeSample(offset + index + 1));
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    std::sort(sequences.begin(), sequences.end());
    for (int index = 0; index < totalRecords; ++index) {
        QCOMPARE(sequences[static_cast<std::size_t>(index)], static_cast<quint64>(index + 1));
    }

    const auto stats = topic.stats();
    QCOMPARE(stats.size, qsizetype(totalRecords));
    QCOMPARE(stats.overwrittenRecords, quint64(0));
    QCOMPARE(stats.range.oldest, quint64(1));
    QCOMPARE(stats.range.newest, quint64(totalRecords));
}

/**
 * @brief 从 Worker 发布五次并在主线程等待通知；槽函数立即顺序读取，用于防止锁内 ring 导致的死锁。
 */
void HistoryTopicTest::ringsCoalescedWatermarkDoorbell()
{
    core::domain::HistoryTopic<TestTrack> topic(5);
    core::domain::TopicScratchBuffer<TestTrack> scratch(5);
    QSignalSpy spy(topic.doorbell(), &core::domain::TopicDoorbell::advanced);
    QVERIFY(spy.isValid());

    QThread* notificationThread = nullptr;
    quint64 consumerCursor = 0;
    QObject::connect(
        topic.doorbell(),
        &core::domain::TopicDoorbell::advanced,
        this,
        /** Doorbell 不使用信号参数取数据；槽内回到 Topic 读取，并只保存自己的 cursor。 */
        [&topic, &scratch, &notificationThread, &consumerCursor](quint64) {
            notificationThread = QThread::currentThread();
            const auto result = topic.readAfter(consumerCursor, scratch);
            if (result.count > 0) {
                consumerCursor = scratch[result.count - 1].sequence;
            }
        });

    const QMetaMethod signal = QMetaMethod::fromSignal(&core::domain::TopicDoorbell::advanced);
    QCOMPARE(signal.parameterCount(), 1);

    /** Worker 只负责发布；Doorbell 必须把 advanced 切回创建 Topic 的主线程。 */
    std::thread producer([&topic]() {
        for (int value = 1; value <= 5; ++value) {
            topic.publish(makeSample(value));
        }
    });
    producer.join();

    QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 1000);
    quint64 previous = 0;
    for (const auto& arguments : spy) {
        const quint64 version = arguments.at(0).toULongLong();
        QVERIFY(version >= previous);
        previous = version;
    }
    QCOMPARE(previous, quint64(5));
    QCOMPARE(consumerCursor, quint64(5));
    QCOMPARE(notificationThread, topic.doorbell()->thread());
}

} // namespace

/**
 * @brief 创建仅含 QCoreApplication 的 QtTest 入口，使 queued Doorbell 通知拥有事件循环。
 * @details 测试不依赖 GUI；入口由 QtTest 宏生成并按声明顺序执行 HistoryTopicTest 的私有槽。
 */
QTEST_GUILESS_MAIN(HistoryTopicTest)

#include "tst_HistoryTopic.moc"
