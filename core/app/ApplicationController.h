/**
 * @file ApplicationController.h
 * @brief 定义任务稳定身份到设备控制运行身份的最小编排边界。
 * @details 上游未来由HTTP采集命令提供TaskService已解析的任务，下游只调用DeviceController准备或清除运行副本。
 * 本阶段不访问SQLite、不创建采集会话、不绑定航迹入口、不发送UDP，也不直接写入任何LatestState。
 */

#pragma once

#include "core/model/EntityIds.h"
#include "core/config/DeviceConfiguration.h"
#include "core/task/TaskDefinition.h"

#include <QHash>
#include <QString>
#include <QUuid>

#include <array>

namespace core::control {
class DeviceController;
}

namespace core::producer {
class TrackIngressProducer;
}

namespace core::app {

class StorageRuntime;

/**
 * @brief 在一次同步编排调用内传递当前任务的唯一设备及逐阵面会话身份。
 * @note 本值不进入Topic、不跨请求保存；第一版任务只允许绑定一台完整雷达。
 */
struct ActiveTaskRuntimeContext
{
    core::task::TaskDefinition task; ///< SQLite冻结的当前任务值副本。
    std::array<core::model::AcquisitionId, 4> faceAcquisitionIds {}; ///< 启动后各阵面独立会话。
    core::model::TaskId taskId; ///< 当前进程任务句柄。
    core::model::DeviceId deviceId; ///< 当前任务唯一完整雷达句柄。
    core::model::EndpointId endpointId; ///< 与设备revision一致的主航迹端点。
    quint32 configurationRevision = 0; ///< DeviceState展示使用的非零配置revision。
    core::config::DeviceConfiguration configuration; ///< 开始采集时冻结给控制Owner的设备部署参数。
    quint8 faceMask = 0; ///< 当前任务冻结的启用阵面位图。
};

/** @brief 校验任务绑定并把稳定设备UUID收敛为DeviceController使用的强类型运行时句柄。 */
class ApplicationController final
{
public:
    /** @brief 借用唯一DeviceController和航迹Producer；调用者保证二者生命周期覆盖本对象。 */
    ApplicationController(core::control::DeviceController &deviceController,
                          core::producer::TrackIngressProducer &trackIngress);

    /**
     * @brief 验证一项已激活任务并准备唯一设备控制运行副本。
     * @param taskId TaskService为当前进程解析出的非零任务句柄。
     * @param task SQLite中已冻结设备绑定与参数快照的任务值。
     * @param deviceIdsByUuid DeviceConfigService解析出的稳定UUID到当前revision DeviceId映射。
     * @return 每项绑定均唯一、可解析且阵面非空时返回true；不代表采集已经开始。
     */
    bool prepareTask(
        core::model::TaskId taskId,
        const core::task::TaskDefinition &task,
        const QHash<QUuid, core::model::DeviceId> &deviceIdsByUuid);

    /** @brief 清除匹配任务的设备控制准备态；不会结束会话或发送待机命令。 */
    bool clearPreparedTask(core::model::TaskId taskId);

    /**
     * @brief 将TaskService已创建的逐阵面会话绑定到唯一航迹入口。
     * @return 任务、设备、阵面集合与准备态完全一致且Producer接受绑定时返回true。
     * @note 本重载只绑定本地航迹身份；完整编排重载再由DeviceController启动50 ms控制发送。
     */
    bool startAcquisition(
        core::model::EndpointId endpointId,
        core::model::DeviceId deviceId,
        core::model::TaskId taskId,
        const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds);

    /** @brief 从SQLite Owner解析指定活动任务、创建会话并启动本地PPI/航迹入口。 */
    bool startAcquisition(StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid);

    /** @brief 停止指定活动任务的本地PPI/航迹入口并收口全部逐阵面会话。 */
    bool stopAcquisition(StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid);

    /** @brief 返回最近一次编排失败原因。 */
    QString lastError() const;

private:
    core::control::DeviceController &m_deviceController; ///< 唯一命令Owner借用关系，不拥有Socket或线程。
    core::producer::TrackIngressProducer &m_trackIngress; ///< 唯一航迹发布入口借用关系，不拥有Topic。
    QString m_lastError; ///< 最近低频编排错误，不进入共享数据域。
};

} // namespace core::app
