#pragma once

#include <QString>

/**
 * @brief 线程局部日志上下文（taskId / deviceId）
 * @details 在作用域内设置后，Logger 自动在每条日志末尾附加归属字段。
 *          系统级日志不设置 Scope 即可保持 taskId/deviceId 为空。
 */
class LogContext {
public:
    static QString currentTaskId();
    static QString currentDeviceId();

    /// RAII：进入作用域时设置，离开时恢复
    class Scope {
    public:
        Scope(const QString& deviceId = {}, const QString& taskId = {});
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        QString m_prevDevice;
        QString m_prevTask;
    };

private:
    static thread_local QString s_deviceId;
    static thread_local QString s_taskId;
};
