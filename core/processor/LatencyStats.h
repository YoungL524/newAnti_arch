/**
 * @file LatencyStats.h
 * @brief 提供上行和下行链路使用的轻量累计延时快照。
 * @details 本结构以毫秒整数累计 min/max/avg，不保存样本序列，适合健康接口低成本读取。
 *          它不加锁，生产和读取必须由调用方保证位于同一线程或已有外部同步。
 */

#pragma once

#include <cstdint>

/**
 * @brief 延时统计（线程安全由调用方保证）
 * @details 记录 min/avg/max 延时，供 HealthReport 和诊断日志使用。
 *          非线程安全；所有调用应在同一线程。
 */
struct LatencyStats {
    /** @brief 纳入一个毫秒延时样本并以整数除法更新平均值。 */
    void addSample(int64_t deltaMs)
    {
        if (sampleCount == 0 || deltaMs < minMs) minMs = deltaMs;
        if (sampleCount == 0 || deltaMs > maxMs) maxMs = deltaMs;
        accumulatedMs += deltaMs;
        ++sampleCount;
        avgMs = accumulatedMs / sampleCount;
    }

    /** @brief 清除全部累计量，使下一样本重新初始化 min/max。 */
    void reset()
    {
        minMs = 0;
        maxMs = 0;
        avgMs = 0;
        sampleCount = 0;
        accumulatedMs = 0;
    }

    /** @brief 返回是否至少纳入过一个样本。 */
    bool hasSamples() const { return sampleCount > 0; }

    int64_t minMs = 0;             ///< 最小端到端延时，单位毫秒。
    int64_t maxMs = 0;             ///< 最大端到端延时，单位毫秒。
    int64_t avgMs = 0;             ///< accumulatedMs/sampleCount 的截断整数平均值。
    uint64_t sampleCount = 0;      ///< reset 后累计样本数。
    int64_t accumulatedMs = 0;     ///< 所有样本的毫秒和，用于计算平均值。
};
