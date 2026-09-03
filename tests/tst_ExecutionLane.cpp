/**
 * @file tst_ExecutionLane.cpp
 * @brief 验证 ExecutionLane 的 slot 隔离、突发合并、运行期再请求和释放语义。
 * @details 测试防止每条 Doorbell 创建独立 Consumer 唤醒、同一 Consumer 并发运行或 Lane 演化为业务队列。
 */

#include "core/execution/ExecutionLane.h"

#include <QSignalSpy>
#include <QtTest>

/** @brief 汇总共享执行通道必须满足的原子 pending bitmask 合同。 */
class ExecutionLaneTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收两个 Consumer 的大量重复请求合并为一个位集合和一次初始唤醒。 */
    void coalescesBurstAcrossConsumers();

    /** @brief 验收 Consumer 执行期间再次请求会进入下一轮而不会递归或丢失。 */
    void schedulesFollowUpRound();

    /** @brief 验收释放 slot 后旧 Doorbell 请求不再触发 Consumer 位。 */
    void ignoresReleasedSlot();
};

/**
 * @brief 在事件循环运行前各请求两个 slot 一百次，并检查首次信号只出现一次且同时包含两位。
 * @details pending bitmask 只表达调度，不携带任何 Sample、sequence 或 Consumer 指针。
 */
void ExecutionLaneTest::coalescesBurstAcrossConsumers()
{
    core::execution::ExecutionLane lane;
    const auto first = lane.acquireSlot();
    const auto second = lane.acquireSlot();
    QSignalSpy spy(&lane, &core::execution::ExecutionLane::executeRequested);
    QVERIFY(spy.isValid());

    for (int repeat = 0; repeat < 100; ++repeat) {
        lane.request(first);
        lane.request(second);
    }

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    const quint64 expected = (quint64(1) << first) | (quint64(1) << second);
    QCOMPARE(spy.front().front().toULongLong(), expected);
    QCOMPARE(lane.executedWakeCount(), quint64(1));

    lane.releaseSlot(second);
    lane.releaseSlot(first);
}

/**
 * @brief 首轮 Consumer 回调只追加一次自身请求，确认它在下一事件循环轮次执行。
 * @details 若 Lane 在发信调用栈内递归执行或在释放 wake token 前漏掉请求，本测试会得到错误轮数。
 */
void ExecutionLaneTest::schedulesFollowUpRound()
{
    core::execution::ExecutionLane lane;
    const auto slot = lane.acquireSlot();
    int executions = 0;
    QObject::connect(
        &lane,
        &core::execution::ExecutionLane::executeRequested,
        &lane,
        [&lane, slot, &executions](quint64 pendingMask) {
            if ((pendingMask & (quint64(1) << slot)) == 0) {
                return;
            }
            ++executions;
            if (executions == 1) {
                lane.request(slot);
            }
        });

    lane.request(slot);
    QTRY_COMPARE_WITH_TIMEOUT(executions, 2, 1000);
    QCOMPARE(lane.executedWakeCount(), quint64(2));
    lane.releaseSlot(slot);
}

/**
 * @brief 在事件循环处理前释放已置位 slot，并确认旧请求被清除且后续 request 被忽略。
 */
void ExecutionLaneTest::ignoresReleasedSlot()
{
    core::execution::ExecutionLane lane;
    const auto slot = lane.acquireSlot();
    QSignalSpy spy(&lane, &core::execution::ExecutionLane::executeRequested);
    QVERIFY(spy.isValid());

    lane.request(slot);
    lane.releaseSlot(slot);
    lane.request(slot);
    QTest::qWait(20);

    QCOMPARE(spy.count(), 0);
}

/**
 * @brief 创建带事件循环的非 GUI QtTest 入口，用于执行 queued Lane 唤醒。
 */
QTEST_GUILESS_MAIN(ExecutionLaneTest)

#include "tst_ExecutionLane.moc"
