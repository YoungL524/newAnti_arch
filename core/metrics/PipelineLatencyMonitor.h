/**
 * @file PipelineLatencyMonitor.h
 * @brief 定义核心实时链路各阶段的线程安全滑动窗口延时指标。
 * @details 生产者以单调纳秒提交时长，监控器转换为微秒并为每阶段保留最近 4096 个样本；snapshot()
 *          在锁内复制和汇总 min/avg/p50/p95/p99/max。窗口只用于近期分位数，totalCount 持续累计。
 */

#pragma once

#include <QMap>
#include <QMutex>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

/** @brief 从 UDP 解码到 WebSocket 实际发送之间可独立定位瓶颈的阶段。 */
enum class PipelineLatencyStage : uint8_t {
    Decode,
    ProcessorTotal,
    IngressToDisplay,
    DisplayProcessing,
    DisplayUpdateWait,
    DisplayUpdateBuild,
    DisplayUpdateBusDispatch,
    WebQueue,
    WebPrepareAndBroadcast,
    IngressToWebSend,
    ActiveBackendToWebSend,
    Count
};

/** @brief 单个阶段的累计数量和近期窗口统计，所有时长单位均为微秒。 */
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
 * @brief 可由接收、显示和 Web 等不同线程共同写入的延时聚合器。
 */
class PipelineLatencyMonitor {
public:
    static constexpr int kWindowCapacity = 4096;

    /** @brief 记录非负纳秒时长；非法阶段或负值被忽略，超出窗口时淘汰最旧样本。 */
    void recordNs(PipelineLatencyStage stage, int64_t durationNs);
    /** @brief 获取全部阶段的一致性统计快照；计算期间短暂持有互斥锁。 */
    PipelineLatencySnapshot snapshot() const;

    /** @brief 返回用于 JSON、数据库和日志的稳定阶段名称。 */
    static QString stageName(PipelineLatencyStage stage);

private:
    struct Accumulator {
        uint64_t totalCount = 0;     ///< 进程生命周期内累计有效样本数。
        std::deque<int64_t> recentUs; ///< 最近 kWindowCapacity 个微秒样本。
    };

    static PipelineLatencyStats summarize(const Accumulator& accumulator);

    mutable QMutex m_mutex;
    std::array<Accumulator, static_cast<size_t>(PipelineLatencyStage::Count)> m_stages;
};
