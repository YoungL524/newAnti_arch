#include "PipelineLatencyMonitor.h"

#include <QMutexLocker>
#include <algorithm>
#include <vector>

namespace {

/**
 * @brief 从已排序窗口中按 nearest-rank 的向下取整索引读取分位数。
 * @details ratio 映射到 [0, size-1]，不做插值；空窗口返回 0，便于无样本快照保持零值。
 */
int64_t percentile(const std::vector<int64_t>& sorted, double ratio)
{
    if (sorted.empty())
        return 0;
    const size_t index = static_cast<size_t>(
        ratio * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

} // namespace

/**
 * @brief 校验阶段和时长后，将纳秒截断为微秒加入对应滑动窗口。
 * @details totalCount 不随窗口淘汰而减少；deque 超过 4096 时只删除最旧的分位数样本。
 */
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

/**
 * @brief 在一把锁内汇总全部阶段，保证同一快照不会混合不同采样时刻的窗口。
 * @note 排序和分位数计算也在锁内完成；调用频率应远低于热路径 recordNs()。
 */
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

/**
 * @brief 把枚举转换为稳定的 camelCase 外部键名，供 JSON 和数据库长期使用。
 */
QString PipelineLatencyMonitor::stageName(PipelineLatencyStage stage)
{
    switch (stage) {
    case PipelineLatencyStage::Decode: return QStringLiteral("decode");
    case PipelineLatencyStage::ProcessorTotal: return QStringLiteral("processorTotal");
    case PipelineLatencyStage::IngressToDisplay: return QStringLiteral("ingressToDisplay");
    case PipelineLatencyStage::DisplayProcessing: return QStringLiteral("displayProcessing");
    case PipelineLatencyStage::DisplayUpdateWait: return QStringLiteral("displayUpdateWait");
    case PipelineLatencyStage::DisplayUpdateBuild: return QStringLiteral("displayUpdateBuild");
    case PipelineLatencyStage::DisplayUpdateBusDispatch: return QStringLiteral("displayUpdateBusDispatch");
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

/**
 * @brief 复制、排序近期窗口并计算微秒 min/avg/p50/p95/p99/max。
 * @details sampleCount 来自不截断的累计计数，windowSampleCount 才是参与本次统计的样本数。
 */
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
