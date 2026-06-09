#pragma once

#include <cstdint>
#include "GenericPackets.h"

/**
 * @brief 运行时参数（用户可配置）
 * @details 内部业务模型，不直接对应 DPH 协议字段名。
 *          DphPublisher 负责将 settings 映射到 DataPackageHeader。
 */
struct RadarRuntimeSettings {
    uint32_t frequencyControl = 0;  // → F_UI_Freq_Ctrl
    uint32_t waveControl = 0;       // → F_UI_Wave_Ctrl
};

/**
 * @brief DPH 运行状态（只读）
 */
struct DphStatus {
    uint32_t currentSceneNumber = 0;
    uint64_t sendCount = 0;
};

// 事件常量（定义在 GenericPackets.h 中）
//   PAYLOAD_TYPE_DPH_PARAMS_CHANGED = 103
//   SOURCE_RUNTIME_SETTINGS = 205

template<>
inline uint32_t getPayloadTypeConstant<RadarRuntimeSettings>() {
    return PAYLOAD_TYPE_DPH_PARAMS_CHANGED;
}
