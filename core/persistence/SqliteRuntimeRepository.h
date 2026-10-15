/**
 * @file SqliteRuntimeRepository.h
 * @brief 定义设备配置、任务、采集会话及有界事件/航迹历史读取共用的 SQLite 低频适配器。
 * @details 上游 DeviceConfigService/TaskService 调用本类创建不可变设备 revision、任务和采集会话，下游复用
 * SqliteArchiveRepository 已打开的唯一 QSQLITE 写连接。本类不分配 CompactId、不发布 Topic、不执行设备命令，
 * 也不保存第二份配置真源；从 saveDeviceRevision()/createCollectionSessions() 阅读事务边界，从 read*Page 阅读分页边界。
 */

#pragma once
#include <QJsonArray>

#include "core/config/DeviceConfiguration.h"
#include "core/task/TaskDefinition.h"
#include "core/task/RadarControlSettings.h"

#include <QString>
#include <QVector>
#include <array>

namespace core::archive {
class SqliteArchiveRepository;
}

namespace core::persistence {

/**
 * @brief 在存储线程内复用单一 SQLite 写连接完成配置和任务实体事务。
 * @warning 对象与 SqliteArchiveRepository 必须在同一线程构造、调用和销毁。
 */
class SqliteRuntimeRepository final
{
public:
    /**
     * @brief 绑定已经 open() 的 SQLite 连接 owner。
     * @param databaseOwner 同时承担归档写入和 application run 的连接所有者，生命周期必须覆盖本对象。
     */
    explicit SqliteRuntimeRepository(core::archive::SqliteArchiveRepository &databaseOwner);

    /** @brief 禁止复制共享写连接的低频持久化适配器。 */
    SqliteRuntimeRepository(const SqliteRuntimeRepository&) = delete;

    /** @brief 禁止复制赋值，避免事务 owner 不明确。 */
    SqliteRuntimeRepository& operator=(const SqliteRuntimeRepository&) = delete;

    /**
     * @brief 以乐观 revision 检查追加一条不可变设备配置。
     * @param configuration 完整配置；新设备 UUID/revision 可为空/零，成功后回填 UUID 与新 revision。
     * @param deviceRevisionId 成功时写入 devices 本地主键。
     * @return 插入并提交成功返回 true；过期 revision、字段或数据库错误返回 false。
     */
    bool saveDeviceRevision(
        core::config::DeviceConfiguration &configuration,
        qint64 &deviceRevisionId);

    /**
     * @brief 读取每个未删除设备 UUID 的最新 revision。
     * @param configurations 成功时替换为完整设备配置集合。
     * @param deviceRevisionIds 与 configurations 同下标对应的数据库主键集合。
     * @return 查询成功返回 true；失败时不保留部分结果。
     */
    bool loadCurrentDevices(
        QVector<core::config::DeviceConfiguration> &configurations,
        QVector<qint64> &deviceRevisionIds);

    /**
     * @brief 读取全部未删除控制模板，供TaskService建立低频选择快照。
     * @param templateUuids 成功时按创建顺序写入稳定UUID。
     * @param names 与UUID同下标的模板名称。
     * @param settings 与UUID同下标的已解析完整控制参数。
     * @return 查询、JSON解析和值域校验全部成功返回true。
     */
    bool loadControlTemplates(
        QVector<QUuid> &templateUuids,
        QVector<QString> &names,
        QVector<core::task::RadarControlSettings> &settings);

    /**
     * @brief 以追加tombstone revision的方式软删除一台完整雷达。
     * @param deviceUuid 待删除设备的稳定UUID。
     * @param expectedRevision UI最后读取的revision，用于拒绝覆盖并发修改。
     * @param changedBy 执行删除的操作员或服务身份。
     * @param changeReason 删除原因，允许为空。
     * @return 当前revision匹配且tombstone事务提交成功返回true。
     */
    bool softDeleteDevice(const QUuid &deviceUuid, quint32 expectedRevision,
                          const QString &changedBy, const QString &changeReason);

    /**
     * @brief 插入一项未删除任务并回填稳定 UUID。
     * @param task 用户任务元数据；UUID 为空时由 Repository 生成。
     * @param databaseTaskId 成功时写入 tasks 本地主键。
     * @return 单行事务提交成功返回 true。
     */
    bool createTask(core::task::TaskDefinition &task, qint64 &databaseTaskId);

    /**
     * @brief 读取全部未软删除任务供 TaskService 重建进程内 TaskId。
     * @param tasks 成功时替换为任务集合。
     * @param databaseTaskIds 与 tasks 同下标对应的数据库主键集合。
     * @return 查询成功返回 true；失败时不保留部分结果。
     */
    bool loadActiveTasks(
        QVector<core::task::TaskDefinition> &tasks,
        QVector<qint64> &databaseTaskIds);

    /**
     * @brief 将未删除且无活动会话的任务标记为软删除。
     * @param databaseTaskId TaskService已绑定的tasks本地主键。
     * @param deletedBy 执行删除的用户或系统标识。
     * @return 唯一任务行成功更新时返回true。
     */
    bool softDeleteTask(qint64 databaseTaskId, const QString &deletedBy);

    /**
     * @brief 为没有活动采集的任务写入唯一完成时间。
     * @param databaseTaskId TaskService内部持有的tasks主键。
     * @param completedUtcMs 用户确认完成任务的Unix epoch毫秒。
     * @return 任务存在、尚未完成且没有未停止会话时返回true。
     */
    bool completeTask(qint64 databaseTaskId, qint64 completedUtcMs);

    /**
     * @brief 只给已完成且未归档任务写入归档时间，不修改航迹、事件和采集会话。
     * @param databaseTaskId SQLite tasks 主键，必须大于零。
     * @param archivedUtcMs Unix epoch UTC 毫秒，必须大于零。
     * @return 条件更新恰好一行时返回 true；失败原因由 lastError() 提供。
     * @warning 只能在 SQLite 单写线程调用。
     */
    bool archiveTask(qint64 databaseTaskId, qint64 archivedUtcMs);

    /**
     * @brief 覆盖一项未完成且未归档任务的独立控制参数快照。
     * @param databaseTaskId TaskService 持有的 tasks 本地主键，必须大于零。
     * @param settings 已通过值域校验的完整参数；序列化后写入 control_settings_json。
     * @param updatedUtcMs 本次修改的 Unix epoch UTC 毫秒，必须大于零。
     * @return 唯一未删除、未闭合且未归档的任务行成功更新返回 true；其他情况返回 false。
     * @note 单条 UPDATE 由 SQLite 自身原子提交；不修改模板、采集会话或事件表，也不建立第二份参数真源。
     * @warning 只能在 SQLite 单写线程调用。
     */
    bool updateTaskControlSettings(qint64 databaseTaskId,
                                   const core::task::RadarControlSettings &settings,
                                   qint64 updatedUtcMs);

    /**
     * @brief 为当前 application run 创建一次 Running 采集会话。
     * @param databaseTaskId tasks 正主键。
     * @param deviceRevisionId devices revision 正主键。
     * @param endpointKey 本次实际接收航迹的稳定端点键。
     * @param faceMask 本次启用阵面低四位，非零。
     * @param sessionUuids 提交后按阵面回填 UUID，未启用项为空。
     * @param collectionSessionIds 提交后按阵面回填主键，未启用项为零。
     * @return 所有阵面在同一事务提交才返回 true；失败不交付部分会话。
     */
    bool createCollectionSessions(
        qint64 databaseTaskId,
        qint64 deviceRevisionId,
        const QString &endpointKey,
        quint8 faceMask,
        std::array<QUuid, 4> &sessionUuids,
        std::array<qint64, 4> &collectionSessionIds);

    /**
     * @brief 把 Running 会话更新为一个已知停止状态。
     * @param collectionSessionIds 当前 run 内非空、无重复的活动会话主键集合。
     * @param stopReason 合同登记值 1=UserStop、2=Disconnected、3=ProcessShutdown、4=Error。
     * @param stoppedUtcMs 已知停止接收的 Unix epoch 毫秒。
     * @return 全部活动行同一事务提交返回 true；任一失败整组回滚，不返回部分成功。
     */
    bool stopCollectionSessions(
        const QVector<qint64> &collectionSessionIds,
        quint8 stopReason,
        qint64 stoppedUtcMs);

    /**
     * @brief 在唯一存储线程读取指定稳定任务的已提交事件，按 UTC/事件 ID 倒序取有界一页。
     * @param taskUuid 可空稳定任务身份；空时包含无任务归属事件，不使用进程句柄查询历史。
     * @param deviceUuid 可空稳定设备身份；跨安装 revision 查询，不要求设备当前仍在台账。
     * @param domain 零表示全部，否则为事件域 1～4。
     * @param severity -1 表示全部，否则精确匹配 0～4。
     * @param startUtcMs 正 UTC 毫秒闭区间起点。
     * @param endUtcMs 不小于起点的安全整数 UTC 毫秒闭区间终点。
     * @param beforeUtcMs 首次为零；续页严格早于该 UTC 毫秒与 beforeEventId 的组合。
     * @param beforeEventId 首次为零；续页同毫秒下严格小于该数据库事件主键。
     * @param limit 每页 1～200，查询最多取 limit+1 行判断后续页。
     * @param events 成功替换为原始事件边界 JSON，ID/参数使用十进制字符串；不格式化显示文本。
     * @param hasMore 成功时表示同一读取时刻还有更早记录；不承诺跨请求数据库快照。
     * @return 参数、线程或 SQL 失败返回 false，不返回部分结果，错误从 lastError 读取。
     */
    bool readEventPage(const QUuid &taskUuid, const QUuid &deviceUuid, int domain, int severity,
                           qint64 startUtcMs, qint64 endUtcMs, qint64 beforeUtcMs, qint64 beforeEventId,
                           int limit, QJsonArray &events, bool &hasMore);

    /**
     * @brief 唯一存储线程读取已提交运行记录，按启动时刻/主键倒序，不推断进程存活。
     * @param startUtcMs 正安全整数 UTC 毫秒闭区间起点。
     * @param endUtcMs 不早于起点的安全整数 UTC 毫秒终点。
     * @param shutdownState -1 表示全部，0/1/2 分别为未标记退出/正常退出/检测到异常。
     * @param beforeUtcMs 首次为零，续页取最后一条启动时刻。
     * @param beforeRunId 首次为零，续页取最后一条数据库主键，不是运行 UUID。
     * @param limit 每页 1～200，额外读取一条判断后续页。
     * @param runs 成功替换的短期 JSON；可空字段省略，ID 保留十进制字符串精度。
     * @param hasMore 是否还有更早记录，跨页不持有快照事务。
     * @return 参数、线程或 SQL 失败返回 false，不交付部分页，lastError 提供原因。
     */
    bool readRunPage(qint64 startUtcMs, qint64 endUtcMs, int shutdownState,
                     qint64 beforeUtcMs, qint64 beforeRunId, int limit, QJsonArray &runs, bool &hasMore);

    /**
     * @brief 在唯一存储线程升序读取一页已提交航迹及采集时不可变安装配置，不使用当前设备 revision。
     * @param taskUuid 稳定任务 UUID。
     * @param startUtcMs 正 UTC 毫秒闭区间起点。
     * @param endUtcMs 不小于起点的 UTC 毫秒闭区间终点。
     * @param afterUtcMs 首次为零，续页为最后观测的接收 UTC 毫秒。
     * @param afterId 首次为零，续页为最后 observation_id，防止同毫秒重复。
     * @param limit 1～1000，实际至多读取 limit+1 行判断后续。
     * @param tracks 成功时替换为短期边界 JSON；角度弧度，距离/高度米，pose 姿态度。
     * @param hasMore 成功时指示还有下一页，跨页不持有 SQL 事务。
     * @return SQL 或归档安装配置缺失/非法返回 false，不交付部分结果，错误由 lastError 提供。
     */
    bool readTaskTrackPage(const QUuid &taskUuid, qint64 startUtcMs, qint64 endUtcMs,
                           qint64 afterUtcMs, qint64 afterId, int limit, QJsonArray &tracks, bool &hasMore);

    /** @brief 返回最近一次低频持久化失败原因；成功操作会清空旧错误。 */
    QString lastError() const;

private:
    /** @brief 确保底层连接已打开且当前调用位于唯一写线程。 */
    bool requireDatabase(const char *operation);

    /** @brief 执行事务回滚并保存业务首因。 */
    bool rollbackWithError(const QString &error);

    core::archive::SqliteArchiveRepository &m_databaseOwner; ///< 借用的单写连接 owner。
    QString m_lastError; ///< 最近配置/任务 SQL 失败原因。
};

} // namespace core::persistence
