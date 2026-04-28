#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonObject>
#include <QVariantMap>
#include <QVector>

/**
 * @file DataStore.h
 * @brief 通用数据库存储层
 * @details 数据访问层组件。封装 QSqlDatabase，提供对 PostgreSQL 的通用操作。
 *          不包含任何业务语义（不知道"航迹"或"校准"是什么），
 *          仅提供表级别的 CRUD 操作，业务语义由上层（业务层）定义。
 *
 * 使用方式：
 * @code
 *   DataStore store;
 *   QJsonObject dbCfg = configManager.getConfig("database");
 *   store.connectFromConfig(dbCfg);
 *
 *   // 建表（业务层定义表结构）
 *   store.ensureTable("tracks",
 *       "CREATE TABLE IF NOT EXISTS tracks ("
 *       "  id SERIAL PRIMARY KEY,"
 *       "  timestamp BIGINT NOT NULL,"
 *       "  target_id INTEGER,"
 *       "  range DOUBLE PRECISION,"
 *       "  azimuth DOUBLE PRECISION"
 *       ")");
 *
 *   // 插入
 *   store.insert("tracks", {{"timestamp", 1234}, {"target_id", 1}, {"range", 5000.0}});
 *
 *   // 查询
 *   auto rows = store.query("SELECT * FROM tracks WHERE target_id = ?", {1});
 * @endcode
 *
 * 依赖：Qt SQL 模块（QT += sql）、PostgreSQL 客户端库（libpq）
 */
class DataStore : public QObject
{
    Q_OBJECT

public:
    explicit DataStore(QObject* parent = nullptr);
    ~DataStore() override;

    // ========== 连接管理 ==========

    /**
     * @brief 从参数连接数据库
     * @param driver Qt SQL 驱动名（如 "QPSQL"）
     * @param host 主机地址
     * @param port 端口
     * @param dbName 数据库名
     * @param user 用户名
     * @param password 密码
     * @return 是否连接成功
     */
    bool connectToDatabase(const QString& driver,
                           const QString& host, int port,
                           const QString& dbName,
                           const QString& user, const QString& password);

    /**
     * @brief 连接 SQLite 数据库（文件式，无需网络参数）
     * @param dbPath 数据库文件路径（绝对或相对路径；父目录不存在会自动创建）
     * @return 是否连接成功
     * @note SQLite 是 Qt 内置驱动（QSQLITE），无需额外依赖
     */
    bool connectToSqlite(const QString& dbPath);

    /**
     * @brief 从 JSON 配置对象连接数据库
     * @param config JSON 对象，按 driver 字段自动分发：
     *               - QSQLITE：只需 database（文件路径）
     *               - QPSQL 等：需 host/port/database/user/password
     * @return 是否连接成功
     * @see config/database.json
     */
    bool connectFromConfig(const QJsonObject& config);

    /**
     * @brief 断开数据库连接
     */
    void disconnect();

    /**
     * @brief 是否已连接
     */
    bool isConnected() const;

    // ========== 表管理 ==========

    /**
     * @brief 确保表存在（不存在则创建）
     * @param tableName 表名（仅用于日志）
     * @param createSql 完整的 CREATE TABLE IF NOT EXISTS 语句
     * @return 是否成功
     */
    bool ensureTable(const QString& tableName, const QString& createSql);

    /**
     * @brief 检查表是否存在
     * @param tableName 表名
     * @return 是否存在
     */
    bool tableExists(const QString& tableName) const;

    // ========== 数据操作 ==========

    /**
     * @brief 插入一条记录
     * @param tableName 表名
     * @param record 键值对（列名→值）
     * @return 是否成功
     */
    bool insert(const QString& tableName, const QVariantMap& record);

    /**
     * @brief 批量插入记录
     * @param tableName 表名
     * @param records 多条记录
     * @return 是否全部成功
     */
    bool insertBatch(const QString& tableName, const QVector<QVariantMap>& records);

    /**
     * @brief 执行带参数的 SELECT 查询
     * @param sql 完整 SQL 语句（可含 ? 占位符）
     * @param bindValues 绑定参数列表
     * @return 查询结果（每行一个 QVariantMap）
     */
    QVector<QVariantMap> query(const QString& sql,
                               const QVariantList& bindValues = QVariantList());

    /**
     * @brief 执行非查询 SQL（INSERT/UPDATE/DELETE/DDL）
     * @param sql SQL 语句
     * @param bindValues 绑定参数列表
     * @return 是否成功
     */
    bool execute(const QString& sql,
                 const QVariantList& bindValues = QVariantList());

    /**
     * @brief 获取底层 QSqlDatabase（高级用法）
     * @return 数据库对象引用
     */
    QSqlDatabase database() const;

signals:
    /**
     * @brief 数据库连接成功
     */
    void connected();

    /**
     * @brief 数据库已断开
     */
    void disconnected();

    /**
     * @brief 数据库操作出错
     * @param message 错误描述
     */
    void errorOccurred(const QString& message);

private:
    /**
     * @brief 报告错误（发信号 + 日志）
     * @param context 错误上下文
     * @param detail 详细信息
     */
    void reportError(const QString& context, const QString& detail);

    /**
     * @brief 安全移除当前数据库连接
     * @details 会先关闭连接并释放局部引用，再执行 `QSqlDatabase::removeDatabase()`。
     */
    void removeConnection();

    /**
     * @brief 为查询对象执行预处理并绑定参数
     * @param query 查询对象
     * @param context 错误上下文
     * @param sql SQL 语句
     * @param bindValues 绑定参数
     * @return 是否成功
     */
    bool prepareQuery(QSqlQuery& query,
                      const QString& context,
                      const QString& sql,
                      const QVariantList& bindValues = QVariantList());

    /**
     * @brief 执行查询对象并统一处理错误日志
     * @param query 查询对象
     * @param context 错误上下文
     * @param detailPrefix 错误详情前缀
     * @return 是否成功
     */
    bool execQuery(QSqlQuery& query,
                   const QString& context,
                   const QString& detailPrefix = QString());

    /**
     * @brief 执行简单 SQL 语句
     * @param db 数据库连接
     * @param context 错误上下文
     * @param sql SQL 语句
     * @param detailPrefix 错误详情前缀
     * @return 是否成功
     */
    bool executeStatement(QSqlDatabase& db,
                          const QString& context,
                          const QString& sql,
                          const QString& detailPrefix = QString());

    /**
     * @brief 打开数据库连接并统一处理失败路径
     * @param db 数据库连接
     * @param context 错误上下文
     * @param detailPrefix 错误详情前缀
     * @return 是否成功
     */
    bool openDatabase(QSqlDatabase& db,
                      const QString& context,
                      const QString& detailPrefix = QString());

    /**
     * @brief 开启事务并统一处理错误日志
     * @param db 数据库连接
     * @param context 错误上下文
     * @return 是否成功
     */
    bool beginTransaction(QSqlDatabase& db, const QString& context);

    /**
     * @brief 提交事务并统一处理错误日志
     * @param db 数据库连接
     * @param context 错误上下文
     * @return 是否成功
     */
    bool commitTransaction(QSqlDatabase& db, const QString& context);

    /**
     * @brief 回滚当前事务并记录失败日志
     * @param db 数据库连接
     * @param context 错误上下文
     */
    void rollbackTransaction(QSqlDatabase& db, const QString& context);

    QString m_connectionName;   ///< 数据库连接名（支持多实例）
    bool m_connected = false;   ///< 连接状态标志

    static int s_connectionCounter;  ///< 全局连接计数器（生成唯一连接名）
};
