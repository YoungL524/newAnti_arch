/**
 * @file RuntimeDiagnostics.h
 * @brief 保存隐藏控制台时的 Qt 警告和错误诊断，不替代 SystemEvent 业务日志。
 * @details main 在启动运行时前安装、运行时销毁后卸载；输入为 Qt 消息，输出为数据库旁的有界文本文件。
 * 仅排障使用，不读取 Topic、不拥有业务状态、不重试设备操作。入口为 open()。
 */
#pragma once

#include <QMutex>
#include <QString>
#include <QtGlobal>

namespace core::app {

/** @brief 进程唯一 Qt 诊断入口；所有线程同步写入，文件与备份各最多 1 MiB，无后台线程或待写队列。 */
class RuntimeDiagnostics final
{
public:
    /** @brief 建立未安装的诊断 Owner，必须在任何工作线程启动前构造。 */
    RuntimeDiagnostics() = default;
    /** @brief 所有工作线程停止后恢复原 Qt handler；对象不得早于 ApplicationRuntime 析构。 */
    ~RuntimeDiagnostics();
    /** @brief 禁止复制进程唯一消息处理器所有权。 */
    RuntimeDiagnostics(const RuntimeDiagnostics &) = delete;
    /** @brief 禁止赋值转移全局 Qt handler 注册关系。 */
    RuntimeDiagnostics &operator=(const RuntimeDiagnostics &) = delete;
    /** @brief 在主线程打开 UTF-8 诊断文件并安装 handler；失败不改变原 handler，调用方报告启动失败。 */
    bool open(const QString &path);

private:
    /** @brief Qt 回调入口；每秒最多写五条警告/错误，Fatal 不抑制；原控制台 handler 仍接收全部消息。 */
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message);
    /** @brief 锁内写入最多 8 KiB 的单行诊断；轮转/锁竞争/写失败丢弃诊断，绝不递归生成 Qt 消息。 */
    void append(QtMsgType type, const QMessageLogContext &context, const QString &message);

    static RuntimeDiagnostics *s_owner; ///< Qt 全局回调借用；安装前尚无工作线程，卸载前工作线程已全部停止。
    QString m_path; ///< 数据库路径加诊断后缀；不主动附加协议载荷或令牌，UTF-8 文件不承担业务归档。
    QtMessageHandler m_previous = nullptr; ///< 被替换的 handler，卸载时恢复；空值表示 Qt 默认控制台输出。
    QMutex m_mutex; ///< 保护写文件、轮转和限频计数的短低频诊断临界区，不持有 Topic 锁。
    qint64 m_windowSecond = -1; ///< steady_clock 秒窗口，禁止墙钟回拨改变限频。
    int m_writtenInWindow = 0; ///< 当前单调秒窗口已尝试写入数，上限五条，Fatal 除外。
    quint64 m_suppressed = 0; ///< 限频抑制累计数，在下一条实际尝试输出中报告。
};

} // namespace core::app
