/**
 * @file MonotonicClock.h
 * @brief 提供全进程统一的单调纳秒时间基准，用于链路阶段耗时计算。
 * @details steady_clock 不受 NTP、手工校时和时区变化影响，但其 epoch 没有业务含义，数值不得持久化为
 *          事件时间或跨进程比较。业务日志和数据库时间使用 Unix 毫秒，两种时间基准不可混算。
 */

#pragma once

#include <chrono>
#include <cstdint>

/** @brief 返回当前进程单调时钟刻度，单位纳秒；仅允许同一进程内做差。 */
inline int64_t monotonicNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
