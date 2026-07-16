#pragma once

#include <chrono>
#include <cstdint>

inline int64_t monotonicNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
