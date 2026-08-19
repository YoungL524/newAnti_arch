/**
 * @file Logger.h
 * @brief 声明核心进程统一的结构化日志记录与持久化服务。
 * @details Logger 接管 Qt 全局消息，向控制台和按日文件输出同一条日志，并保留一小段内存记录供 Web 客户端补发。
 *          LogContext 提供的任务、设备标识会随记录保存，便于跨模块定位一条业务链路。
 */

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

/** @brief 可供文件输出、Web 推送和诊断查询复用的一条结构化日志。 */
struct LogRecord {
    QString level;
    QString message;
    QString taskId;
    QString deviceId;
    qint64  timestampMs = 0;

    /** @brief 转换为 Web API 使用的 JSON；空任务或设备标识不会产生对应字段。 */
    QJsonObject toJson() const;
};

/**
 * @class Logger
 * @brief 全局日志门面（单例）
 * @details
 *  - 安装为 Qt 消息处理器（qInstallMessageHandler），统一接管 qDebug/qInfo/qWarning/qCritical/qFatal
 *  - 每条日志同时写入：1) 控制台（Windows 下走 WriteConsoleW，避免中文乱码） 2) 当日日志文件
 *  - 文件名按日期：data/logs/YYYY-MM-DD.log
 *  - 跨天自动切换文件；启动时自动删除超过 keepDays（默认 7）天的旧日志
 *  - 文件、外部回调和最近记录分别由独立互斥量保护，允许来自多个 Qt 线程的并发日志
 *  - 外部回调在 Logger 的互斥量之外执行，回调可安全地产生新的日志而不会自锁
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
     * @brief 获取进程内唯一的 Logger 实例。
     * @return 函数局部静态对象；首次调用时创建，进程退出时析构。
     */
    static Logger& instance();

    /**
     * @brief 接收 Qt 全局日志并转交给单例处理。
     * @details 传给 qInstallMessageHandler()；QtFatalMsg 写出后调用 abort()，保持 Qt 致命日志语义。
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
     * @brief 注册或清除每条日志的外部分发回调。
     * @param cb 新回调；传入空函数可停止分发。回调可能在任意产生日志的线程中执行。
     */
    void setExternalCallback(std::function<void(const LogRecord&)> cb);

    /**
     * @brief 取得内存中最新的若干条结构化日志。
     * @param count 期望条数；超过当前缓存量时返回全部缓存。
     * @return 按产生时间从旧到新排列的副本，仅包含 INFO 及以上级别。
     */
    QVector<LogRecord> recentLogRecords(int count = 50) const;

    /**
     * @brief 取得仅含级别和正文的最近日志，兼容旧调用方。
     * @param count 期望条数。
     * @return 从 recentLogRecords() 投影得到的 `(level, message)` 列表。
     */
    QVector<QPair<QString, QString>> recentLogs(int count = 50) const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief 完成一条 Qt 日志的格式化、输出、分发与短期缓存。
     * @details 文件输出按日期轮转并立即 flush；外部回调不持锁执行；INFO 以下日志不进入最近记录缓存。
     */
    void handle(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

    /**
     * @brief 计算与可执行文件位置绑定的默认日志目录。
     * @return `<applicationDir>/data/logs` 的规范化路径，不受当前工作目录影响。
     */
    static QString locateLogsDir();

    /**
     * @brief 打开当天日志文件，或在日期改变时关闭旧文件并切换。
     * @param today 当天日期
     * @warning 调用者必须持有 m_mutex；打开失败只降级为控制台输出，不抛出异常。
     */
    void rotateIfNeededLocked(const QDate& today);

    /**
     * @brief 删除日志目录中超出保留窗口且文件名日期可识别的日志。
     * @param logsDir 待清理目录。
     * @param keepDays 包含当天在内的保留天数。
     * @details 不能匹配日期命名规则的 `.log` 文件不会删除，避免误删人工或其他程序生成的文件。
     */
    void pruneOldLogs(const QString& logsDir, int keepDays);

    /**
     * @brief 将完整格式化文本写入与日志级别对应的控制台流。
     * @details Windows 控制台优先使用 WriteConsoleW；输出被重定向时退回 UTF-8 stdout/stderr。
     */
    static void writeConsole(QtMsgType type, const QString& line);

    /**
     * @brief 生成包含时间、级别、正文和可选业务上下文的单行文本。
     * @param type Qt 日志级别。
     * @param msg 原始日志正文。
     * @param timestampMs Unix Epoch 毫秒时间戳。
     * @return 以换行符结尾的日志文本。
     */
    static QString formatLine(QtMsgType type, const QString& msg, qint64 timestampMs);

    QMutex   m_mutex;
    QString  m_logsDir;
    QFile    m_file;
    QDate    m_currentDate;
    int      m_keepDays = 7;
    bool     m_initialized = false;
    std::function<void(const LogRecord&)> m_externalCb;
    QMutex   m_cbMutex;                ///< 保护回调对象的替换与复制，不覆盖回调执行阶段。

    /// 最近日志的有界缓存；超过 200 条时批量淘汰最旧 50 条，供 WebSocket 初次连接补发。
    mutable QMutex m_recentMutex;
    QVector<LogRecord> m_recentRecords;
};
