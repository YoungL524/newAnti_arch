/**
 * @file SqliteArchiveRepository.h
 * @brief 定义 ArchiveReader 到单写者 SQLite 数据库的强类型事务实现。
 * @details 本类位于 ArchiveReader 与 QSQLITE 之间：上游传入固定 ScratchBuffer 中的 TrackPacket/SystemEvent
 * 批次，下游在当前存储线程完成七表初始化、运行实例登记、紧凑句柄解析和事务提交。它不订阅 Topic、不维护
 * cursor、不创建内部队列或线程，也不负责 React 配置 API；新人应先阅读 open()、bindAcquisition() 与两个
 * append 方法的线程及提交合同。
 */

#pragma once

#include "core/archive/IArchiveRepository.h"
#include "core/model/EntityIds.h"

#include <QHash>
#include <QSqlDatabase>
#include <QString>

#include <memory>

class QSqlQuery;
class QThread;
class QLockFile;

namespace core::persistence {
class SqliteRuntimeRepository;
}

namespace core::archive {

/**
 * @brief 使用一个线程私有 QSQLITE 写连接实现航迹和系统事件整批提交。
 * @details open() 所在线程成为永久 owner；配置/任务服务建立数据库实体后，必须把本进程 CompactId 与对应
 * 数据库主键绑定，再允许 ArchiveReader 写入。所有绑定只保存身份映射，不复制 Topic 历史。
 */
class SqliteArchiveRepository final : public IArchiveRepository
{
public:
    /**
     * @brief 保存数据库位置与应用版本，但不打开文件或创建连接。
     * @param databasePath SQLite 文件绝对或相对路径；父目录必须已经存在。
     * @param applicationVersion 写入 application_runs 的程序版本，不能为空。
     */
    SqliteArchiveRepository(QString databasePath, QString applicationVersion);

    /**
     * @brief 关闭连接但不伪造正常退出；未调用 closeCleanly() 的 run 会在下次启动判定为异常。
     * @warning 必须在 open() 所在线程析构，且 ArchiveReader 已停止调用本对象。
     */
    ~SqliteArchiveRepository() override;

    /** @brief 禁止复制数据库连接、prepared query 和运行实例所有权。 */
    SqliteArchiveRepository(const SqliteArchiveRepository&) = delete;

    /** @brief 禁止复制赋值，避免两个 Repository 误共享同一个 Qt SQL 连接名。 */
    SqliteArchiveRepository& operator=(const SqliteArchiveRepository&) = delete;

    /**
     * @brief 取得规范化路径独占锁后打开 QSQLITE、初始化七表 Schema、恢复旧会话并登记当前 run。
     * @return 所有步骤成功返回 true；失败时关闭连接并由 lastError() 提供原因。
     * @warning 只能调用一次，且必须早于通信 Producer 与 ArchiveReader 启动。
     */
    bool open();

    /**
     * @brief 把当前 run 标记为正常退出后关闭数据库连接。
     * @param endedUtcMs 已完成 Producer 停止和归档提交后的 Unix epoch 毫秒，必须为正。
     * @return run 更新并关闭成功返回 true；更新失败仍关闭连接并返回 false。
     */
    bool closeCleanly(qint64 endedUtcMs);

    /** @brief 返回数据库写连接与当前 run 是否已经成功建立。 */
    bool isOpen() const noexcept;

    /** @brief 返回当前 application_runs 本地主键；未打开时为零。 */
    qint64 currentRunId() const noexcept;

    /** @brief 返回最近一次失败的可诊断文本；成功操作会清空旧错误。 */
    QString lastError() const;

    /**
     * @brief 绑定进程内 EndpointId 与设备 revision 内的稳定 endpointKey。
     * @param endpointId 已由 EndpointRegistry 分配的非零句柄。
     * @param endpointKey 设备配置中非空且稳定的端点键。
     * @return 参数有效且没有与既有不同绑定冲突时返回 true。
     */
    bool bindEndpoint(core::model::EndpointId endpointId, const QString &endpointKey);

    /**
     * @brief 绑定进程内 DeviceId 与 devices.device_revision_id。
     * @param deviceId 已由 DeviceRegistry 分配的非零句柄。
     * @param deviceRevisionId 当前运行明确采用的正数据库主键。
     * @return 参数有效且没有与既有不同绑定冲突时返回 true。
     */
    bool bindDevice(core::model::DeviceId deviceId, qint64 deviceRevisionId);

    /**
     * @brief 绑定进程内 TaskId 与 tasks.task_id。
     * @param taskId 已由任务 owner 分配的非零句柄。
     * @param databaseTaskId 对应的正数据库主键。
     * @return 参数有效且没有与既有不同绑定冲突时返回 true。
     */
    bool bindTask(core::model::TaskId taskId, qint64 databaseTaskId);

    /**
     * @brief 冻结一次 AcquisitionId 对应的会话主键及其设备、任务和接收端点上下文。
     * @param acquisitionId 当前活动采集会话的非零句柄。
     * @param collectionSessionId 已创建的 collection_sessions 正主键。
     * @param deviceId 该会话冻结的设备句柄。
     * @param taskId 该会话所属任务句柄，第一版必须非零。
     * @param endpointId 该会话实际接收航迹的端点句柄。
     * @return 全部参数有效且没有与既有不同绑定冲突时返回 true。
     */
    bool bindAcquisition(
        core::model::AcquisitionId acquisitionId,
        qint64 collectionSessionId,
        core::model::DeviceId deviceId,
        core::model::TaskId taskId,
        core::model::EndpointId endpointId);

    /**
     * @brief 在一个 BEGIN IMMEDIATE 事务中写入连续航迹批次。
     * @return 全部记录通过身份/字段校验并提交成功返回 true；任一失败则整批回滚。
     */
    bool appendTrackBatch(
        const core::domain::TopicRecord<core::model::TrackPacket> *records,
        qsizetype count) override;

    /**
     * @brief 在一个 BEGIN IMMEDIATE 事务中写入连续系统事件批次。
     * @return 全部记录通过身份/flags/事件码校验并提交成功返回 true；任一失败则整批回滚。
     */
    bool appendEventBatch(
        const core::domain::TopicRecord<core::model::SystemEvent> *records,
        qsizetype count) override;

private:
    friend class core::persistence::SqliteRuntimeRepository;

    /** @brief 保存一个 AcquisitionId 在启动时冻结的最小归档身份上下文。 */
    struct CollectionBinding
    {
        qint64 collectionSessionId = 0; ///< collection_sessions 本地主键。
        core::model::DeviceId deviceId; ///< 用于拒绝会话内误绑设备的期望句柄。
        core::model::TaskId taskId; ///< 用于拒绝会话内误绑任务的期望句柄。
        core::model::EndpointId endpointId; ///< 用于拒绝会话内误绑接收端点的期望句柄。
    };

    /** @brief 检查调用线程、连接状态并设置统一失败文本。 */
    bool requireOwnerThread(const char *operation);

    /** @brief 设置 PRAGMA、建立或迁移并核对当前 user_version 的七表 Schema。 */
    bool initializeDatabase();

    /** @brief 核对当前七表及必要列，拒绝缺表、旧姿态列或缺少冻结字段的数据库。 */
    bool verifyRequiredTables();

    /** @brief 标记历史未闭合 run，并插入当前 application_runs 行。 */
    bool beginApplicationRun();

    /** @brief 为两个热写入口准备可跨批次复用的 INSERT。 */
    bool prepareInsertQueries();

    /** @brief 执行一条不返回业务数据的 SQL，并保留数据库错误。 */
    bool executeSql(const QString &sql);

    /** @brief 回滚当前事务并保留首个业务错误或数据库错误。 */
    bool rollbackWithError(const QString &error);

    /** @brief 销毁 prepared query、关闭连接并安全移除 Qt 连接名。 */
    void closeConnection() noexcept;

    /** @brief 把完整 quint64 位模式编码为固定 8 字节大端 BLOB。 */
    static QByteArray encodeUnsigned64(quint64 value);

    QString m_databasePath; ///< SQLite 文件位置，由组合根配置。
    QString m_applicationVersion; ///< 当前二进制版本，写入 application_runs。
    QString m_connectionName; ///< 本实例唯一 Qt SQL 连接名，禁止跨对象复用。
    QSqlDatabase m_database; ///< 仅属于 open() 线程的单一写连接。
    std::unique_ptr<QLockFile> m_databaseLock; ///< 按规范化数据库路径独占的进程锁；先于恢复获取，连接关闭后释放。
    std::unique_ptr<QSqlQuery> m_trackInsert; ///< 复用的 track_observations INSERT。
    std::unique_ptr<QSqlQuery> m_eventInsert; ///< 复用的 system_events INSERT。
    QThread *m_ownerThread = nullptr; ///< 首次 open() 的线程亲和性，不拥有线程。
    qint64 m_runId = 0; ///< 当前 application_runs 主键，关闭后归零。
    qint64 m_runStartedUtcMs = 0; ///< 当前 run 开始时间，用于拒绝倒退的正常结束时间。
    QString m_lastError; ///< 最近失败原因，不进入实时 Topic。

    QHash<quint32, QString> m_endpointKeys; ///< EndpointId 原值到稳定端点键的有界注册映射。
    QHash<quint32, qint64> m_deviceRevisionIds; ///< DeviceId 原值到设备 revision 主键的映射。
    QHash<quint32, qint64> m_taskIds; ///< TaskId 原值到任务主键的映射。
    QHash<quint64, CollectionBinding> m_collectionBindings; ///< AcquisitionId 到冻结会话上下文的映射。
};

} // namespace core::archive
