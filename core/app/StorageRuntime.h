/**
 * @file StorageRuntime.h
 * @brief 定义 SQLite 配置真源、任务服务与稳定身份登记的单写线程组合根。
 * @details ApplicationRuntime 将本对象移动到唯一存储 QThread 后调用 initialize()；本对象在该线程构造并
 * 拥有 SQLite Repository、配置/任务 Owner、共享存储 ExecutionLane 和 ArchiveReader。下游通过
 * StableIdRegistry 读取已提交身份；本类不创建采集会话、不绑定 TrackIngressProducer，也不执行命令。
 */
#pragma once

#include "core/datadomain/EventTopic.h"
#include "core/datadomain/HistoryTopic.h"
#include "core/datadomain/LatestState.h"
#include "core/model/EntityIds.h"
#include "core/model/RuntimeState.h"
#include "core/model/SystemEvent.h"
#include "core/model/TrackPacket.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QUuid>

#include <array>
#include <memory>

namespace core::archive {
class ArchiveReader;
class SqliteArchiveRepository;
}

namespace core::execution {
class ExecutionLane;
}

namespace core::config {
class DeviceConfigService;
}

namespace core::model {
class StableIdRegistry;
}

namespace core::persistence {
class SqliteRuntimeRepository;
}

namespace core::producer {
class SystemEventPublisher;
}

namespace core::task {
class TaskService;
struct TaskDefinition;
}

namespace core::app {

struct ActiveTaskRuntimeContext;

/**
 * @brief 在一个存储线程内建立和关闭全部 SQLite 单写依赖。
 * @warning initialize()、shutdown() 和析构必须在对象所属存储线程执行；数据库目录由上层提前创建。
 */
class StorageRuntime final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 保存启动参数和稳定身份注册表借用关系，不在构造线程打开 SQLite。
     * @param databasePath SQLite 数据库文件路径，不能为空。
     * @param applicationVersion 写入 application_runs 的应用版本，不能为空。
     * @param stableIds 生命周期覆盖本对象的跨线程稳定身份注册表。
     * @param trackTopic 数据域唯一航迹历史，生命周期覆盖本对象。
     * @param eventTopic 数据域唯一系统事件历史，生命周期覆盖本对象。
     * @param taskState 数据域唯一任务状态，由本对象拥有的TaskService写入Ready及后续阶段。
     */
    StorageRuntime(QString databasePath, QString applicationVersion,
                   core::model::StableIdRegistry &stableIds,
                   const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
                   core::domain::EventTopic<core::model::SystemEvent> &eventTopic,
                   core::domain::LatestState<core::model::TaskState> &taskState);

    /** @brief 释放前要求 shutdown() 已完成，防止跨线程销毁 Qt SQL 连接。 */
    ~StorageRuntime() override;

    /** @brief 禁止复制数据库连接、服务 Owner 和线程归属。 */
    StorageRuntime(const StorageRuntime&) = delete;

    /** @brief 禁止复制赋值，避免产生第二个 SQLite 写 Owner。 */
    StorageRuntime& operator=(const StorageRuntime&) = delete;

    /**
     * @brief 打开数据库并按设备、任务顺序重建本次进程紧凑身份。
     * @return 全部依赖创建、Schema 校验和身份加载成功返回 true；失败时回收已创建对象。
     */
    Q_INVOKABLE bool initialize();

    /**
     * @brief 在停止新输入后归档当前 Topic 水位，再在所属线程释放全部 SQLite 依赖。
     * @return 两类历史均已提交且无已知覆盖时标记正常退出；否则释放连接并保留未闭合 run，返回 false。
     */
    Q_INVOKABLE bool shutdown();

    /** @brief 返回当前未删除设备台账的V1 JSON响应，只能在存储线程调用。 */
    Q_INVOKABLE QByteArray listDevices();

    /** @brief 按稳定UUID返回一台未删除设备的完整最新配置。 */
    Q_INVOKABLE QByteArray getDevice(const QString &deviceUuid);

    /** @brief 校验JSON并创建一台完整雷达revision，返回包含稳定UUID的V1响应。 */
    Q_INVOKABLE QByteArray createDevice(const QByteArray &requestBody);

    /** @brief 按路径UUID和请求expectedRevision追加设备配置revision。 */
    Q_INVOKABLE QByteArray updateDevice(const QString &deviceUuid, const QByteArray &requestBody);

    /** @brief 按路径UUID和请求revision追加软删除tombstone。 */
    Q_INVOKABLE QByteArray deleteDevice(const QString &deviceUuid, const QByteArray &requestBody);

    /** @brief 返回当前未删除任务及其持久化设备阵面绑定。 */
    Q_INVOKABLE QByteArray listTasks();

    /** @brief 返回可复用控制模板；模板不是运行状态且不会随任务参数修改。 */
    Q_INVOKABLE QByteArray listTaskTemplates();

    /** @brief 校验任务JSON并通过唯一TaskService创建Draft任务。 */
    Q_INVOKABLE QByteArray createTask(const QByteArray &requestBody);

    /** @brief 选择一个已持久化任务进入Ready阶段，不发送设备控制命令。 */
    Q_INVOKABLE QByteArray activateTask(const QString &taskUuid);

    /** @brief 设备控制快照准备失败时撤销刚激活的 Ready 任务，不修改草稿或采集会话。 */
    bool cancelTaskActivation(const QUuid &taskUuid);

    /** @brief 完成当前Ready任务并解除后续新建任务门禁。 */
    Q_INVOKABLE QByteArray completeTask(const QString &taskUuid);

    /** @brief 请求 TaskService 归档已完成任务，只返回本地持久化结果。 */
    Q_INVOKABLE QByteArray archiveTask(const QString &taskUuid);

    /** @brief 返回当前已选择进入Ready阶段的任务；尚未选择时返回空。 */
    Q_INVOKABLE QByteArray activeTask();

    /** @brief 按稳定UUID软删除没有活动采集的任务。 */
    Q_INVOKABLE QByteArray deleteTask(const QString &taskUuid);

    /**
     * @brief 在存储线程内校验并持久化任务控制参数，返回供上层同步设备运行副本的任务值。
     * @param taskUuid 请求路径中的稳定任务 UUID 文本。
     * @param requestBody 完整请求体，顶层必须是含 controlSettings 的 JSON object。
     * @param preparedTaskId 主线程读取的 DeviceController 准备任务句柄；零表示该任务没有运行副本。
     * @param appliedTaskId 成功时写入目标任务的当前进程句柄。
     * @param appliedTask 成功时写入更新后的完整任务台账快照。
     * @param response 始终写入 V1 成功或失败 JSON；失败时含稳定 error.code。
     * @return 结构、值域、任务状态与 SQLite 提交全部通过返回 true。
     * @note 仅由 ApplicationController 在存储线程同步调用；不调用 DeviceController、不发送 UDP，
     * 也不保存第二份长期参数。Running 阶段且 preparedTaskId 与目标任务不一致时在落库前拒绝。
     */
    bool updateTaskControlSettings(const QString &taskUuid,
                                   const QByteArray &requestBody,
                                   core::model::TaskId preparedTaskId,
                                   core::model::TaskId &appliedTaskId,
                                   core::task::TaskDefinition &appliedTask,
                                   QByteArray &response);

    /**
     * @brief 在存储线程发布一条“任务快照已更新但设备运行副本替换失败”的 Control 域故障事实。
     * @param taskId 已确认属于当前 TaskService 的任务句柄。
     * @return 事件通过门禁并写入 EventTopic 返回 true；未初始化或门禁拒绝返回 false。
     * @note 只发布已发生事实，不改变任务或设备状态，也不重试；发布失败不回滚任何业务结果。
     * @warning 必须在存储线程调用；主线程应通过 BlockingQueuedConnection 调用本方法。
     */
    bool publishTaskSettingsInvariantFault(core::model::TaskId taskId);

    /** @brief 解析当前Ready任务的唯一设备运行身份，不创建会话或改变状态。 */
    bool resolveActiveTaskContext(ActiveTaskRuntimeContext &context);

    /** @brief 为已解析上下文创建逐阵面会话并推进TaskState至Running。 */
    bool startCollections(ActiveTaskRuntimeContext &context);

    /** @brief 收口上下文中的全部活动会话，最后一个停止后TaskState回到Ready。 */
    bool stopCollections(const std::array<core::model::AcquisitionId, 4> &acquisitionIds);

    /** @brief 返回最近初始化或关闭失败原因；只在同步调用返回后由组合根读取。 */
    QString lastError() const;

private:
    /**
     * @brief 构造并发布一条携带 TaskId 的 Control 域任务参数故障事实。
     * @param code 已登记的 Control 域事件码；严重级别由事件码决定。
     * @param taskId 非零目标任务句柄。
     * @return 门禁通过并写入 EventTopic 返回 true；入口为空、句柄为零或门禁拒绝返回 false。
     */
    bool publishTaskSettingsFault(core::model::EventCode code, core::model::TaskId taskId);

    /**
     * @brief 读取唯一 TaskState 的当前阶段，且仅当它属于给定任务时返回。
     * @param taskId 待核对的任务句柄；零返回 None。
     * @return 该任务当前阶段；不是当前活动任务时返回 None，避免把别的任务阶段套用过来。
     */
    core::model::TaskPhase activePhaseOf(core::model::TaskId taskId) const;

    QString m_databasePath; ///< SQLite 文件路径；父目录必须在进入存储线程前存在。
    QString m_applicationVersion; ///< application_runs 记录的应用版本。
    core::model::StableIdRegistry &m_stableIds; ///< 跨线程稳定身份注册表，生命周期由 ApplicationRuntime 管理。
    const core::domain::HistoryTopic<core::model::TrackPacket> &m_trackTopic; ///< 数据域唯一航迹历史，只由 Reader 读取。
    core::domain::EventTopic<core::model::SystemEvent> &m_eventTopic; ///< 数据域唯一事件历史；Producer 写入，Reader 只读。
    core::domain::LatestState<core::model::TaskState> &m_taskState; ///< TaskService唯一写入的任务运行状态。
    std::unique_ptr<core::archive::SqliteArchiveRepository> m_archiveRepository; ///< 当前线程唯一 SQLite 连接 Owner。
    std::unique_ptr<core::persistence::SqliteRuntimeRepository> m_runtimeRepository; ///< 复用唯一连接的低频持久化入口。
    std::unique_ptr<core::config::DeviceConfigService> m_deviceService; ///< 设备 revision 与运行时句柄唯一 Owner。
    std::unique_ptr<core::producer::SystemEventPublisher> m_eventPublisher; ///< 借用唯一 EventTopic 的任务事件门禁；先于 TaskService 创建并晚于它释放。
    std::unique_ptr<core::task::TaskService> m_taskService; ///< 任务和采集会话唯一 Owner。
    std::unique_ptr<core::execution::ExecutionLane> m_archiveLane; ///< 唯一存储线程上的合并唤醒通道。
    std::unique_ptr<core::archive::ArchiveReader> m_archiveReader; ///< Track/Event Topic 的正式归档 Consumer。
    QString m_lastError; ///< 最近失败原因；不进入 Topic 或 Web 数据流。
    quint64 m_nextFaultTraceId = 1; ///< 任务参数故障事件的进程内诊断关联号；零不使用。
};

} // namespace core::app
