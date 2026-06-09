#pragma once

#include "RuntimeSettingsTypes.h"
class QString;

/**
 * @brief 运行时参数服务（内存快照）
 * @details 持有当前有效运行时参数，支持读写快照。
 *          对 DphPublisher 无直接依赖，变更通过 SystemBus 事件通知。
 *
 * 线程安全：
 *   当前由 WebApiController 所在线程访问（Qt 主线程）。
 *   SystemBus 回调不得直接访问本 service。
 */
class RuntimeSettingsService {
public:
    RuntimeSettingsService() = default;

    RadarRuntimeSettings get() const;
    bool set(const RadarRuntimeSettings& settings, QString* errorOut = nullptr);

private:
    RadarRuntimeSettings m_settings;
};
