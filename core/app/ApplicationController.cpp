#include "core/app/ApplicationController.h"
#include "core/app/StorageRuntime.h"
#include "core/control/DeviceController.h"
#include "core/producer/TrackIngressProducer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>

namespace core::app {
namespace {
/**
 * @brief 编排失败沿原 HTTP 请求返回，不把本地失败伪装成设备确认。
 */
QByteArray errorResponse(const QString &code, const QString &message)
{
    return QJsonDocument(QJsonObject{{"ok", false}, {"error", QJsonObject{
        {"code", code}, {"message", message}}}}).toJson(QJsonDocument::Compact);
}

/**
 * @brief 仅判断存储已经返回的 V1 本地处理结果，不读取任何运行状态。
 */
bool succeeded(const QByteArray &response)
{
    return QJsonDocument::fromJson(response).object().value(QStringLiteral("ok")).toBool();
}

/**
 * @brief 本地采集状态已经转换后生成响应，不代表 DP 已收到工作或待机报文。
 */
QByteArray acquisitionResponse(bool acquisition)
{
    return QJsonDocument(QJsonObject{{"ok", true}, {"data", QJsonObject{
        {"acquisition", acquisition}}}}).toJson(QJsonDocument::Compact);
}
}

/**
 * @brief 保持编排者在创建它的主线程，借用唯一设备 Owner 和航迹入口。
 */
ApplicationController::ApplicationController(
    core::control::DeviceController &deviceController,
    core::producer::TrackIngressProducer &trackIngress)
    : m_deviceController(deviceController), m_trackIngress(trackIngress)
{
}

/**
 * @brief 在主线程核对任务和阵面身份后一次绑定入口；未改变采集状态前不会发送工作报文。
 */
bool ApplicationController::startAcquisition(
    core::model::EndpointId endpointId, core::model::DeviceId deviceId,
    core::model::TaskId taskId,
    const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds)
{
    quint8 faceMask = 0;
    for (quint8 face = 0; face < faceAcquisitionIds.size(); ++face) {
        if (faceAcquisitionIds[face].isSet()) faceMask |= quint8(1U) << face;
    }
    if (!endpointId.isSet() || !deviceId.isSet() || taskId != m_deviceController.preparedTaskId()
        || faceMask == 0U || faceMask != m_deviceController.faceMask(deviceId)) {
        m_lastError = QStringLiteral("采集会话身份与当前准备任务不一致");
        return false;
    }
    if (!m_trackIngress.bind(endpointId, deviceId, taskId, faceAcquisitionIds)) {
        m_lastError = QStringLiteral("航迹入口拒绝采集会话绑定");
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 存储线程激活并解析快照后返回主线程；控制准备失败必须等待撤销结果才能结束写请求。
 * @details 上下文只按值跨两个执行阶段传递；不捕获栈引用，不从存储线程访问 DeviceController。
 */
void ApplicationController::activateTask(StorageRuntime &storage, const QUuid &uuid,
                                         std::function<void(QByteArray)> completed)
{
    const bool queued = QMetaObject::invokeMethod(&storage, [this, &storage, uuid, completed] {
        QByteArray response = storage.activateTask(uuid.toString(QUuid::WithoutBraces));
        ActiveTaskRuntimeContext context;
        if (succeeded(response) && (!storage.resolveActiveTaskContext(context)
                                   || context.task.taskUuid != uuid)) {
            const QString reason = storage.lastError().isEmpty()
                ? QStringLiteral("请求任务不是当前活动任务") : storage.lastError();
            const bool cancelled = storage.cancelTaskActivation(uuid);
            response = errorResponse(cancelled ? QStringLiteral("COMMAND_REJECTED")
                                              : QStringLiteral("INTERNAL_ERROR"),
                cancelled ? reason : reason + QStringLiteral("；任务激活撤销失败：") + storage.lastError());
        }
        QMetaObject::invokeMethod(this, [this, &storage, uuid, context, response, completed] {
            if (!succeeded(response)) { completed(response); return; }
            QHash<QUuid, core::model::DeviceId> devices;
            devices.insert(context.task.deviceBindings.front().deviceUuid, context.deviceId);
            if (prepareTask(context.taskId, context.task, devices)
                && m_deviceController.configureControlSnapshot(context.deviceId, context.configuration)) {
                completed(response);
                return;
            }
            const QString reason = m_lastError.isEmpty() ? m_deviceController.lastError() : m_lastError;
            if (m_deviceController.preparedTaskId() == context.taskId) clearPreparedTask(context.taskId);
            QMetaObject::invokeMethod(&storage, [this, &storage, uuid, reason, completed] {
                const bool cancelled = storage.cancelTaskActivation(uuid);
                const QByteArray failure = errorResponse(
                    cancelled ? QStringLiteral("COMMAND_REJECTED") : QStringLiteral("INTERNAL_ERROR"),
                    cancelled ? reason : reason + QStringLiteral("；任务激活撤销失败：") + storage.lastError());
                QMetaObject::invokeMethod(this, [completed, failure] { completed(failure); }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) completed(errorResponse(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("存储线程调用失败")));
}

/**
 * @brief 只在持久化任务完成后清除控制准备态；存储等待期间主线程继续收包及发送当前快照。
 */
void ApplicationController::completeTask(StorageRuntime &storage, const QUuid &uuid,
                                         std::function<void(QByteArray)> completed)
{
    const bool queued = QMetaObject::invokeMethod(&storage, [this, &storage, uuid, completed] {
        const QByteArray response = storage.completeTask(uuid.toString(QUuid::WithoutBraces));
        QMetaObject::invokeMethod(this, [this, response, completed] {
            if (succeeded(response) && !clearActiveTask()) {
                completed(errorResponse(QStringLiteral("INTERNAL_ERROR"), m_lastError));
            } else completed(response);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) completed(errorResponse(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("存储线程调用失败")));
}

/**
 * @brief 存储线程先解析并创建会话，主线程再绑定入口和状态；失败异步停止已建会话并报告回滚失败。
 * @details 主线程准备态在入队前取值，HTTP 写互斥保证请求收口前任务和设备配置不会被其他请求替换。
 */
void ApplicationController::startAcquisition(StorageRuntime &storage, const QUuid &uuid,
                                             std::function<void(QByteArray)> completed)
{
    const auto preparedTaskId = m_deviceController.preparedTaskId();
    const bool queued = QMetaObject::invokeMethod(&storage, [this, &storage, uuid, preparedTaskId, completed] {
        ActiveTaskRuntimeContext context;
        QByteArray failure;
        if (uuid.isNull()) failure = errorResponse(QStringLiteral("INVALID_REQUEST"), QStringLiteral("任务UUID无效"));
        else if (!storage.resolveActiveTaskContext(context)) {
            failure = errorResponse(QStringLiteral("COMMAND_REJECTED"), storage.lastError());
        } else if (context.task.taskUuid != uuid || context.taskId != preparedTaskId) {
            failure = errorResponse(QStringLiteral("COMMAND_REJECTED"), QStringLiteral("当前任务尚未准备控制快照"));
        } else if (!storage.startCollections(context)) {
            failure = errorResponse(QStringLiteral("COMMAND_REJECTED"), storage.lastError());
        }
        QMetaObject::invokeMethod(this, [this, &storage, context, failure, completed] {
            if (!failure.isEmpty()) { completed(failure); return; }
            if (startAcquisition(context.endpointId, context.deviceId, context.taskId, context.faceAcquisitionIds)
                && m_deviceController.beginAcquisition(context.deviceId, context.endpointId,
                    context.configurationRevision, context.configuration, context.faceAcquisitionIds)) {
                completed(acquisitionResponse(true));
                return;
            }
            const QString reason = m_lastError.isEmpty() ? m_deviceController.lastError() : m_lastError;
            m_trackIngress.clearBinding();
            QMetaObject::invokeMethod(&storage, [this, &storage, context, reason, completed] {
                const bool rolledBack = storage.stopCollections(context.faceAcquisitionIds);
                const QByteArray response = errorResponse(
                    rolledBack ? QStringLiteral("COMMAND_REJECTED") : QStringLiteral("INTERNAL_ERROR"),
                    rolledBack ? reason : reason + QStringLiteral("；采集会话收口失败：") + storage.lastError());
                QMetaObject::invokeMethod(this, [completed, response] { completed(response); }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) completed(errorResponse(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("存储线程调用失败")));
}

/**
 * @brief 从设备 Owner 取得当前身份后异步关闭数据库会话，成功返回主线程才清除入口和发送一次待机。
 * @details 等待期间本地采集仍然成立；HTTP 写互斥保证对应会话不会被其他写请求替换。
 */
void ApplicationController::stopAcquisition(StorageRuntime &storage, const QUuid &uuid,
                                            std::function<void(QByteArray)> completed)
{
    core::model::DeviceId deviceId;
    core::model::EndpointId endpointId;
    core::model::TaskId taskId;
    std::array<core::model::AcquisitionId, 4> acquisitionIds {};
    if (uuid.isNull() || !m_deviceController.activeAcquisition(deviceId, endpointId, taskId, acquisitionIds)) {
        completed(errorResponse(uuid.isNull() ? QStringLiteral("INVALID_REQUEST") : QStringLiteral("COMMAND_REJECTED"),
                                QStringLiteral("任务UUID无效或当前没有本地采集")));
        return;
    }
    const bool queued = QMetaObject::invokeMethod(&storage, [this, &storage, uuid, taskId, acquisitionIds, completed] {
        ActiveTaskRuntimeContext context;
        QByteArray failure;
        if (!storage.resolveActiveTaskContext(context)) {
            failure = errorResponse(QStringLiteral("COMMAND_REJECTED"), storage.lastError());
        } else if (context.task.taskUuid != uuid || context.taskId != taskId) {
            failure = errorResponse(QStringLiteral("COMMAND_REJECTED"), QStringLiteral("请求任务不是当前采集任务"));
        } else if (!storage.stopCollections(acquisitionIds)) {
            failure = errorResponse(QStringLiteral("COMMAND_REJECTED"), storage.lastError());
        }
        QMetaObject::invokeMethod(this, [this, failure, completed] {
            if (!failure.isEmpty()) { completed(failure); return; }
            m_trackIngress.clearBinding();
            if (!m_deviceController.endAcquisition()) {
                completed(errorResponse(QStringLiteral("INTERNAL_ERROR"), m_deviceController.lastError()));
            } else completed(acquisitionResponse(false));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) completed(errorResponse(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("存储线程调用失败")));
}

/**
 * @brief 先核对全部稳定身份与阵面位图，再委托设备 Owner 替换准备态，失败不保留半组绑定。
 */
bool ApplicationController::prepareTask(core::model::TaskId taskId,
    const core::task::TaskDefinition &task,
    const QHash<QUuid, core::model::DeviceId> &deviceIdsByUuid)
{
    if (!taskId.isSet() || task.deviceBindings.isEmpty()) {
        m_lastError = QStringLiteral("任务句柄和设备绑定不能为空");
        return false;
    }
    QHash<core::model::DeviceId, quint8> masks;
    QSet<QUuid> seen;
    for (const auto &binding : task.deviceBindings) {
        const auto device = deviceIdsByUuid.constFind(binding.deviceUuid);
        if (binding.deviceUuid.isNull() || seen.contains(binding.deviceUuid)
            || device == deviceIdsByUuid.cend() || !device.value().isSet()
            || binding.faceMask == 0U || (binding.faceMask & 0xF0U) != 0U || masks.contains(device.value())) {
            m_lastError = QStringLiteral("任务设备绑定无法唯一解析为有效运行时设备");
            return false;
        }
        seen.insert(binding.deviceUuid);
        masks.insert(device.value(), binding.faceMask);
    }
    if (!m_deviceController.prepareTask(taskId, task.controlSettings, masks)) {
        m_lastError = m_deviceController.lastError();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 已完成任务没有准备态时幂等成功，否则只清除当前设备 Owner 的准备身份。
 */
bool ApplicationController::clearActiveTask()
{
    const auto taskId = m_deviceController.preparedTaskId();
    return !taskId.isSet() || clearPreparedTask(taskId);
}

/**
 * @brief 以任务句柄保护清除操作，不能由旧请求撤销其他任务的周期发送。
 */
bool ApplicationController::clearPreparedTask(core::model::TaskId taskId)
{
    if (!m_deviceController.clearPreparedTask(taskId)) {
        m_lastError = m_deviceController.lastError();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 返回主线程最近一次身份编排错误，存储错误只通过请求的值响应传回。
 */
QString ApplicationController::lastError() const
{
    return m_lastError;
}

/**
 * @brief 等待数据库时保留旧控制快照；提交完成后在主线程整体替换，不暂停心跳及 50 ms 定时器。
 * @details 写请求互斥使 preparedTaskId 跨异步阶段稳定；运行副本不变量失败在存储线程发布同链事件。
 */
void ApplicationController::updateTaskControlSettings(StorageRuntime &storage,
    const QUuid &uuid, const QByteArray &body, std::function<void(QByteArray)> completed)
{
    const auto preparedTaskId = m_deviceController.preparedTaskId();
    const bool queued = QMetaObject::invokeMethod(&storage, [this, &storage, uuid, body, preparedTaskId, completed] {
        core::model::TaskId appliedTaskId;
        core::task::TaskDefinition appliedTask;
        QByteArray response;
        const bool updated = storage.updateTaskControlSettings(uuid.toString(QUuid::WithoutBraces),
            body, preparedTaskId, appliedTaskId, appliedTask, response);
        QMetaObject::invokeMethod(this, [this, &storage, preparedTaskId, appliedTaskId, appliedTask, response, updated, completed] {
            if (updated && preparedTaskId.isSet() && preparedTaskId == appliedTaskId
                && !m_deviceController.applyControlSettings(appliedTaskId, appliedTask.controlSettings)) {
                QMetaObject::invokeMethod(&storage, [this, &storage, appliedTaskId, completed] {
                    storage.publishTaskSettingsInvariantFault(appliedTaskId);
                    const QByteArray failure = errorResponse(QStringLiteral("INTERNAL_ERROR"),
                        QStringLiteral("任务快照已更新，但设备运行副本替换失败"));
                    QMetaObject::invokeMethod(this, [completed, failure] { completed(failure); }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            } else completed(response);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) completed(errorResponse(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("存储线程调用失败")));
}

} // namespace core::app
