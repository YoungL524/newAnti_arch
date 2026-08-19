#include "DataStore.h"
#include <QSqlError>
#include <QSqlRecord>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

int DataStore::s_connectionCounter = 0;

/**
 * @brief 分配唯一 Qt SQL 连接名，使同一进程多个线程/实例互不覆盖。
 */
DataStore::DataStore(QObject* parent)
    : QObject(parent)
{
    // 生成唯一连接名，避免多个 DataStore 实例冲突
    m_connectionName = QStringLiteral("DataStore_%1").arg(++s_connectionCounter);
}

/** @brief 关闭连接并满足 QSqlDatabase::removeDatabase 对引用释放顺序的要求。 */
DataStore::~DataStore()
{
    disconnect();
}

// ========== 连接管理 ==========

/**
 * @brief 使用网络数据库参数创建具名 Qt SQL 连接并统一处理打开失败清理。
 * @details 驱动不可用时不登记连接；open 失败时先释放局部 QSqlDatabase 句柄，再从全局注册表移除。
 */
bool DataStore::connectToDatabase(const QString& driver,
                                  const QString& host, int port,
                                  const QString& dbName,
                                  const QString& user, const QString& password)
{
    // 检查驱动是否可用
    if (!QSqlDatabase::isDriverAvailable(driver)) {
        reportError(QStringLiteral("连接"),
                    QStringLiteral("SQL 驱动 '%1' 不可用。已安装的驱动: %2")
                    .arg(driver, QSqlDatabase::drivers().join(QStringLiteral(", "))));
        return false;
    }

    // 创建连接
    QSqlDatabase db = QSqlDatabase::addDatabase(driver, m_connectionName);
    db.setHostName(host);
    db.setPort(port);
    db.setDatabaseName(dbName);
    db.setUserName(user);
    db.setPassword(password);

    if (!openDatabase(db,
                      QStringLiteral("连接"),
                      QStringLiteral("无法连接到 %1:%2/%3").arg(host).arg(port).arg(dbName))) {
        db = QSqlDatabase();
        removeConnection();
        return false;
    }

    m_connected = true;
    qDebug() << QStringLiteral("[DataStore] 已连接: %1:%2/%3 (驱动=%4)")
                .arg(host).arg(port).arg(dbName, driver);
    emit connected();
    return true;
}

/**
 * @brief 创建 SQLite 文件父目录，打开连接并强制启用 WAL 与外键约束。
 * @details 任一 PRAGMA 失败都视为连接初始化失败并移除连接，避免上层在错误并发或完整性模式下运行。
 */
bool DataStore::connectToSqlite(const QString& dbPath)
{
    if (dbPath.isEmpty()) {
        reportError(QStringLiteral("连接"), QStringLiteral("SQLite 路径为空"));
        return false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        reportError(QStringLiteral("连接"),
                    QStringLiteral("QSQLITE 驱动不可用。已安装的驱动: %1")
                    .arg(QSqlDatabase::drivers().join(QStringLiteral(", "))));
        return false;
    }

    // 确保父目录存在（SQLite 不会自动创建）
    QFileInfo fi(dbPath);
    QDir parentDir = fi.absoluteDir();
    if (!parentDir.exists()) {
        if (!parentDir.mkpath(QStringLiteral("."))) {
            reportError(QStringLiteral("连接"),
                        QStringLiteral("无法创建数据库目录: %1").arg(parentDir.absolutePath()));
            return false;
        }
        qDebug() << QStringLiteral("[DataStore] 已创建目录: %1").arg(parentDir.absolutePath());
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);

    if (!openDatabase(db,
                      QStringLiteral("连接"),
                      QStringLiteral("无法打开 SQLite 数据库 '%1'").arg(dbPath))) {
        db = QSqlDatabase();
        removeConnection();
        return false;
    }

    // SQLite 性能优化：启用 WAL 模式、外键约束
    if (!executeStatement(db,
                          QStringLiteral("连接"),
                          QStringLiteral("PRAGMA journal_mode=WAL"),
                          QStringLiteral("设置 SQLite WAL 模式失败")) ||
        !executeStatement(db,
                          QStringLiteral("连接"),
                          QStringLiteral("PRAGMA foreign_keys=ON"),
                          QStringLiteral("启用 SQLite 外键约束失败"))) {
        db = QSqlDatabase();
        removeConnection();
        return false;
    }

    m_connected = true;
    qDebug() << QStringLiteral("[DataStore] 已连接 SQLite: %1").arg(fi.absoluteFilePath());
    emit connected();
    return true;
}

/**
 * @brief 按 driver 分派文件数据库或网络数据库参数，并支持从环境变量读取密码。
 * @details passwordEnv 非空时覆盖 JSON 中的 password，避免生产凭据必须明文落盘。
 */
bool DataStore::connectFromConfig(const QJsonObject& config)
{
    QString driver = config[QStringLiteral("driver")].toString(QStringLiteral("QSQLITE"));

    // SQLite：文件式，只需路径
    if (driver == QStringLiteral("QSQLITE")) {
        QString dbPath = config[QStringLiteral("database")].toString();
        return connectToSqlite(dbPath);
    }

    // 网络式数据库（PostgreSQL、MySQL 等）
    QString host     = config[QStringLiteral("host")].toString(QStringLiteral("localhost"));
    int     port     = config[QStringLiteral("port")].toInt(5432);
    QString dbName   = config[QStringLiteral("database")].toString();
    QString user     = config[QStringLiteral("user")].toString();
    QString password = config[QStringLiteral("password")].toString();
    const QString passwordEnv = config[QStringLiteral("passwordEnv")].toString();
    if (!passwordEnv.isEmpty())
        password = qEnvironmentVariable(passwordEnv.toUtf8().constData());

    return connectToDatabase(driver, host, port, dbName, user, password);
}

/**
 * @brief 幂等关闭并移除具名连接，随后更新逻辑状态和发出 disconnected。
 */
void DataStore::disconnect()
{
    if (!m_connected) {
        return;
    }

    removeConnection();

    m_connected = false;
    qDebug() << QStringLiteral("[DataStore] 已断开连接");
    emit disconnected();
}

/**
 * @brief 返回本实例是否完成连接初始化；不额外发起网络探活查询。
 */
bool DataStore::isConnected() const
{
    return m_connected;
}

// ========== 表管理 ==========

/**
 * @brief 在当前连接执行由业务层提供的幂等建表 SQL，并用 tableName 关联诊断日志。
 */
bool DataStore::ensureTable(const QString& tableName, const QString& createSql)
{
    if (!m_connected) {
        reportError(QStringLiteral("建表"), QStringLiteral("数据库未连接"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!executeStatement(db,
                          QStringLiteral("建表"),
                          createSql,
                          QStringLiteral("表 '%1' 创建失败").arg(tableName))) {
        return false;
    }

    qDebug() << QStringLiteral("[DataStore] 表已就绪: %1").arg(tableName);
    return true;
}

/**
 * @brief 通过当前驱动的元数据表列表检查精确表名；未连接时返回 false。
 */
bool DataStore::tableExists(const QString& tableName) const
{
    if (!m_connected) {
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    return db.tables().contains(tableName);
}

// ========== 数据操作 ==========

/**
 * @brief 从 QVariantMap 的键构造列列表，值全部使用位置占位符绑定后插入一行。
 * @note tableName 和列名来自内部 schema 代码，不接受未经校验的外部标识符。
 */
bool DataStore::insert(const QString& tableName, const QVariantMap& record)
{
    if (!m_connected) {
        reportError(QStringLiteral("插入"), QStringLiteral("数据库未连接"));
        return false;
    }

    if (record.isEmpty()) {
        reportError(QStringLiteral("插入"), QStringLiteral("插入记录为空"));
        return false;
    }

    // 构造 INSERT INTO tableName (col1, col2, ...) VALUES (?, ?, ...)
    QStringList columns;
    QStringList placeholders;
    QVariantList values;

    for (auto it = record.constBegin(); it != record.constEnd(); ++it) {
        columns << it.key();
        placeholders << QStringLiteral("?");
        values << it.value();
    }

    QString sql = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                  .arg(tableName,
                       columns.join(QStringLiteral(", ")),
                       placeholders.join(QStringLiteral(", ")));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!prepareQuery(q, QStringLiteral("插入"), sql, values)) {
        return false;
    }

    if (!execQuery(q, QStringLiteral("插入"), QStringLiteral("表 '%1' 插入失败").arg(tableName))) {
        return false;
    }

    return true;
}

/**
 * @brief 在单个事务中逐行复用 insert()，实现全批成功或回滚。
 * @details 任一行失败立即回滚；commit 失败也尝试回滚。该实现重视一致性，尚未使用驱动级 execBatch。
 */
bool DataStore::insertBatch(const QString& tableName, const QVector<QVariantMap>& records)
{
    if (!m_connected) {
        reportError(QStringLiteral("批量插入"), QStringLiteral("数据库未连接"));
        return false;
    }

    if (records.isEmpty()) {
        reportError(QStringLiteral("批量插入"), QStringLiteral("记录列表为空"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    // 使用事务提高批量插入性能
    if (!beginTransaction(db, QStringLiteral("批量插入"))) {
        return false;
    }

    for (const auto& record : records) {
        if (!insert(tableName, record)) {
            rollbackTransaction(db, QStringLiteral("批量插入"));
            return false;
        }
    }

    if (!commitTransaction(db, QStringLiteral("批量插入"))) {
        rollbackTransaction(db, QStringLiteral("批量插入"));
        return false;
    }

    return true;
}

/**
 * @brief 预处理并执行 SELECT，把每行按驱动返回的字段名转换为 QVariantMap。
 * @details 未连接、prepare 或 exec 失败均返回空集合，因此调用方需要结合 errorOccurred 区分“无结果”和失败。
 */
QVector<QVariantMap> DataStore::query(const QString& sql,
                                      const QVariantList& bindValues)
{
    QVector<QVariantMap> results;

    if (!m_connected) {
        reportError(QStringLiteral("查询"), QStringLiteral("数据库未连接"));
        return results;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!prepareQuery(q, QStringLiteral("查询"), sql, bindValues)) {
        return results;
    }

    if (!execQuery(q, QStringLiteral("查询"), QStringLiteral("SQL 执行失败"))) {
        return results;
    }

    // 将结果集转换为 QVariantMap 列表
    QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < rec.count(); ++i) {
            row.insert(rec.fieldName(i), q.value(i));
        }
        results.append(row);
    }

    return results;
}

/**
 * @brief 执行无结果 SQL；无绑定值时直接 exec，有参数时使用预处理绑定。
 * @details PostgreSQL 不允许部分 DDL 进入服务端 PREPARE，故无参数语句直接执行以兼容 CREATE INDEX 等操作。
 */
bool DataStore::execute(const QString& sql, const QVariantList& bindValues)
{
    if (!m_connected) {
        reportError(QStringLiteral("执行"), QStringLiteral("数据库未连接"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // PostgreSQL does not allow every DDL statement (for example CREATE INDEX)
    // in a server-side PREPARE. Statements without parameters do not benefit
    // from preparation, so execute them directly.
    if (bindValues.isEmpty()) {
        if (!q.exec(sql)) {
            reportError(QStringLiteral("执行"),
                        QStringLiteral("SQL 执行失败: %1").arg(q.lastError().text()));
            return false;
        }
        return true;
    }

    if (!prepareQuery(q, QStringLiteral("执行"), sql, bindValues)) {
        return false;
    }

    if (!execQuery(q, QStringLiteral("执行"), QStringLiteral("SQL 执行失败"))) {
        return false;
    }

    return true;
}

/**
 * @brief 在当前具名连接开启调用方管理的事务，并复用统一错误报告。
 */
bool DataStore::beginTransaction()
{
    if (!m_connected)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    return beginTransaction(db, QStringLiteral("事务")); 
}

/**
 * @brief 提交当前具名连接的事务，并复用统一错误报告。
 */
bool DataStore::commitTransaction()
{
    if (!m_connected)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    return commitTransaction(db, QStringLiteral("事务"));
}

/**
 * @brief 回滚当前具名连接的事务；回滚本身失败时同样报告。
 */
void DataStore::rollbackTransaction()
{
    if (!m_connected)
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    rollbackTransaction(db, QStringLiteral("事务"));
}

/**
 * @brief 返回当前线程中指向本实例具名连接的 Qt 句柄副本。
 * @warning 句柄不得保存到 DataStore::disconnect() 之后，也不得传给其他线程。
 */
QSqlDatabase DataStore::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

// ========== 内部方法 ==========

/**
 * @brief 用统一前缀写警告日志并发出 errorOccurred，供 AppController 归档系统事件。
 */
void DataStore::reportError(const QString& context, const QString& detail)
{
    QString msg = QStringLiteral("[DataStore] %1 错误: %2").arg(context, detail);
    qWarning() << msg;
    emit errorOccurred(msg);
}

/**
 * @brief 在局部作用域关闭并释放最后一个 QSqlDatabase 句柄后，再移除全局具名连接。
 * @details Qt 要求 removeDatabase 时不存在活跃句柄，否则查询会失效并产生警告；额外作用域用于保证析构顺序。
 */
void DataStore::removeConnection()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid() && db.isOpen()) {
            db.close();
        }
    }

    QSqlDatabase::removeDatabase(m_connectionName);
}

/** @brief 预处理 SQL 并按位置绑定全部值，集中报告驱动错误。 */
bool DataStore::prepareQuery(QSqlQuery& query,
                             const QString& context,
                             const QString& sql,
                             const QVariantList& bindValues)
{
    if (!query.prepare(sql)) {
        reportError(context,
                    QStringLiteral("SQL 预处理失败: %1").arg(query.lastError().text()));
        return false;
    }

    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues[i]);
    }

    return true;
}

/** @brief 执行已准备的查询，并把可选业务前缀与驱动错误组合为诊断文本。 */
bool DataStore::execQuery(QSqlQuery& query,
                          const QString& context,
                          const QString& detailPrefix)
{
    if (!query.exec()) {
        const QString detail = detailPrefix.isEmpty()
            ? query.lastError().text()
            : QStringLiteral("%1: %2").arg(detailPrefix, query.lastError().text());
        reportError(context, detail);
        return false;
    }

    return true;
}

/** @brief 在指定连接直接执行一条无参数 SQL，主要用于 PRAGMA 和 DDL 初始化。 */
bool DataStore::executeStatement(QSqlDatabase& db,
                                 const QString& context,
                                 const QString& sql,
                                 const QString& detailPrefix)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        const QString detail = detailPrefix.isEmpty()
            ? query.lastError().text()
            : QStringLiteral("%1: %2").arg(detailPrefix, query.lastError().text());
        reportError(context, detail);
        return false;
    }

    return true;
}

/** @brief 打开已配置连接，并统一拼装数据库驱动返回的失败详情。 */
bool DataStore::openDatabase(QSqlDatabase& db,
                             const QString& context,
                             const QString& detailPrefix)
{
    if (!db.open()) {
        const QString detail = detailPrefix.isEmpty()
            ? db.lastError().text()
            : QStringLiteral("%1: %2").arg(detailPrefix, db.lastError().text());
        reportError(context, detail);
        return false;
    }

    return true;
}

/**
 * @brief 在显式 QSqlDatabase 句柄上开启事务，供批量内部路径复用。
 */
bool DataStore::beginTransaction(QSqlDatabase& db, const QString& context)
{
    if (!db.transaction()) {
        reportError(context,
                    QStringLiteral("无法开始事务: %1").arg(db.lastError().text()));
        return false;
    }

    return true;
}

/**
 * @brief 提交显式连接上的事务；失败不会自动回滚，由调用路径决定后续策略。
 */
bool DataStore::commitTransaction(QSqlDatabase& db, const QString& context)
{
    if (!db.commit()) {
        reportError(context,
                    QStringLiteral("事务提交失败: %1").arg(db.lastError().text()));
        return false;
    }

    return true;
}

/**
 * @brief 回滚显式连接上的事务，并且不隐藏回滚操作本身的失败。
 */
void DataStore::rollbackTransaction(QSqlDatabase& db, const QString& context)
{
    if (!db.rollback()) {
        reportError(context,
                    QStringLiteral("事务回滚失败: %1").arg(db.lastError().text()));
    }
}
