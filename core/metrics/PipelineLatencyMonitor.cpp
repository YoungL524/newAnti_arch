#include "PipelineLatencyMonitor.h"

#include <QMutexLocker>
#include <algorithm>
#include <vector>

namespace {

int64_t percentile(const std::vector<int64_t>& sorted, double ratio)
{
    if (sorted.empty())
        return 0;
    const size_t index = static_cast<size_t>(
        ratio * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

} // namespace

void PipelineLatencyMonitor::recordNs(PipelineLatencyStage stage, int64_t durationNs)
{
    if (durationNs < 0 || stage == PipelineLatencyStage::Count)
        return;

    const int64_t durationUs = durationNs / 1000;
    const size_t index = static_cast<size_t>(stage);
    QMutexLocker lock(&m_mutex);
    auto& accumulator = m_stages[index];
    ++accumulator.totalCount;
    accumulator.recentUs.push_back(durationUs);
    if (accumulator.recentUs.size() > kWindowCapacity)
        accumulator.recentUs.pop_front();
}

PipelineLatencySnapshot PipelineLatencyMonitor::snapshot() const
{
    PipelineLatencySnapshot result;
    result.windowCapacity = kWindowCapacity;

    QMutexLocker lock(&m_mutex);
    for (size_t i = 0; i < m_stages.size(); ++i) {
        const auto stage = static_cast<PipelineLatencyStage>(i);
        result.stages.insert(stageName(stage), summarize(m_stages[i]));
    }
    return result;
}

QString PipelineLatencyMonitor::stageName(PipelineLatencyStage stage)
{
    switch (stage) {
    case PipelineLatencyStage::Decode: return QStringLiteral("decode");
    case PipelineLatencyStage::ProcessorTotal: return QStringLiteral("processorTotal");
    case PipelineLatencyStage::IngressToDisplay: return QStringLiteral("ingressToDisplay");
    case PipelineLatencyStage::DisplayProcessing: return QStringLiteral("displayProcessing");
    case PipelineLatencyStage::SnapshotWait: return QStringLiteral("snapshotWait");
    case PipelineLatencyStage::SnapshotBuild: return QStringLiteral("snapshotBuild");
    case PipelineLatencyStage::SnapshotBusDispatch: return QStringLiteral("snapshotBusDispatch");
    case PipelineLatencyStage::WebQueue: return QStringLiteral("webQueue");
    case PipelineLatencyStage::WebPrepareAndBroadcast:
        return QStringLiteral("webPrepareAndBroadcast");
    case PipelineLatencyStage::IngressToWebSend: return QStringLiteral("ingressToWebSend");
    case PipelineLatencyStage::ActiveBackendToWebSend:
        return QStringLiteral("activeBackendToWebSend");
    case PipelineLatencyStage::Count: break;
    }
    return QStringLiteral("unknown");
}

PipelineLatencyStats PipelineLatencyMonitor::summarize(const Accumulator& accumulator)
{
    PipelineLatencyStats result;
    result.sampleCount = accumulator.totalCount;
    result.windowSampleCount = accumulator.recentUs.size();
    if (accumulator.recentUs.empty())
        return result;

    std::vector<int64_t> sorted(accumulator.recentUs.begin(), accumulator.recentUs.end());
    std::sort(sorted.begin(), sorted.end());

    int64_t sum = 0;
    for (const int64_t value : sorted)
        sum += value;

    result.minUs = sorted.front();
    result.avgUs = sum / static_cast<int64_t>(sorted.size());
    result.p50Us = percentile(sorted, 0.50);
    result.p95Us = percentile(sorted, 0.95);
    result.p99Us = percentile(sorted, 0.99);
    result.maxUs = sorted.back();
    return result;
}
