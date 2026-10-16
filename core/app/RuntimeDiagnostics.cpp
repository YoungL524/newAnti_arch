#include "core/app/RuntimeDiagnostics.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMutexLocker>
#include <chrono>
#include <cstdio>

namespace core::app {
RuntimeDiagnostics *RuntimeDiagnostics::s_owner = nullptr;

/**
 * @brief main 的栈析构顺序保证运行时先停止，再卸载回调，不让存储线程借用已释放的诊断 Owner。
 */
RuntimeDiagnostics::~RuntimeDiagnostics()
{
    if (s_owner == this) {
        qInstallMessageHandler(m_previous);
        s_owner = nullptr;
    }
}

/**
 * @brief 启动时先检查文件可写，失败不安装回调；跨进程文件锁只用于短暂写入，不充当数据库独占锁。
 */
bool RuntimeDiagnostics::open(const QString &path)
{
    if (s_owner || path.isEmpty()) return false;
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(1000)) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    file.close();
    m_path = path;
    s_owner = this;
    m_previous = qInstallMessageHandler(&RuntimeDiagnostics::handle);
    return true;
}

/**
 * @brief 只持久化警告、错误及致命诊断；Info 接收计数不写文件，原 stderr 输出行为保持可用。
 */
void RuntimeDiagnostics::handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    auto *owner = s_owner;
    if (!owner) return;
    static thread_local bool writing = false; ///< 文件 API 的嵌套 Qt 诊断只走原输出，防止重复锁住同线程互斥量。
    if (!writing && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)) {
        writing = true;
        owner->append(type, context, message);
        writing = false;
    }
    if (owner->m_previous) owner->m_previous(type, context, message);
    else {
        const QByteArray text = qFormatLogMessage(type, context, message).toLocal8Bit();
        std::fprintf(stderr, "%s\n", text.constData());
        std::fflush(stderr);
    }
}

/**
 * @brief 同进程串行化、跨进程非阻塞争锁；轮转失败即放弃本条，保证不因排障文件无限增长或形成重试链。
 */
void RuntimeDiagnostics::append(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QMutexLocker guard(&m_mutex);
    const qint64 second = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (second != m_windowSecond) { m_windowSecond = second; m_writtenInWindow = 0; }
    if (type != QtFatalMsg && m_writtenInWindow >= 5) { ++m_suppressed; return; }
    ++m_writtenInWindow;
    QString line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral(" pid=%1 type=%2 suppressed=%3 ").arg(QCoreApplication::applicationPid())
            .arg(int(type)).arg(m_suppressed)
        + qFormatLogMessage(type, context, message.left(2048));
    line.replace('\r', ' '); line.replace('\n', ' ');
    const QByteArray bytes = line.left(1800).toUtf8() + '\n';
    QLockFile lock(m_path + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return;
    constexpr qint64 maximumBytes = 1024 * 1024;
    if (QFileInfo(m_path).size() + bytes.size() > maximumBytes) {
        const QString backup = m_path + QStringLiteral(".1");
        if (QFile::exists(backup) && !QFile::remove(backup)) return;
        if (!QFile::rename(m_path, backup)) return;
    }
    QFile file(m_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append) && file.write(bytes) == bytes.size())
        m_suppressed = 0;
}

} // namespace core::app
