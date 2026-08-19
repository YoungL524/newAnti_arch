#include <QtTest>
#include "metrics/PipelineLatencyMonitor.h"

class PipelineLatencyMonitorTest : public QObject
{
    Q_OBJECT

private slots:
    void emptySnapshotContainsAllStages()
    {
        PipelineLatencyMonitor monitor;
        const auto snapshot = monitor.snapshot();

        QCOMPARE(snapshot.unit, QStringLiteral("us"));
        QCOMPARE(snapshot.windowCapacity, PipelineLatencyMonitor::kWindowCapacity);
        QCOMPARE(snapshot.stages.size(), static_cast<int>(PipelineLatencyStage::Count));
        QCOMPARE(snapshot.stages.value(QStringLiteral("decode")).sampleCount, uint64_t(0));
    }

    void calculatesRollingPercentiles()
    {
        PipelineLatencyMonitor monitor;
        for (int us = 1; us <= 100; ++us)
            monitor.recordNs(PipelineLatencyStage::Decode, int64_t(us) * 1000);

        const auto stats = monitor.snapshot().stages.value(QStringLiteral("decode"));
        QCOMPARE(stats.sampleCount, uint64_t(100));
        QCOMPARE(stats.windowSampleCount, uint64_t(100));
        QCOMPARE(stats.minUs, int64_t(1));
        QCOMPARE(stats.avgUs, int64_t(50));
        QCOMPARE(stats.p50Us, int64_t(50));
        QCOMPARE(stats.p95Us, int64_t(95));
        QCOMPARE(stats.p99Us, int64_t(99));
        QCOMPARE(stats.maxUs, int64_t(100));
    }

    void boundsTheRollingWindow()
    {
        PipelineLatencyMonitor monitor;
        const int total = PipelineLatencyMonitor::kWindowCapacity + 4;
        for (int us = 1; us <= total; ++us)
            monitor.recordNs(PipelineLatencyStage::IngressToDisplay, int64_t(us) * 1000);
        monitor.recordNs(PipelineLatencyStage::IngressToDisplay, -1);

        const auto stats = monitor.snapshot().stages.value(QStringLiteral("ingressToDisplay"));
        QCOMPARE(stats.sampleCount, static_cast<uint64_t>(total));
        QCOMPARE(stats.windowSampleCount,
                 static_cast<uint64_t>(PipelineLatencyMonitor::kWindowCapacity));
        QCOMPARE(stats.minUs, int64_t(5));
        QCOMPARE(stats.maxUs, static_cast<int64_t>(total));
    }
};

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(PipelineLatencyMonitorTest)
#include "tst_PipelineLatencyMonitor.moc"
