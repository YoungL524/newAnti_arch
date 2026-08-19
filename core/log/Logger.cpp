#include "Logger.h"
#include "LogContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonObject>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief 返回进程内唯一的日志服务实例。
 * @return C++ 运行时保证线程安全初始化的函数局部静态对象。
 */
Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

/**
 * @brief 把结构化日志转换为 Web 客户端使用的 JSON 对象。
 * @return 始终包含 level、message、timestamp；taskId 和 deviceId 仅在非空时加入。
 */
QJsonObject LogRecord::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("level")] = level;
    j[QStringLiteral("message")] = message;
    j[QStringLiteral("timestamp")] = timestampMs;
    if (!taskId.isEmpty())
        j[QStringLiteral("taskId")] = taskId;
    if (!deviceId.isEmpty())
        j[QStringLiteral("deviceId")] = deviceId;
    return j;
}

/** @brief 析构日志单例，并在进程静态对象销毁阶段刷盘、关闭当前文件。 */
Logger::~Logger()
{
    shutdown();
}

/**
 * @brief Qt 全局消息处理器的静态入口。
 * @param type Qt 日志级别。
 * @param ctx Qt 提供的源码上下文；当前仅透传，尚不写入日志格式。
 * @param msg 日志正文。
 * @details 普通消息交给单例处理；致命消息完成输出后调用 abort()，不会静默继续运行。
 */
void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext& ctx,
                            const QString& msg)
{
    Logger::instance().handle(type, ctx, msg);
    if (type == QtFatalMsg) {
        abort();
    }
}

/**
 * @brief 创建日志目录、清理过期文件并打开当天日志文件。
 * @param logsDir 指定目录；为空时使用可执行文件旁的 `data/logs`。
 * @param keepDays 包含当天在内的保留天数；非正值回退为 7 天。
 * @return 当天文件成功打开时为 true；失败时仍标记已初始化并保留控制台日志能力。
 */
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

/**
 * @brief 刷盘并关闭当前日志文件。
 * @details 在文件互斥量内完成，可重复调用；关闭后 Qt 消息仍可输出到控制台，但不再写文件。
 */
void Logger::shutdown()
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_initialized = false;
}

/**
 * @brief 返回 QFile 当前绑定的日志路径。
 * @return 尚未初始化时通常为空；文件打开失败时仍可能返回尝试打开的目标路径。
 * @warning 本函数当前不加锁，宜在初始化完成后或外部已保证无并发轮转时用于诊断展示。
 */
QString Logger::currentLogFile() const
{
    return m_file.fileName();
}

/**
 * @brief 计算默认日志目录，使不同启动方式得到相同落盘位置。
 * @return 可执行文件目录下 `data/logs` 的规范化路径。
 */
QString Logger::locateLogsDir()
{
    // 始终落在 exe 旁边的 data/logs/，与 cwd 无关。
    // 这样 VSCode/QtCreator/双击/命令行 启动行为一致，方便排错。
    const QString exeDir = QCoreApplication::applicationDirPath();
    return QDir::cleanPath(exeDir + QStringLiteral("/data/logs"));
}

/**
 * @brief 确保日志文件指向指定日期，必要时完成跨日轮转。
 * @param today 用于构造文件名的本地日期。
 * @details 文件名为 `<应用名>-yyyy-MM-dd.log`；应用名为空时省略前缀。打开失败时写 stderr 并保持文件关闭。
 * @warning 调用者必须持有 m_mutex，避免多个日志线程同时关闭或替换 QFile。
 */
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

/**
 * @brief 按文件名中的日期删除保留窗口之前的日志。
 * @param logsDir 日志目录。
 * @param keepDays 包含当天在内的保留天数。
 * @details 同时识别 `yyyy-MM-dd.log` 和 `<应用名>-yyyy-MM-dd.log`；名称或日期无效的文件一律跳过。
 */
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

/**
 * @brief 将 Qt 日志和当前线程的 LogContext 格式化为持久化文本。
 * @param type Qt 日志级别。
 * @param msg 原始正文。
 * @param timestampMs Unix Epoch 毫秒时间戳。
 * @return `时间 [级别] 正文 [task=... device=...]` 格式且以换行结尾的字符串。
 */
QString Logger::formatLine(QtMsgType type, const QString& msg, qint64 timestampMs)
{
    const char* level = "INFO ";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO "; break;
    case QtWarningMsg:  level = "WARN "; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    const QString ts = QDateTime::fromMSecsSinceEpoch(timestampMs)
                           .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));

    QString contextSuffix;
    const QString taskId = LogContext::currentTaskId();
    const QString deviceId = LogContext::currentDeviceId();
    if (!taskId.isEmpty() || !deviceId.isEmpty()) {
        QStringList parts;
        if (!taskId.isEmpty())
            parts << QStringLiteral("task=%1").arg(taskId);
        if (!deviceId.isEmpty())
            parts << QStringLiteral("device=%1").arg(deviceId);
        contextSuffix = QStringLiteral(" [%1]").arg(parts.join(QStringLiteral(" ")));
    }

    return QStringLiteral("%1 [%2] %3%4\n").arg(ts, QString::fromLatin1(level), msg, contextSuffix);
}

#ifdef Q_OS_WIN
/**
 * @brief 选择 Windows 控制台输出句柄。
 * @param type Qt 日志级别。
 * @return WARN、ERROR、FATAL 对应标准错误，其余级别对应标准输出。
 */
static HANDLE consoleHandleForMessage(QtMsgType type)
{
    const bool err = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);
    return err ? GetStdHandle(STD_ERROR_HANDLE)
               : GetStdHandle(STD_OUTPUT_HANDLE);
}
#endif

/**
 * @brief 向控制台写出一条已格式化的完整日志。
 * @param type 用于选择 stdout 或 stderr 的 Qt 日志级别。
 * @param line 已包含末尾换行符的文本。
 * @details Windows 连接真实控制台时用 UTF-16 WriteConsoleW 保证中文；重定向到文件或管道时改写 UTF-8 字节。
 */
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

/**
 * @brief 处理一条 Qt 日志并分发到控制台、文件、外部订阅者和最近记录缓存。
 * @param type Qt 日志级别。
 * @param ctx Qt 源码上下文；当前实现未写入输出。
 * @param msg 原始正文。
 * @details 文件锁、回调锁和缓存锁互不嵌套。外部回调在无锁状态执行，避免 Web 分发回调再次记录日志时死锁。
 *          最近记录只保存 INFO 及以上级别；超过 200 条时一次淘汰最旧 50 条，限制长期运行的内存占用。
 */
void Logger::handle(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx);

    const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
    const QString line = formatLine(type, msg, timestampMs);

    // 控制台不需要锁（OS 自己保证 WriteConsole/fwrite 的原子写入足够好）
    writeConsole(type, line);

    QString levelStr;
    switch (type) {
    case QtDebugMsg:   levelStr = QStringLiteral("DEBUG"); break;
    case QtInfoMsg:    levelStr = QStringLiteral("INFO");  break;
    case QtWarningMsg: levelStr = QStringLiteral("WARN");  break;
    case QtCriticalMsg:levelStr = QStringLiteral("ERROR"); break;
    case QtFatalMsg:   levelStr = QStringLiteral("FATAL"); break;
    default:           levelStr = QStringLiteral("DEBUG"); break;
    }

    LogRecord record;
    record.level = levelStr;
    record.message = msg;
    record.taskId = LogContext::currentTaskId();
    record.deviceId = LogContext::currentDeviceId();
    record.timestampMs = timestampMs;

    {
        QMutexLocker lock(&m_mutex);
        if (m_initialized) {
            const QDate today = QDate::currentDate();
            if (today != m_currentDate)
                rotateIfNeededLocked(today);

            if (m_file.isOpen()) {
                m_file.write(line.toUtf8());
                m_file.flush();
            }
        }
    }

    std::function<void(const LogRecord&)> externalCb;
    {
        QMutexLocker cbLock(&m_cbMutex);
        externalCb = m_externalCb;
    }
    if (externalCb)
        externalCb(record);

    if (type >= QtInfoMsg) {
        QMutexLocker recentLock(&m_recentMutex);
        m_recentRecords.append(record);
        if (m_recentRecords.size() > 200)
            m_recentRecords.remove(0, 50);
    }
}

/**
 * @brief 从有界内存缓存末尾复制最近的结构化日志。
 * @param count 期望条数；大于缓存量时返回全部记录。
 * @return 按时间从旧到新排列的记录副本。
 */
QVector<LogRecord> Logger::recentLogRecords(int count) const
{
    QMutexLocker lock(&m_recentMutex);
    if (m_recentRecords.size() <= count)
        return m_recentRecords;
    return m_recentRecords.mid(m_recentRecords.size() - count);
}

/**
 * @brief 将最近的结构化日志转换为旧版二元组接口。
 * @param count 期望条数。
 * @return 每项仅保留日志级别和正文，不含时间、任务与设备标识。
 */
QVector<QPair<QString, QString>> Logger::recentLogs(int count) const
{
    QVector<QPair<QString, QString>> result;
    for (const auto& rec : recentLogRecords(count))
        result.append(qMakePair(rec.level, rec.message));
    return result;
}

/**
 * @brief 原子地替换日志外部分发回调。
 * @param cb 新回调；空回调表示取消订阅。
 * @details 仅在 m_cbMutex 内移动回调对象；实际调用发生在 handle() 的锁外，且执行线程就是产生日志的线程。
 */
void Logger::setExternalCallback(std::function<void(const LogRecord&)> cb)
{
    QMutexLocker lock(&m_cbMutex);
    m_externalCb = std::move(cb);
}
