#include "LogContext.h"

thread_local QString LogContext::s_deviceId;
thread_local QString LogContext::s_taskId;

/**
 * @brief 返回调用线程当前关联的任务标识。
 * @return 最近一层 Scope 设置的任务标识；从未设置时为空字符串。
 */
QString LogContext::currentTaskId()
{
    return s_taskId;
}

/**
 * @brief 返回调用线程当前关联的设备标识。
 * @return 最近一层 Scope 设置的设备标识；从未设置时为空字符串。
 */
QString LogContext::currentDeviceId()
{
    return s_deviceId;
}

/**
 * @brief 保存调用线程的旧上下文，并应用本作用域提供的非空标识。
 * @param deviceId 本作用域使用的设备标识；空值表示继承外层值。
 * @param taskId 本作用域使用的任务标识；空值表示继承外层值。
 */
LogContext::Scope::Scope(const QString& deviceId, const QString& taskId)
    : m_prevDevice(s_deviceId)
    , m_prevTask(s_taskId)
{
    if (!deviceId.isEmpty())
        s_deviceId = deviceId;
    if (!taskId.isEmpty())
        s_taskId = taskId;
}

/**
 * @brief 结束日志上下文作用域并恢复进入前的线程局部值。
 * @details 无论本层是否覆盖某个字段，都会恢复构造时保存的两个字段，从而保证嵌套作用域成对还原。
 */
LogContext::Scope::~Scope()
{
    s_deviceId = m_prevDevice;
    s_taskId = m_prevTask;
}
