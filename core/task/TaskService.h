/**
 * @file TaskService.h
 * @brief 定义任务及其多次采集会话的唯一业务 owner。
 * @details 上游 ApplicationController 经 StorageRuntime 请求创建任务、开始或停止采集；本类通过 SqliteRuntimeRepository
 * 先提交 tasks/collection_sessions，再分配 TaskId/AcquisitionId 并登记归档身份。下游通信 Producer 使用返回
 * 句柄构造 TrackPacket；本类不发送设备命令、不解析 ACK、不发布航迹或创建线程。
 */

#pragma once

#include "core/model/EntityIds.h"
#include "core/datadomain/LatestState.h"
#include "core/model/RuntimeState.h"
#include "core/task/TaskDefinition.h"

#include <QHash>
#include <QString>

#include <array>

namespace core::archive {
class SqliteArchiveRepository;
}

namespace core::config {
class DeviceConfigService;
}

namespace core::persistence {
class SqliteRuntimeRepository;
}

namespace core::producer {
class SystemEventPublisher;
}

namespace core::model {
class StableIdRegistry;
}

namespace core::task {

/** @brief 表示具有准确停止时刻的采集会话结束原因。 */
enum class CollectionStopReason : quint8
{
    UserStop = 1, ///< 操作员或 UI 明确停止。
    Disconnected = 2, ///< 通信端点确认断开，后续重连会创建新会话。
    ProcessShutdown = 3, ///< 应用正常停机时主动停止。
    Error = 4 ///< 控制或通信错误导致本次会话终止。
};

/**
 * @brief 持久化任务并管理当前 run 内多次、不可复用的 AcquisitionId 生命周期。
 * @warning 所有方法必须在 SQLite 单写线程调用；由唯一采集编排者调用 startCollections()。
 */
class TaskService final
{
public:
    /**
     * @brief 绑定任务持久化、设备配置 owner 和归档身份登记入口。
     * @param runtimeRepository tasks/collection_sessions 的 SQLite 适配器。
     * @param deviceConfigService 解析冻结设备 revision 和 endpointKey。
     * @param archiveRepository 登记 TaskId/AcquisitionId 的归档映射。
     * @param stableIds 跨线程稳定身份注册表，只登记SQLite已经提交的实体。
     * @param taskState CoreDataDomain中唯一的任务完整状态，由本服务独占写入。
     * @param eventPublisher 可空的统一事件入口；正式运行时由存储组合根提供，单元级调用可省略。
     */
    TaskService(
        core::persistence::SqliteRuntimeRepository &runtimeRepository,
        core::config::DeviceConfigService &deviceConfigService,
        core::archive::SqliteArchiveRepository &archiveRepository,
        core::model::StableIdRegistry &stableIds,
        core::domain::LatestState<core::model::TaskState> &taskState,
        core::producer::SystemEventPublisher *eventPublisher = nullptr);

    /**
     * @brief 从 SQLite 未删除任务重建当前进程 TaskId。
     * @return 全部任务成功分配、绑定并发布 None 初始状态返回 true；只允许在空注册域调用一次。
     * @note 不恢复历史采集或自动激活旧任务；失败交由存储组合根销毁本服务及其已加载依赖。
     */
    bool loadFromDatabase();

    /**
     * @brief 返回当前可选任务模板的稳定UUID、名称和完整参数。
     * @param templateUuids 成功时按稳定UUID顺序写入模板UUID。
     * @param names 与UUID同下标的用户可见名称。
     * @param settings 与UUID同下标的完整控制参数。
     * @return 三组内部模板数据数量一致时返回true。
     */
    bool listControlTemplates(
        QVector<QUuid> &templateUuids,
        QVector<QString> &names,
        QVector<RadarControlSettings> &settings) const;

    /**
     * @brief 先创建数据库任务，再分配并登记非零 TaskId。
     * @param task 用户元数据；成功后回填 taskUuid。
     * @param taskId 成功时写入当前进程任务句柄。
     * @return 名称有效、SQLite 提交和归档绑定均成功返回 true。
     */
    bool createTask(TaskDefinition &task, core::model::TaskId &taskId);

    /**
     * @brief 返回当前未软删除任务的稳定快照，供低频HTTP序列化。
     * @param tasks 成功时按数据库创建顺序写入任务集合。
     * @return 内部任务与身份映射完整时返回true。
     */
    bool listTasks(QVector<TaskDefinition> &tasks) const;

    /**
     * @brief 选择一个 Draft 任务作为当前唯一活动任务，并发布 Ready 完整快照。
     * @param taskUuid UI持有的跨重启稳定任务UUID。
     * @param task 成功时返回被选择任务的台账快照。
     * @return 当前无活动任务且目标存在时返回true；本操作不启动设备或创建采集会话。
     */
    bool activateTask(const QUuid &taskUuid, TaskDefinition &task);

    /** @brief 仅供激活后设备快照准备失败时撤销当前 Ready 选择；任务仍保持未完成草稿。 */
    bool cancelActivation(const QUuid &taskUuid);

    /**
     * @brief 读取当前Ready或运行阶段任务对应的台账快照。
     * @param task 存在当前活动任务时写入其稳定台账字段。
     * @return TaskState已设置且TaskId仍属于本TaskService时返回true。
     */
    bool activeTask(TaskDefinition &task) const;

    /**
     * @brief 判断指定任务是否为当前活动任务。
     * @param taskUuid 待检查的稳定任务UUID。
     * @return LatestState当前引用该任务且阶段不是None时返回true。
     */
    bool isTaskActive(const QUuid &taskUuid) const;

    /**
     * @brief 完成当前Ready任务并清除唯一活动任务状态。
     * @param taskUuid 必须与LatestState当前TaskId对应的稳定UUID一致。
     * @param task 成功时返回带完成时间的台账快照。
     * @return 没有活动采集且SQLite收口与状态发布都成功时返回true。
     * @warning 后续Running阶段必须先由ApplicationController完成设备停止和会话收口，再调用本函数。
     */
    bool completeTask(const QUuid &taskUuid, TaskDefinition &task);

    /**
     * @brief 将已完成且非活动任务标记为已归档，保留原任务和全部采集事实。
     * @param taskUuid UI 持有的稳定任务 UUID。
     * @param task 成功时返回带归档时间的任务快照。
     * @return SQLite 条件更新成功时返回 true；其他情况返回 false 并保留旧状态。
     * @warning 只能在 SQLite 单写线程调用。
     */
    bool archiveTask(const QUuid &taskUuid, TaskDefinition &task);

    /**
     * @brief 保存当前活动任务的独立控制参数快照，不修改模板、TaskState 或采集会话。
     * @param taskUuid 稳定任务 UUID；必须已加载且是当前唯一活动任务。
     * @param settings 已通过 validateRadarControlSettings 的完整参数。
     * @param taskId 成功时写入该任务在当前进程的句柄，供上层同步设备运行副本。
     * @param task 成功时返回更新后的完整任务台账快照。
     * @return 任务存在、是当前活动任务、阶段为 Ready 或 Running、值域合法且 SQLite 提交成功返回 true。
     * @warning 必须在 SQLite 单写线程调用；失败时不修改内存快照、TaskState、模板或采集会话。
     */
    bool updateControlSettings(const QUuid &taskUuid,
                               const RadarControlSettings &settings,
                               core::model::TaskId &taskId,
                               TaskDefinition &task);

    /**
     * @brief 软删除一项尚无活动采集的任务。
     * @param taskUuid UI持有的跨重启稳定任务UUID。
     * @param deletedBy 执行删除的用户或系统标识。
     * @return 任务存在、不是当前活动任务、没有活动采集且SQLite更新唯一一行时返回true。
     */
    bool softDeleteTask(const QUuid &taskUuid, const QString &deletedBy);

    /**
     * @brief 把数据库稳定任务 UUID 解析为当前进程 TaskId。
     * @param taskUuid 创建或加载任务的稳定 UUID。
     * @param taskId 成功时写入本次进程紧凑句柄。
     * @return UUID 属于未软删除且已加载任务时返回 true。
     */
    bool resolveTask(const QUuid &taskUuid, core::model::TaskId &taskId) const;

    /**
     * @brief 按任务阵面位图建立一次本地开始操作所需的全部独立会话。
     * @param taskId 当前Ready任务句柄。
     * @param deviceId 任务绑定的完整雷达句柄。
     * @param endpointId 与设备revision一致的主航迹端点。
     * @param faceMask bit0～bit3表示本次启用阵面，必须与任务冻结绑定一致。
     * @param acquisitionIds 成功时按阵面下标写入独立句柄，未启用阵面保持零。
     * @return 全部会话同一事务创建且 TaskState 进入 Running 返回 true；身份绑定失败显式收口，收口失败进入 Faulted。
     */
    bool startCollections(
        core::model::TaskId taskId,
        core::model::DeviceId deviceId,
        core::model::EndpointId endpointId,
        quint8 faceMask,
        std::array<core::model::AcquisitionId, 4> &acquisitionIds);

    /**
     * @brief 原子停止当前活动采集会话集合，但保留身份供 Topic 晚到批次完成归档。
     * @param acquisitionIds 同次操作的非空、无重复活动句柄集合。
     * @param reason 已知停止原因，不接受 AbnormalProcessExit。
     * @param stoppedUtcMs 实际停止接收的 Unix epoch 毫秒。
     * @return 整组事务提交后统一更新内存；未知、重复或失败保持原活动位。
     */
    bool stopCollections(
        const QVector<core::model::AcquisitionId> &acquisitionIds,
        CollectionStopReason reason,
        qint64 stoppedUtcMs);

    /** @brief Producer 停止后，以 ProcessShutdown 原子收口全部活动会话；空集合成功，失败保留活动位和错误。 */
    bool stopAllCollections(qint64 stoppedUtcMs);

    /**
     * @brief 判断 AcquisitionId 是否仍允许 Producer 接收并发布新航迹。
     * @return 已创建且尚未停止时返回 true。
     */
    bool isCollectionActive(core::model::AcquisitionId acquisitionId) const;

    /** @brief 返回最近任务、会话或身份绑定失败原因。 */
    QString lastError() const;

private:
    /** @brief 保存当前 run 中一条会话的数据库主键和活动状态。 */
    struct ActiveCollection
    {
        qint64 collectionSessionId = 0; ///< collection_sessions 本地主键。
        core::model::TaskId taskId; ///< 创建该会话的任务，用于阻止活动任务被删除。
        quint8 faceIndex = 0; ///< 本次会话唯一对应的物理阵面索引。
        bool active = false; ///< true 时 Producer 才能继续使用对应 AcquisitionId。
    };

    /** @brief 单调分配当前进程 TaskId，耗尽时返回零。 */
    core::model::TaskId allocateTaskId();

    /** @brief 单调分配当前进程 AcquisitionId，停止后也不复用。 */
    core::model::AcquisitionId allocateAcquisitionId();

    /**
     * @brief 形成合法聚合元数据并发布一份TaskState完整快照。
     * @param taskId None阶段必须为零，其他阶段必须属于本服务。
     * @param phase 待发布的任务阶段。
     * @return 门禁和LatestState revision分配均成功时返回true。
     */
    bool publishTaskState(core::model::TaskId taskId, core::model::TaskPhase phase);

    /**
     * @brief 在任务事实已提交后追加一条定长生命周期事件，供 Web 和归档各自读取。
     * @param taskId 已登记的非零任务句柄。
     * @param stage 稳定阶段码：1创建、2激活、3完成、4归档。
     * @note 事件发布失败不回滚已提交的任务操作，避免向用户伪报本地事务失败。
     */
    void publishTaskEvent(core::model::TaskId taskId, qint64 stage);

    core::persistence::SqliteRuntimeRepository &m_runtimeRepository; ///< 借用的任务/会话持久化端口。
    core::config::DeviceConfigService &m_deviceConfigService; ///< 借用的设备 revision 与任务绑定校验owner。
    core::archive::SqliteArchiveRepository &m_archiveRepository; ///< 借用的归档身份登记入口。
    core::model::StableIdRegistry &m_stableIds; ///< 借用的稳定 UUID 注册表；只登记已提交实体。
    core::domain::LatestState<core::model::TaskState> &m_taskState; ///< 任务运行阶段唯一共享快照；仅本服务写入。
    core::producer::SystemEventPublisher *m_eventPublisher = nullptr; ///< 借用的可空统一事件入口；正式运行由存储组合根持有。
    QHash<quint32, qint64> m_databaseTaskIds; ///< TaskId 原值到 tasks 主键。
    QHash<QUuid, core::model::TaskId> m_taskIdsByUuid; ///< 稳定任务 UUID 到当前进程句柄。
    QHash<quint32, TaskDefinition> m_tasks; ///< 当前未删除任务元数据；SQLite仍是跨重启唯一真源。
    QHash<QUuid, QString> m_templateNames; ///< 模板UUID到用户可见名称；SQLite是跨重启真源。
    QHash<QUuid, RadarControlSettings> m_templateSettings; ///< 模板UUID到已校验控制参数。
    QHash<quint64, ActiveCollection> m_collections; ///< AcquisitionId 原值到当前 run 会话。
    quint32 m_nextTaskId = 1; ///< 下一个候选 TaskId，零不分配。
    quint64 m_nextAcquisitionId = 1; ///< 下一个候选 AcquisitionId，零不分配。
    quint64 m_nextStateTraceId = 1; ///< 任务状态快照的进程内诊断关联号；零不使用。
    QString m_lastError; ///< 最近失败原因，不作为 SystemEvent 正文。
};

} // namespace core::task
