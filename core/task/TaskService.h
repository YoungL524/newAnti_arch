/**
 * @file TaskService.h
 * @brief 定义任务及其多次采集会话的唯一业务 owner。
 * @details 上游未来由 ApplicationController 请求创建任务、开始或停止采集；本类通过 SqliteRuntimeRepository
 * 先提交 tasks/collection_sessions，再分配 TaskId/AcquisitionId 并登记归档身份。下游通信 Producer 使用返回
 * 句柄构造 TrackPacket；本类不发送设备命令、不解析 ACK、不发布航迹或创建线程。
 */

#pragma once

#include "core/model/EntityIds.h"
#include "core/task/TaskDefinition.h"

#include <QHash>
#include <QString>

namespace core::archive {
class SqliteArchiveRepository;
}

namespace core::config {
class DeviceConfigService;
}

namespace core::persistence {
class SqliteRuntimeRepository;
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
 * @warning 所有方法必须在 SQLite 单写线程调用；设备 Command/ACK 完成后再调用 startCollection()。
 */
class TaskService final
{
public:
    /**
     * @brief 绑定任务持久化、设备配置 owner 和归档身份登记入口。
     * @param runtimeRepository tasks/collection_sessions 的 SQLite 适配器。
     * @param deviceConfigService 解析冻结设备 revision 和 endpointKey。
     * @param archiveRepository 登记 TaskId/AcquisitionId 的归档映射。
     */
    TaskService(
        core::persistence::SqliteRuntimeRepository &runtimeRepository,
        const core::config::DeviceConfigService &deviceConfigService,
        core::archive::SqliteArchiveRepository &archiveRepository);

    /**
     * @brief 从 SQLite 未删除任务重建当前进程 TaskId。
     * @return 全部任务成功分配并绑定返回 true；只允许在空注册域调用一次。
     */
    bool loadFromDatabase();

    /**
     * @brief 先创建数据库任务，再分配并登记非零 TaskId。
     * @param task 用户元数据；成功后回填 taskUuid。
     * @param taskId 成功时写入当前进程任务句柄。
     * @return 名称有效、SQLite 提交和归档绑定均成功返回 true。
     */
    bool createTask(TaskDefinition &task, core::model::TaskId &taskId);

    /**
     * @brief 把数据库稳定任务 UUID 解析为当前进程 TaskId。
     * @param taskUuid 创建或加载任务的稳定 UUID。
     * @param taskId 成功时写入本次进程紧凑句柄。
     * @return UUID 属于未软删除且已加载任务时返回 true。
     */
    bool resolveTask(const QUuid &taskUuid, core::model::TaskId &taskId) const;

    /**
     * @brief 为已确认启动的设备建立新采集会话和 AcquisitionId。
     * @param taskId 已加载或创建的任务句柄。
     * @param deviceId DeviceConfigService 已激活且协议配置就绪的设备句柄。
     * @param endpointId 与 deviceId 同 revision 的主航迹接收端点。
     * @param acquisitionId 成功时写入不可复用的采集会话句柄。
     * @return 数据库会话创建与完整归档身份绑定成功返回 true。
     * @warning 本函数不替代 DeviceController 启动 ACK；调用者只能在设备确认进入接收状态后调用。
     */
    bool startCollection(
        core::model::TaskId taskId,
        core::model::DeviceId deviceId,
        core::model::EndpointId endpointId,
        core::model::AcquisitionId &acquisitionId);

    /**
     * @brief 停止一个当前活动采集会话，但保留其身份供 Topic 中晚到批次完成归档。
     * @param acquisitionId startCollection() 返回且尚未停止的句柄。
     * @param reason 已知停止原因，不接受 AbnormalProcessExit。
     * @param stoppedUtcMs 实际停止接收的 Unix epoch 毫秒。
     * @return 唯一活动会话成功收口返回 true；重复停止或未知句柄返回 false。
     */
    bool stopCollection(
        core::model::AcquisitionId acquisitionId,
        CollectionStopReason reason,
        qint64 stoppedUtcMs);

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
        bool active = false; ///< true 时 Producer 才能继续使用对应 AcquisitionId。
    };

    /** @brief 单调分配当前进程 TaskId，耗尽时返回零。 */
    core::model::TaskId allocateTaskId();

    /** @brief 单调分配当前进程 AcquisitionId，停止后也不复用。 */
    core::model::AcquisitionId allocateAcquisitionId();

    core::persistence::SqliteRuntimeRepository &m_runtimeRepository; ///< 借用的任务/会话持久化端口。
    const core::config::DeviceConfigService &m_deviceConfigService; ///< 借用的设备 revision 解析 owner。
    core::archive::SqliteArchiveRepository &m_archiveRepository; ///< 借用的归档身份登记入口。
    QHash<quint32, qint64> m_databaseTaskIds; ///< TaskId 原值到 tasks 主键。
    QHash<QUuid, core::model::TaskId> m_taskIdsByUuid; ///< 稳定任务 UUID 到当前进程句柄。
    QHash<quint64, ActiveCollection> m_collections; ///< AcquisitionId 原值到当前 run 会话。
    quint32 m_nextTaskId = 1; ///< 下一个候选 TaskId，零不分配。
    quint64 m_nextAcquisitionId = 1; ///< 下一个候选 AcquisitionId，零不分配。
    QString m_lastError; ///< 最近失败原因，不作为 SystemEvent 正文。
};

} // namespace core::task
