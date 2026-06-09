#include "RuntimeSettingsService.h"
#include <QDebug>

static constexpr uint32_t kMaxFieldValue = 255;

RadarRuntimeSettings RuntimeSettingsService::get() const
{
    return m_settings;
}

bool RuntimeSettingsService::set(const RadarRuntimeSettings& settings, QString* errorOut)
{
    if (settings.frequencyControl > kMaxFieldValue) {
        if (errorOut) *errorOut = QStringLiteral("frequencyControl 超出范围 0..255");
        return false;
    }
    if (settings.waveControl > kMaxFieldValue) {
        if (errorOut) *errorOut = QStringLiteral("waveControl 超出范围 0..255");
        return false;
    }
    m_settings = settings;
    return true;
}
