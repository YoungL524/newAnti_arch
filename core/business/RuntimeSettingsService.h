#pragma once

#include "RuntimeSettingsTypes.h"

/**
 * @brief 运行时参数服务（内存快照）
 * @details 持有当前有效运行时参数，支持读写快照。
 *          对 DphPublisher 无直接依赖，变更通过 SystemBus 事件通知。
 */
class RuntimeSettingsService {
public:
    RuntimeSettingsService() = default;

    RadarRuntimeSettings get() const;
    bool set(const RadarRuntimeSettings& settings, QString* errorOut = nullptr);

private:
    RadarRuntimeSettings m_settings;
};
