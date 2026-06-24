#pragma once

#include <QString>
#include <QFile>
#include <QMutex>
#include <QDate>
#include <QVector>
#include <QPair>
#include <QJsonObject>
#include <QtMsgHandler>
#include <functional>

struct LogRecord {
    QString level;
    QString message;
    QString taskId;
    QString deviceId;
    qint64  timestampMs = 0;

    QJsonObject toJson() const;
};

/**
 * @file Logger.h
 * @brief 全局日志门面（单例）
 * @details
 *  - 安装为 Qt 消息处理器（qInstallMessageHandler），统一接管 qDebug/qInfo/qWarning/qCritical/qFatal
 *  - 每条日志同时写入：1) 控制台（Windows 下走 WriteConsoleW，避免中文乱码） 2) 当日日志文件
 *  - 文件名按日期：data/logs/YYYY-MM-DD.log
 *  - 跨天自动切换文件；启动时自动删除超过 keepDays（默认 7）天的旧日志
 *  - 线程安全（QMutex 保护文件写）
 *
 * 使用：
 * @code
 *   Logger::instance().init();              // 自动定位 data/logs
 *   qInstallMessageHandler(Logger::messageHandler);
 *   qInfo() << "started";
 * @endcode
 */
class Logger
{
public:
    /**
     * @brief 获取全局单例
     */
    static Logger& instance();

    /**
     * @brief 安装为 Qt 消息处理器使用的回调
     * @details 与 qInstallMessageHandler 配套
     */
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext& ctx,
                               const QString& msg);

    /**
     * @brief 初始化日志目录与首个日志文件
     * @param logsDir 可选；为空时使用 <exeDir>/data/logs（与 cwd 无关，跨启动方式稳定）
     * @param keepDays 保留天数；早于该天数的 .log 文件启动时清理；默认 7
     * @return 是否成功打开文件；失败时仍可继续运行（仅控制台输出）
     */
    bool init(const QString& logsDir = QString(), int keepDays = 7);

    /**
     * @brief 关闭并刷盘当前日志文件
     */
    void shutdown();

    /**
     * @brief 当前日志文件路径（未初始化时为空）
     */
    QString currentLogFile() const;

    /**
     * @brief 注册外部回调：每条日志写入后调用 (线程安全: 回调在 mutex 外执行)
     */
    void setExternalCallback(std::function<void(const LogRecord&)> cb);

    /// 获取最近 N 条结构化日志 (线程安全)
    QVector<LogRecord> recentLogRecords(int count = 50) const;

    /**
     * @brief 获取最近 N 条日志 (线程安全，兼容旧接口)
     */
    QVector<QPair<QString, QString>> recentLogs(int count = 50) const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief 处理一条日志（写控制台 + 写文件）
     */
    void handle(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

    /**
     * @brief 自动定位 data/logs/，参考 ConfigManager 的搜索顺序
     */
    static QString locateLogsDir();

    /**
     * @brief 按当天日期打开/切换日志文件；调用前必须持有 m_mutex
     * @param today 当天日期
     */
    void rotateIfNeededLocked(const QDate& today);

    /**
     * @brief 删除 logsDir 中早于 cutoff 日期的 *.log；启动时调用一次
     */
    void pruneOldLogs(const QString& logsDir, int keepDays);

    /**
     * @brief 把单条文本写到控制台（保留原有 Windows Unicode 输出逻辑）
     */
    static void writeConsole(QtMsgType type, const QString& line);

    /**
     * @brief 拼装一条标准日志：时间 [级别] msg\n
     */
    static QString formatLine(QtMsgType type, const QString& msg);

    QMutex   m_mutex;
    QString  m_logsDir;
    QFile    m_file;
    QDate    m_currentDate;
    int      m_keepDays = 7;
    bool     m_initialized = false;
    std::function<void(const LogRecord&)> m_externalCb;
    QMutex   m_cbMutex;                ///< 保护 m_externalCb 读写

    // 环形缓冲区：保存最近日志用于 WS 初连接推送
    mutable QMutex m_recentMutex;
    QVector<LogRecord> m_recentRecords;
};
