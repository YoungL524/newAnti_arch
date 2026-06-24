#include "LogContext.h"

thread_local QString LogContext::s_deviceId;
thread_local QString LogContext::s_taskId;

QString LogContext::currentTaskId()
{
    return s_taskId;
}

QString LogContext::currentDeviceId()
{
    return s_deviceId;
}

LogContext::Scope::Scope(const QString& deviceId, const QString& taskId)
    : m_prevDevice(s_deviceId)
    , m_prevTask(s_taskId)
{
    if (!deviceId.isEmpty())
        s_deviceId = deviceId;
    if (!taskId.isEmpty())
        s_taskId = taskId;
}

LogContext::Scope::~Scope()
{
    s_deviceId = m_prevDevice;
    s_taskId = m_prevTask;
}
