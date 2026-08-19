/**
 * @file LogContext.h
 * @brief 声明按线程传播任务和设备标识的日志上下文。
 * @details Logger 在生成 LogRecord 时读取这里的线程局部值，使同一条执行链上的日志可按任务或设备筛选，
 *          同时避免多个通信线程之间相互覆盖上下文。
 */

#pragma once

#include <QString>

/**
 * @brief 保存当前线程的任务标识和设备标识。
 * @details 使用 Scope 在一个词法作用域内临时覆盖标识；析构时恢复进入前的值，因此允许嵌套。
 *          线程局部存储不会跨线程自动传播，异步任务若需要上下文，必须在目标线程重新创建 Scope。
 */
class LogContext {
public:
    /** @brief 返回当前线程的任务标识；未设置时返回空字符串。 */
    static QString currentTaskId();
    /** @brief 返回当前线程的设备标识；未设置时返回空字符串。 */
    static QString currentDeviceId();

    /** @brief 以 RAII 方式临时设置当前线程的日志归属字段。 */
    class Scope {
    public:
        /**
         * @brief 保存旧上下文，并用非空参数覆盖当前线程的对应字段。
         * @param deviceId 新设备标识；为空时保留原设备标识。
         * @param taskId 新任务标识；为空时保留原任务标识。
         */
        Scope(const QString& deviceId = {}, const QString& taskId = {});
        /** @brief 恢复构造 Scope 前保存的设备和任务标识。 */
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
