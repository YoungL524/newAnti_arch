#include "RuntimeSettingsService.h"
#include <QDebug>

static constexpr uint32_t kMaxFieldValue = 255;

/**
 * @brief 返回当前雷达运行参数的值快照，调用方修改副本不会影响服务状态。
 */
RadarRuntimeSettings RuntimeSettingsService::get() const
{
    return m_settings;
}

/**
 * @brief 校验频率和波形控制值是否处于雷达协议允许范围，并返回首个可展示错误。
 */
bool RuntimeSettingsService::validate(const RadarRuntimeSettings& settings, QString* errorOut) const
{
    if (settings.frequencyControl > kMaxFieldValue) {
        if (errorOut) *errorOut = QStringLiteral("frequencyControl 超出范围 0..255");
        return false;
    }
    if (settings.waveControl > kMaxFieldValue) {
        if (errorOut) *errorOut = QStringLiteral("waveControl 超出范围 0..255");
        return false;
    }
    return true;
}

/**
 * @brief 仅在校验通过且值实际变化时更新快照，并发布 DphParamsChanged 总线事件。
 * @details 总线发布失败会向调用方返回失败，避免界面误认为 DPH 已应用新参数。
 */
bool RuntimeSettingsService::set(const RadarRuntimeSettings& settings, QString* errorOut)
{
    if (!validate(settings, errorOut))
        return false;
    m_settings = settings;
    return true;
}
