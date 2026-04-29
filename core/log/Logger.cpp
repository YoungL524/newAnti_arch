#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::~Logger()
{
    shutdown();
}

void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext& ctx,
                            const QString& msg)
{
    Logger::instance().handle(type, ctx, msg);
    if (type == QtFatalMsg) {
        abort();
    }
}

bool Logger::init(const QString& logsDir, int keepDays)
{
    QMutexLocker lock(&m_mutex);

    m_keepDays = keepDays > 0 ? keepDays : 7;
    m_logsDir = logsDir.isEmpty() ? locateLogsDir() : logsDir;

    QDir().mkpath(m_logsDir);
    pruneOldLogs(m_logsDir, m_keepDays);

    m_currentDate = QDate::currentDate();
    rotateIfNeededLocked(m_currentDate);

    m_initialized = true;
    return m_file.isOpen();
}

void Logger::shutdown()
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_initialized = false;
}

QString Logger::currentLogFile() const
{
    return m_file.fileName();
}

QString Logger::locateLogsDir()
{
    // 始终落在 exe 旁边的 data/logs/，与 cwd 无关。
    // 这样 VSCode/QtCreator/双击/命令行 启动行为一致，方便排错。
    const QString exeDir = QCoreApplication::applicationDirPath();
    return QDir::cleanPath(exeDir + QStringLiteral("/data/logs"));
}

void Logger::rotateIfNeededLocked(const QDate& today)
{
    const QString date = today.toString(QStringLiteral("yyyy-MM-dd"));
    const QString appName = QCoreApplication::applicationName().trimmed();
    const QString fileName = appName.isEmpty()
                                 ? (date + QStringLiteral(".log"))
                                 : (appName + QStringLiteral("-") + date + QStringLiteral(".log"));
    const QString path = m_logsDir + QStringLiteral("/") + fileName;

    if (m_file.isOpen() && m_file.fileName() == path) {
        return;
    }

    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // 打开失败：仅控制台输出
        std::fprintf(stderr, "[Logger] 无法打开日志文件: %s\n",
                     path.toLocal8Bit().constData());
        return;
    }
    m_currentDate = today;
}

void Logger::pruneOldLogs(const QString& logsDir, int keepDays)
{
    QDir dir(logsDir);
    if (!dir.exists()) {
        return;
    }

    const QDate cutoff = QDate::currentDate().addDays(-keepDays + 1);
    // 兼容两种命名：YYYY-MM-DD.log 与 <app>-YYYY-MM-DD.log
    const QRegularExpression re(QStringLiteral(R"(^(?:.+-)?(\d{4}-\d{2}-\d{2})\.log$)"));

    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.log"),
                                                  QDir::Files);
    for (const QFileInfo& fi : files) {
        const auto m = re.match(fi.fileName());
        if (!m.hasMatch()) {
            continue;
        }
        const QDate d = QDate::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd"));
        if (!d.isValid()) {
            continue;
        }
        if (d < cutoff) {
            QFile::remove(fi.absoluteFilePath());
        }
    }
}

QString Logger::formatLine(QtMsgType type, const QString& msg)
{
    const char* level = "INFO ";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO "; break;
    case QtWarningMsg:  level = "WARN "; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    const QString ts = QDateTime::currentDateTime()
                           .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    return QStringLiteral("%1 [%2] %3\n").arg(ts, QString::fromLatin1(level), msg);
}

#ifdef Q_OS_WIN
static HANDLE consoleHandleForMessage(QtMsgType type)
{
    const bool err = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);
    return err ? GetStdHandle(STD_ERROR_HANDLE)
               : GetStdHandle(STD_OUTPUT_HANDLE);
}
#endif

void Logger::writeConsole(QtMsgType type, const QString& line)
{
#ifdef Q_OS_WIN
    HANDLE handle = consoleHandleForMessage(type);
    DWORD mode = 0;
    if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        DWORD written = 0;
        WriteConsoleW(handle,
                      reinterpret_cast<const wchar_t*>(line.utf16()),
                      static_cast<DWORD>(line.size()),
                      &written, nullptr);
        return;
    }
#endif
    const QByteArray bytes = line.toUtf8();
    const bool err = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);
    FILE* out = err ? stderr : stdout;
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), out);
    std::fflush(out);
}

void Logger::handle(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx);

    const QString line = formatLine(type, msg);

    // 控制台不需要锁（OS 自己保证 WriteConsole/fwrite 的原子写入足够好）
    writeConsole(type, line);

    QMutexLocker lock(&m_mutex);
    if (!m_initialized) {
        return;
    }

    // 跨天则切换文件
    const QDate today = QDate::currentDate();
    if (today != m_currentDate) {
        rotateIfNeededLocked(today);
    }

    if (!m_file.isOpen()) {
        return;
    }

    const QByteArray bytes = line.toUtf8();
    m_file.write(bytes);
    m_file.flush();  // 现场排错优先：每条都 flush，避免崩溃丢日志
}
