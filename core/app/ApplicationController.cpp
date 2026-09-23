#include "core/app/ApplicationController.h"

#include "core/app/StorageRuntime.h"
#include "core/control/DeviceController.h"
#include "core/producer/TrackIngressProducer.h"

#include <QMetaObject>
#include <QSet>
#include <QThread>

namespace core::app {

/**
 * @brief 固定ApplicationController到单一设备控制Owner，禁止运行期切换命令执行者。
 */
ApplicationController::ApplicationController(
    core::control::DeviceController &deviceController,
    core::producer::TrackIngressProducer &trackIngress)
    : m_deviceController(deviceController)
    , m_trackIngress(trackIngress)
{
}

/**
 * @brief 从非零会话槽推导阵面位图并与DeviceController准备态核对，匹配后一次替换Producer上下文。
 */
bool ApplicationController::startAcquisition(
    core::model::EndpointId endpointId,
    core::model::DeviceId deviceId,
    core::model::TaskId taskId,
    const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds)
{
    quint8 faceMask = 0;
    for (quint8 faceIndex = 0; faceIndex < faceAcquisitionIds.size(); ++faceIndex) {
        if (faceAcquisitionIds[faceIndex].isSet()) {
            faceMask |= quint8(1U) << faceIndex;
        }
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
 * @brief 串行完成身份解析、任务准备、会话创建、航迹绑定和DeviceState发布，后半段失败时收口已建会话。
 * @details 存储调用仅在跨线程时使用阻塞queued调用；心跳不是开始本地PPI扫描的门禁。
 */
bool ApplicationController::startAcquisition(
    StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid)
{
    if (expectedTaskUuid.isNull()) {
        m_lastError = QStringLiteral("任务UUID无效");
        return false;
    }
    ActiveTaskRuntimeContext context;
    bool resolved = false;
    if (storageRuntime.thread() == QThread::currentThread()) {
        resolved = storageRuntime.resolveActiveTaskContext(context);
    } else {
        QMetaObject::invokeMethod(&storageRuntime, [&] {
            resolved = storageRuntime.resolveActiveTaskContext(context);
        }, Qt::BlockingQueuedConnection);
    }
    if (!resolved || context.task.taskUuid != expectedTaskUuid) {
        m_lastError = resolved ? QStringLiteral("请求任务不是当前活动任务")
                               : storageRuntime.lastError();
        return false;
    }
    QHash<QUuid, core::model::DeviceId> devices;
    devices.insert(context.task.deviceBindings.front().deviceUuid, context.deviceId);
    if (!prepareTask(context.taskId, context.task, devices)) return false;

    bool sessionsStarted = false;
    if (storageRuntime.thread() == QThread::currentThread()) {
        sessionsStarted = storageRuntime.startCollections(context);
    } else {
        QMetaObject::invokeMethod(&storageRuntime, [&] {
            sessionsStarted = storageRuntime.startCollections(context);
        }, Qt::BlockingQueuedConnection);
    }
    if (!sessionsStarted) {
        m_lastError = storageRuntime.lastError();
        return false;
    }
    if (startAcquisition(context.endpointId, context.deviceId, context.taskId,
                         context.faceAcquisitionIds)
        && m_deviceController.beginAcquisition(
            context.deviceId, context.endpointId, context.configurationRevision,
            context.configuration,
            context.faceAcquisitionIds)) {
        m_lastError.clear();
        return true;
    }

    const QString firstError = m_lastError.isEmpty()
        ? m_deviceController.lastError() : m_lastError;
    m_trackIngress.clearBinding();
    if (storageRuntime.thread() == QThread::currentThread()) {
        storageRuntime.stopCollections(context.faceAcquisitionIds);
    } else {
        QMetaObject::invokeMethod(&storageRuntime, [&] {
            storageRuntime.stopCollections(context.faceAcquisitionIds);
        }, Qt::BlockingQueuedConnection);
    }
    m_lastError = firstError;
    return false;
}

/**
 * @brief 先由TaskService关闭全部会话，再清除航迹入口和DeviceState，避免停止后继续产生可归档航迹。
 */
bool ApplicationController::stopAcquisition(
    StorageRuntime &storageRuntime, const QUuid &expectedTaskUuid)
{
    ActiveTaskRuntimeContext context;
    bool resolved = false;
    if (storageRuntime.thread() == QThread::currentThread()) {
        resolved = storageRuntime.resolveActiveTaskContext(context);
    } else {
        QMetaObject::invokeMethod(&storageRuntime, [&] {
            resolved = storageRuntime.resolveActiveTaskContext(context);
        }, Qt::BlockingQueuedConnection);
    }
    core::model::DeviceId deviceId;
    core::model::EndpointId endpointId;
    core::model::TaskId taskId;
    std::array<core::model::AcquisitionId, 4> acquisitionIds {};
    if (!resolved || context.task.taskUuid != expectedTaskUuid
        || !m_deviceController.activeAcquisition(
            deviceId, endpointId, taskId, acquisitionIds)) {
        m_lastError = !resolved ? storageRuntime.lastError()
            : context.task.taskUuid != expectedTaskUuid
                ? QStringLiteral("请求任务不是当前活动任务")
                : QStringLiteral("当前没有正在进行的本地采集");
        return false;
    }
    bool stopped = false;
    if (storageRuntime.thread() == QThread::currentThread()) {
        stopped = storageRuntime.stopCollections(acquisitionIds);
    } else {
        QMetaObject::invokeMethod(&storageRuntime, [&] {
            stopped = storageRuntime.stopCollections(acquisitionIds);
        }, Qt::BlockingQueuedConnection);
    }
    if (!stopped) {
        m_lastError = storageRuntime.lastError();
        return false;
    }
    m_trackIngress.clearBinding();
    if (!m_deviceController.endAcquisition()) {
        m_lastError = m_deviceController.lastError();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 先完成稳定UUID、重复绑定和阵面位图校验，再一次性交给DeviceController，失败不改变旧准备态。
 */
bool ApplicationController::prepareTask(
    core::model::TaskId taskId,
    const core::task::TaskDefinition &task,
    const QHash<QUuid, core::model::DeviceId> &deviceIdsByUuid)
{
    if (!taskId.isSet() || task.deviceBindings.isEmpty()) {
        m_lastError = QStringLiteral("任务句柄和设备绑定不能为空");
        return false;
    }
    QHash<core::model::DeviceId, quint8> runtimeFaceMasks;
    QSet<QUuid> seenDeviceUuids;
    for (const core::task::TaskDeviceBinding &binding : task.deviceBindings) {
        const auto device = deviceIdsByUuid.constFind(binding.deviceUuid);
        if (binding.deviceUuid.isNull() || seenDeviceUuids.contains(binding.deviceUuid)
            || device == deviceIdsByUuid.cend() || !device.value().isSet()
            || binding.faceMask == 0U || (binding.faceMask & 0xF0U) != 0U
            || runtimeFaceMasks.contains(device.value())) {
            m_lastError = QStringLiteral("任务设备绑定无法唯一解析为有效运行时设备");
            return false;
        }
        seenDeviceUuids.insert(binding.deviceUuid);
        runtimeFaceMasks.insert(device.value(), binding.faceMask);
    }
    if (!m_deviceController.prepareTask(taskId, task.controlSettings, runtimeFaceMasks)) {
        m_lastError = m_deviceController.lastError();
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 委托唯一设备Owner按任务身份清除，防止ApplicationController保存第二份准备状态。
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
 * @brief 返回最近编排错误；成功操作后为空。
 */
QString ApplicationController::lastError() const
{
    return m_lastError;
}

} // namespace core::app
