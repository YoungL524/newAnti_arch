/**
 * @file ApplicationController.h
 * @brief 定义任务稳定身份到设备控制运行身份的最小编排边界。
 * @details 上游 HTTP 任务激活和采集命令通过 StorageRuntime 提供已解析任务；下游准备 DeviceController 快照、
 * 异步编排本地会话和航迹入口。本类不直接写 SQLite、Socket 或 LatestState；新人从 activateTask() 阅读。
 */

#pragma once

#include "core/model/EntityIds.h"
#include "core/config/DeviceConfiguration.h"
#include "core/task/TaskDefinition.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUuid>
#include <QObject>

#include <array>
#include <functional>

namespace core::control {
class DeviceController;
}

namespace core::producer {
class TrackIngressProducer;
}

namespace core::app {

class StorageRuntime;

/**
 * @brief 在一次有界异步请求内传递当前任务的唯一设备及逐阵面会话身份。
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
    core::config::DeviceConfiguration configuration; ///< 激活任务时提供给控制Owner的设备部署参数值副本。
    quint8 faceMask = 0; ///< 当前任务冻结的启用阵面位图。
};

/**
 * @brief 将稳定任务身份异步编排为唯一设备 Owner 的运行快照和本地采集状态。
 * @warning 所有入口及完成回调均在创建本对象的主线程执行；存储线程只处理值副本和数据库操作。
 * completed 必须非空；HTTP 写互斥及关停收口保证本对象和 StorageRuntime 覆盖每次请求。
 */
class ApplicationController final : public QObject
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

    /**
     * @brief 异步激活任务并准备唯一控制快照，准备失败时先撤销存储激活再返回。
     * @param storageRuntime 已初始化且生命周期覆盖请求的唯一存储 Owner。
     * @param expectedTaskUuid 请求指定的稳定任务 UUID。
     * @param completed 在本对象主线程执行一次的 V1 JSON 完成回调；不能绑定客户端存活期。
     * @note HTTP 边界必须串行受理全部写请求，直至完成回调；本对象和存储在请求收口后才能销毁。
     */
    void activateTask(StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid,
                      std::function<void(QByteArray)> completed);

    /**
     * @brief 异步完成存储任务，成功后在主线程撤销周期控制快照。
     * @param storageRuntime 生命周期覆盖请求的存储 Owner。
     * @param expectedTaskUuid 待完成任务 UUID。
     * @param completed 主线程一次性 V1 响应回调；与其他写请求互斥。
     */
    void completeTask(StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid,
                      std::function<void(QByteArray)> completed);

    /** @brief 完成任务后清除当前控制快照及周期发送；没有准备态时视为已清除。 */
    bool clearActiveTask();

    /** @brief 清除匹配任务的设备控制准备态；不会结束会话或发送待机命令。 */
    bool clearPreparedTask(core::model::TaskId taskId);

    /**
     * @brief 将TaskService已创建的逐阵面会话绑定到唯一航迹入口。
     * @return 任务、设备、阵面集合与准备态完全一致且Producer接受绑定时返回true。
     * @note 本重载只绑定本地航迹身份；50 ms控制发送已由任务激活和双心跳独立门控。
     */
    bool startAcquisition(
        core::model::EndpointId endpointId,
        core::model::DeviceId deviceId,
        core::model::TaskId taskId,
        const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds);

    /**
     * @brief 异步创建逐阵面会话，回到主线程绑定航迹和采集状态；绑定失败先异步收口会话。
     * @param storageRuntime 生命周期覆盖请求的存储 Owner。
     * @param expectedTaskUuid 指定活动任务 UUID。
     * @param completed 主线程一次性 V1 响应回调；与其他写请求互斥，断开客户端不取消回滚。
     */
    void startAcquisition(StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid,
                          std::function<void(QByteArray)> completed);

    /**
     * @brief 异步收口指定活动任务的全部会话，再在主线程停止航迹入口及本地采集。
     * @param storageRuntime 生命周期覆盖请求的存储 Owner。
     * @param expectedTaskUuid 指定活动任务 UUID。
     * @param completed 主线程一次性 V1 响应回调；持久化失败保留当前本地采集。
     * @note 与其他写请求互斥；等待存储期间 UDP、心跳和周期控制继续运行。
     */
    void stopAcquisition(StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid,
                         std::function<void(QByteArray)> completed);

    /**
     * @brief 异步持久化参数，提交成功后在主线程一次替换设备运行副本。
     * @param storageRuntime 生命周期覆盖请求的唯一存储组合根。
     * @param expectedTaskUuid 请求路径中的稳定任务 UUID。
     * @param requestBody 完整请求体，顶层必须是含 controlSettings 的 JSON object。
     * @param completed 主线程一次性 V1 响应回调；存储失败保留原错误码。
     * @note 与其他写请求互斥；等待提交时周期发送旧快照，完成回调后发送新快照，不承诺跨线程原子事务。
     */
    void updateTaskControlSettings(StorageRuntime &storageRuntime,
                                   const QUuid &expectedTaskUuid,
                                   const QByteArray &requestBody,
                                   std::function<void(QByteArray)> completed);

    /** @brief 返回最近一次编排失败原因。 */
    QString lastError() const;

private:
    core::control::DeviceController &m_deviceController; ///< 唯一命令Owner借用关系，不拥有Socket或线程。
    core::producer::TrackIngressProducer &m_trackIngress; ///< 唯一航迹发布入口借用关系，不拥有Topic。
    QString m_lastError; ///< 最近低频编排错误，不进入共享数据域。
};

} // namespace core::app
