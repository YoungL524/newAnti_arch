#include "DataStore.h"
#include <QSqlError>
#include <QSqlRecord>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

int DataStore::s_connectionCounter = 0;

DataStore::DataStore(QObject* parent)
    : QObject(parent)
{
    // 生成唯一连接名，避免多个 DataStore 实例冲突
    m_connectionName = QStringLiteral("DataStore_%1").arg(++s_connectionCounter);
}

DataStore::~DataStore()
{
    disconnect();
}

// ========== 连接管理 ==========

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

    return connectToDatabase(driver, host, port, dbName, user, password);
}

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

bool DataStore::isConnected() const
{
    return m_connected;
}

// ========== 表管理 ==========

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

bool DataStore::tableExists(const QString& tableName) const
{
    if (!m_connected) {
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    return db.tables().contains(tableName);
}

// ========== 数据操作 ==========

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

bool DataStore::execute(const QString& sql, const QVariantList& bindValues)
{
    if (!m_connected) {
        reportError(QStringLiteral("执行"), QStringLiteral("数据库未连接"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!prepareQuery(q, QStringLiteral("执行"), sql, bindValues)) {
        return false;
    }

    if (!execQuery(q, QStringLiteral("执行"), QStringLiteral("SQL 执行失败"))) {
        return false;
    }

    return true;
}

QSqlDatabase DataStore::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

// ========== 内部方法 ==========

void DataStore::reportError(const QString& context, const QString& detail)
{
    QString msg = QStringLiteral("[DataStore] %1 错误: %2").arg(context, detail);
    qWarning() << msg;
    emit errorOccurred(msg);
}

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

bool DataStore::beginTransaction(QSqlDatabase& db, const QString& context)
{
    if (!db.transaction()) {
        reportError(context,
                    QStringLiteral("无法开始事务: %1").arg(db.lastError().text()));
        return false;
    }

    return true;
}

bool DataStore::commitTransaction(QSqlDatabase& db, const QString& context)
{
    if (!db.commit()) {
        reportError(context,
                    QStringLiteral("事务提交失败: %1").arg(db.lastError().text()));
        return false;
    }

    return true;
}

void DataStore::rollbackTransaction(QSqlDatabase& db, const QString& context)
{
    if (!db.rollback()) {
        reportError(context,
                    QStringLiteral("事务回滚失败: %1").arg(db.lastError().text()));
    }
}
