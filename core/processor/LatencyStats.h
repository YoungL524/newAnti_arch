#pragma once

#include <cstdint>

/**
 * @brief 延时统计（线程安全由调用方保证）
 * @details 记录 min/avg/max 延时，供 HealthReport 和诊断日志使用。
 *          非线程安全；所有调用应在同一线程。
 */
struct LatencyStats {
    void addSample(int64_t deltaMs)
    {
        if (sampleCount == 0 || deltaMs < minMs) minMs = deltaMs;
        if (sampleCount == 0 || deltaMs > maxMs) maxMs = deltaMs;
        accumulatedMs += deltaMs;
        ++sampleCount;
        avgMs = accumulatedMs / sampleCount;
    }

    void reset()
    {
        minMs = 0;
        maxMs = 0;
        avgMs = 0;
        sampleCount = 0;
        accumulatedMs = 0;
    }

    bool hasSamples() const { return sampleCount > 0; }

    int64_t minMs = 0;
    int64_t maxMs = 0;
    int64_t avgMs = 0;
    uint64_t sampleCount = 0;
    int64_t accumulatedMs = 0;
};
