#pragma once

#include <QMap>
#include <QMutex>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

enum class PipelineLatencyStage : uint8_t {
    Decode,
    ProcessorTotal,
    IngressToDisplay,
    DisplayProcessing,
    SnapshotWait,
    SnapshotBuild,
    SnapshotBusDispatch,
    WebQueue,
    WebPrepareAndBroadcast,
    IngressToWebSend,
    ActiveBackendToWebSend,
    Count
};

struct PipelineLatencyStats {
    uint64_t sampleCount = 0;
    uint64_t windowSampleCount = 0;
    int64_t minUs = 0;
    int64_t avgUs = 0;
    int64_t p50Us = 0;
    int64_t p95Us = 0;
    int64_t p99Us = 0;
    int64_t maxUs = 0;
};

struct PipelineLatencySnapshot {
    QString unit = QStringLiteral("us");
    int windowCapacity = 0;
    QMap<QString, PipelineLatencyStats> stages;
};

/**
 * In-memory rolling latency aggregation. Recording is O(1); percentile sorting
 * only happens when a diagnostics snapshot is requested.
 */
class PipelineLatencyMonitor {
public:
    static constexpr int kWindowCapacity = 4096;

    void recordNs(PipelineLatencyStage stage, int64_t durationNs);
    PipelineLatencySnapshot snapshot() const;

    static QString stageName(PipelineLatencyStage stage);

private:
    struct Accumulator {
        uint64_t totalCount = 0;
        std::deque<int64_t> recentUs;
    };

    static PipelineLatencyStats summarize(const Accumulator& accumulator);

    mutable QMutex m_mutex;
    std::array<Accumulator, static_cast<size_t>(PipelineLatencyStage::Count)> m_stages;
};
