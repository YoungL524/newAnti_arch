/**
 * @file tst_RealtimeDataPathStress.cpp
 * @brief 验收实时数据域在持续发布、慢 Consumer、容量覆盖和突发调度下的有界资源行为。
 * @details 本测试不模拟完整 7×24 小时，但用确定性高压场景守住其必要条件：Producer 不等待 Consumer、
 * gap 可检测恢复、内存由启动容量决定、ScratchBuffer 不扩容、ExecutionLane 不按请求数累积 Qt 唤醒。
 */

#include "core/datadomain/HistoryTopic.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/TrackPacket.h"

#include <QSignalSpy>
#include <QtTest>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <thread>
#include <vector>

namespace {

/** @brief 为并发压力测试提供无动态成员的 64 字节业务载荷。 */
struct StressRecord
{
    quint64 sequenceEcho = 0; ///< 保存发布侧编号，用于核对最后一条事实未被撕裂。
    std::array<quint64, 7> padding {}; ///< 模拟定长业务字段，使复制压力接近真实航迹量级。
};

static_assert(sizeof(StressRecord) == 64, "StressRecord must remain a fixed 64-byte payload");

/**
 * @brief 构造业务编号和全部填充字段一致的确定性压力 Sample。
 * @param value 从 1 开始的发布编号。
 * @return 不依赖外部时钟、可检查完整复制的定长事实。
 */
core::model::Sample<StressRecord> makeStressSample(quint64 value)
{
    core::model::Sample<StressRecord> sample;
    sample.value.sequenceEcho = value;
    sample.value.padding.fill(value);
    return sample;
}

} // namespace

/** @brief 汇总核心实时链路的持续负载、慢读者、内存和调度合并验收。 */
class RealtimeDataPathStressTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收 10 万条并发发布与游标读取最终追上同一 Topic 水位。 */
    void sustainsConcurrentPublishAndCursorRead();

    /** @brief 验收 Consumer 在锁外停顿时 Producer 不被反压且随后得到精确 gap。 */
    void slowConsumerNeverBlocksProducer();

    /** @brief 验收 4096 航迹槽位和 256 条 ScratchBuffer 的总内存预算固定。 */
    void trackStorageStaysWithinFrozenBudget();

    /** @brief 验收多线程十六万次调度请求只形成一个初始 Qt 唤醒。 */
    void coalescesHighRateLaneRequests();
};

/**
 * @brief 一个 Producer 和一个独立 cursor Reader 并发处理 100000 条事实。
 * @details Reader 允许因调度落后出现 gap，但必须从当前 oldest 恢复并最终读到 sequence 100000；测试同时
 * 验证覆盖统计、最终范围和记录内容在并发复制下保持一致。
 */
void RealtimeDataPathStressTest::sustainsConcurrentPublishAndCursorRead()
{
    constexpr quint64 totalRecords = 100'000;
    constexpr qsizetype topicCapacity = 4096;
    core::domain::HistoryTopic<StressRecord> topic(topicCapacity);
    std::atomic_bool producerFinished {false};
    std::atomic<quint64> finalCursor {0};
    std::atomic<quint64> observedGaps {0};

    std::thread reader([&topic, &producerFinished, &finalCursor, &observedGaps]() {
        core::domain::TopicScratchBuffer<StressRecord> scratch(128);
        core::domain::TopicSequence cursor = 0;
        while (!producerFinished.load(std::memory_order_acquire)
               || cursor < topic.range().newest) {
            const auto result = topic.readAfter(cursor, scratch);
            if (result.relation == core::domain::CursorRelation::Gap) {
                observedGaps.fetch_add(1, std::memory_order_relaxed);
            }
            for (qsizetype index = 0; index < result.count; ++index) {
                const auto &record = scratch[index];
                if (record.sample.value.sequenceEcho != record.sequence) {
                    finalCursor.store(std::numeric_limits<quint64>::max(), std::memory_order_release);
                    return;
                }
                cursor = record.sequence;
            }
            if (result.count == 0) {
                std::this_thread::yield();
            }
        }
        finalCursor.store(cursor, std::memory_order_release);
    });

    std::thread producer([&topic, &producerFinished]() {
        for (quint64 value = 1; value <= totalRecords; ++value) {
            topic.publish(makeStressSample(value));
        }
        producerFinished.store(true, std::memory_order_release);
    });

    producer.join();
    reader.join();

    QCOMPARE(finalCursor.load(std::memory_order_acquire), totalRecords);
    const auto stats = topic.stats();
    QCOMPARE(stats.size, topicCapacity);
    QCOMPARE(stats.overwrittenRecords, totalRecords - static_cast<quint64>(topicCapacity));
    QCOMPARE(stats.range.newest, totalRecords);
    QVERIFY(observedGaps.load(std::memory_order_relaxed) <= totalRecords);
}

/**
 * @brief Reader 取出首批后等待测试信号，Producer 同时覆盖 Topic 数百次并必须在 Reader 恢复前完成。
 * @details 如果 readAfter 把 Topic 锁带到 Consumer 处理阶段，Producer future 会超时；恢复后新 Reader 从旧
 * cursor 读取必须报告完整丢失闭区间，而不是阻塞、静默跳过或扩展 retention。
 */
void RealtimeDataPathStressTest::slowConsumerNeverBlocksProducer()
{
    constexpr qsizetype capacity = 32;
    constexpr quint64 initialRecords = 4;
    constexpr quint64 additionalRecords = 10'000;
    core::domain::HistoryTopic<StressRecord> topic(capacity);
    for (quint64 value = 1; value <= initialRecords; ++value) {
        topic.publish(makeStressSample(value));
    }

    std::atomic_bool batchCopied {false};
    std::atomic_bool releaseConsumer {false};
    std::thread slowConsumer([&topic, &batchCopied, &releaseConsumer]() {
        core::domain::TopicScratchBuffer<StressRecord> scratch(4);
        const auto result = topic.readAfter(0, scratch);
        if (result.count == 4) {
            batchCopied.store(true, std::memory_order_release);
        }
        while (!releaseConsumer.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });

    QTRY_VERIFY_WITH_TIMEOUT(batchCopied.load(std::memory_order_acquire), 1000);
    auto producer = std::async(std::launch::async, [&topic]() {
        for (quint64 offset = 1; offset <= additionalRecords; ++offset) {
            topic.publish(makeStressSample(initialRecords + offset));
        }
        return true;
    });

    const auto producerStatus = producer.wait_for(std::chrono::seconds(5));
    releaseConsumer.store(true, std::memory_order_release);
    slowConsumer.join();
    QVERIFY(producerStatus == std::future_status::ready);
    QVERIFY(producer.get());

    core::domain::TopicScratchBuffer<StressRecord> recovery(64);
    const auto result = topic.readAfter(initialRecords, recovery);
    QVERIFY(result.relation == core::domain::CursorRelation::Gap);
    QVERIFY(result.gap.has_value());
    const quint64 expectedOldest = initialRecords + additionalRecords
        - static_cast<quint64>(capacity) + 1;
    QCOMPARE(result.gap->firstLost, initialRecords + 1);
    QCOMPARE(result.gap->lastLost, expectedOldest - 1);
    QCOMPARE(result.count, capacity);
    QCOMPARE(recovery[0].sequence, expectedOldest);
    QCOMPARE(recovery[result.count - 1].sequence, initialRecords + additionalRecords);
}

/**
 * @brief 按固定部署容量核算 TrackTopic 槽位和显示批次缓冲区，并验证地址在重复读取后不变。
 * @details 600 KiB 是当前 4096 条航迹历史加 256 条 Reader 批次的上限，不包含 QObject、QMutex 和最终
 * 显示索引；任何字段膨胀都会通过精确字节值或上限断言显式暴露。
 */
void RealtimeDataPathStressTest::trackStorageStaysWithinFrozenBudget()
{
    constexpr qsizetype trackCapacity = 4096;
    constexpr qsizetype displayBatch = 256;
    constexpr quint64 budgetBytes = 600U * 1024U;
    core::domain::HistoryTopic<core::model::TrackPacket> topic(trackCapacity);
    core::domain::TopicScratchBuffer<core::model::TrackPacket> scratch(displayBatch);
    const auto *const initialAddress = scratch.data();

    const quint64 expectedTopicBytes = static_cast<quint64>(trackCapacity)
        * sizeof(core::domain::TopicRecord<core::model::TrackPacket>);
    const quint64 expectedScratchBytes = static_cast<quint64>(displayBatch)
        * sizeof(core::domain::TopicRecord<core::model::TrackPacket>);
    QCOMPARE(topic.stats().slotStorageBytes, expectedTopicBytes);
    QCOMPARE(scratch.storageBytes(), expectedScratchBytes);
    QVERIFY(expectedTopicBytes + expectedScratchBytes <= budgetBytes);

    for (quint64 value = 1; value <= 1024; ++value) {
        core::model::Sample<core::model::TrackPacket> sample;
        sample.value.trackId = static_cast<quint32>(value);
        topic.publish(sample);
    }
    const auto result = topic.readAfter(0, scratch);
    QCOMPARE(result.count, displayBatch);
    QCOMPARE(scratch.data(), initialAddress);
    QCOMPARE(scratch.capacity(), displayBatch);
}

/**
 * @brief 四个线程对八个 Consumer slot 共发出 160000 次请求，并在事件循环启动后检查首次合并结果。
 * @details 请求阶段不处理 Qt 事件，因此所有重复请求必须折叠为八个位和一个 queued wake；该测试守住
 * “唤醒数量与 Sample/请求数量无关”的核心资源合同。
 */
void RealtimeDataPathStressTest::coalescesHighRateLaneRequests()
{
    core::execution::ExecutionLane lane;
    std::array<core::execution::ExecutionSlot, 8> laneSlots {};
    quint64 expectedMask = 0;
    for (auto &slot : laneSlots) {
        slot = lane.acquireSlot();
        expectedMask |= quint64(1) << slot;
    }
    QSignalSpy spy(&lane, &core::execution::ExecutionLane::executeRequested);
    QVERIFY(spy.isValid());

    std::vector<std::thread> requesters;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
        requesters.emplace_back([&lane, &laneSlots]() {
            for (int repeat = 0; repeat < 5'000; ++repeat) {
                for (const auto slot : laneSlots) {
                    lane.request(slot);
                }
            }
        });
    }
    for (auto &requester : requesters) {
        requester.join();
    }

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    QCOMPARE(spy.front().front().toULongLong(), expectedMask);
    QCOMPARE(lane.executedWakeCount(), quint64(1));
    for (auto iterator = laneSlots.rbegin(); iterator != laneSlots.rend(); ++iterator) {
        lane.releaseSlot(*iterator);
    }
}

/**
 * @brief 创建带事件循环的非 GUI QtTest 入口，支持 Doorbell 和 ExecutionLane 的 queued 唤醒。
 */
QTEST_GUILESS_MAIN(RealtimeDataPathStressTest)

#include "tst_RealtimeDataPathStress.moc"
