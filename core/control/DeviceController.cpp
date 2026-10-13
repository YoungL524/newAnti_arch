#include "core/control/DeviceController.h"

#include "core/producer/RuntimeStateGate.h"
#include "core/protocol/DcmpProtocolAdapter.h"
#include "core/producer/SystemEventPublisher.h"
#include "core/task/RadarControlSettingsJson.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace core::control {

/**
 * @brief 建立唯一状态写入关系，并把定时检查固定在对象所属线程。
 */
DeviceController::DeviceController(
    core::domain::LatestState<core::model::DeviceState> &state,
    ControlSender controlSender,
    core::producer::SystemEventPublisher *eventPublisher,
    QObject *parent)
    : QObject(parent)
    , m_state(state)
    , m_heartbeatTimer(this)
    , m_controlTimer(this)
    , m_controlSender(std::move(controlSender))
    , m_eventPublisher(eventPublisher)
{
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publishDeviceState({}, QDateTime::currentMSecsSinceEpoch(), steadyNs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, [this] {
        const auto nowSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        checkHeartbeatDeadlines(QDateTime::currentMSecsSinceEpoch(), nowSteadyNs);
    });
    connect(&m_controlTimer, &QTimer::timeout, this, [this] {
        const auto record = m_state.latest();
        if (m_controlTimer.isActive() && m_preparedTaskId.isSet()
            && record && record->sample->value.dpHeartbeat == 1U
            && record->sample->value.spHeartbeat == 1U) {
            sendControl();
        }
    });
}

/**
 * @brief 重启期限检查时不继承旧在线结论，但保留同一完整状态中的设备运行字段。
 */
void DeviceController::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_lastHeartbeatSteadyNs = {};
    auto sample = *m_state.latest()->sample;
    sample.value.dpHeartbeat = 0;
    sample.value.spHeartbeat = 0;
    sample.value.fpOnlineMask = 0;
    sample.value.fpDataMask = 0;
    sample.value.radarPose.validFaceMask = 0;
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publishDeviceState(std::move(sample), QDateTime::currentMSecsSinceEpoch(), steadyNs);
    m_heartbeatTimer.start(100);
}

/**
 * @brief 停止内部期限唤醒；组合根须先停止网络接收以避免停机尾部继续更新。
 */
void DeviceController::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_heartbeatTimer.stop();
    m_controlTimer.stop();
}

/**
 * @brief 先验证来源字段和单调顺序，再按在线阵面平均位置并保留逐面姿态；数值不变时只续期。
 */
void DeviceController::acceptHeartbeat(
    bool sp, quint16 online, quint16 mask, qint64 utcMs, qint64 steadyNs,
    const core::protocol::DpHeartbeatPayload *dpPayload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const std::size_t index = sp ? 1U : 0U;
    if (online > 1U || (mask & 0xFFF0U) != 0U || utcMs <= 0 || steadyNs <= 0
        || steadyNs < m_lastHeartbeatSteadyNs[index]) {
        return;
    }
    m_lastHeartbeatSteadyNs[index] = steadyNs;
    auto sample = *m_state.latest()->sample;
    quint8 &heartbeat = sp ? sample.value.spHeartbeat : sample.value.dpHeartbeat;
    quint16 &bits = sp ? sample.value.fpDataMask : sample.value.fpOnlineMask;
    const quint8 nextHeartbeat = online != 0U ? 1U : 2U;
    const bool statusChanged = heartbeat != nextHeartbeat || bits != mask;
    bool poseChanged = false;
    if (!sp) {
        auto &pose = sample.value.radarPose;
        const quint8 validMask = online == 1U && dpPayload
            && dpPayload->poseFormatTag == core::protocol::DpHeartbeatPayload::expectedPoseFormatTag
            ? static_cast<quint8>(mask) : 0U;
        const quint8 nextMask = online == 1U ? validMask : 0U;
        poseChanged = pose.validFaceMask != nextMask;
        pose.validFaceMask = nextMask;
        if (dpPayload && nextMask != 0U) {
            qint64 longitudeSum = 0;
            qint64 latitudeSum = 0;
            qint64 altitudeSum = 0;
            qint32 faceCount = 0;
            for (std::size_t face = 0; face < pose.faceAttitudeCdeg.size(); ++face) {
                if ((nextMask & (1U << face)) == 0U) continue;
                longitudeSum += dpPayload->facePose[face][0];
                latitudeSum += dpPayload->facePose[face][1];
                altitudeSum += dpPayload->facePose[face][2];
                ++faceCount;
            }
            const qint32 longitudeE6 = static_cast<qint32>(std::llround(
                static_cast<double>(longitudeSum) / faceCount));
            const qint32 latitudeE6 = static_cast<qint32>(std::llround(
                static_cast<double>(latitudeSum) / faceCount));
            const qint32 altitudeCm = static_cast<qint32>(std::llround(
                static_cast<double>(altitudeSum) / faceCount));
            poseChanged = poseChanged
                || pose.longitudeE6 != longitudeE6
                || pose.latitudeE6 != latitudeE6
                || pose.altitudeCm != altitudeCm;
            pose.longitudeE6 = longitudeE6;
            pose.latitudeE6 = latitudeE6;
            pose.altitudeCm = altitudeCm;
            for (std::size_t face = 0; face < pose.faceAttitudeCdeg.size(); ++face) {
                if ((nextMask & (1U << face)) == 0U) continue;
                for (std::size_t axis = 0; axis < 3U; ++axis) {
                    const qint32 next = dpPayload->facePose[face][axis + 3U];
                    poseChanged = poseChanged || pose.faceAttitudeCdeg[face][axis] != next;
                    pose.faceAttitudeCdeg[face][axis] = next;
                }
            }
        }
    }
    if (!statusChanged && !poseChanged) {
        return;
    }
    heartbeat = nextHeartbeat;
    bits = mask;
    if (!publishDeviceState(std::move(sample), utcMs, steadyNs)) {
        qWarning().noquote() << (sp ? "SP" : "DP") << "心跳状态被运行时门禁拒绝";
        return;
    }
    if (statusChanged) {
        qInfo().noquote() << (sp ? "SP" : "DP") << "心跳状态" << nextHeartbeat
                          << "阵面位图" << mask;
        publishEvent(core::model::EventCode::DeviceHeartbeatChanged,
                     nextHeartbeat == 1U ? core::model::EventSeverity::Info
                                         : core::model::EventSeverity::Warning,
                     sp ? 2 : 1, static_cast<qint64>(nextHeartbeat) * 16 + mask);
    }
    refreshControlTimer();
}

/**
 * @brief 分别评估DP与SP期限；只有达到5秒超时时清除对应FP位图，避免展示陈旧在线事实。
 */
void DeviceController::checkHeartbeatDeadlines(qint64 utcMs, qint64 steadyNs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto sample = *m_state.latest()->sample;
    bool changed = false;
    std::array<quint8, 2> changedStatuses {};
    std::array<quint16, 2> changedMasks {};
    std::array<bool, 2> changedSources {};
    for (std::size_t index = 0; index < m_lastHeartbeatSteadyNs.size(); ++index) {
        const qint64 last = m_lastHeartbeatSteadyNs[index];
        if (last == 0 || steadyNs < last) {
            continue;
        }
        quint8 &heartbeat = index == 1U
            ? sample.value.spHeartbeat : sample.value.dpHeartbeat;
        quint16 &bits = index == 1U
            ? sample.value.fpDataMask : sample.value.fpOnlineMask;
        const qint64 age = steadyNs - last;
        const quint8 nextHeartbeat = age >= 5'000'000'000LL
            ? 0U : age >= 2'000'000'000LL ? 2U : heartbeat;
        if (nextHeartbeat == heartbeat) {
            continue;
        }
        heartbeat = nextHeartbeat;
        if (nextHeartbeat == 0U) {
            bits = 0;
        }
        if (index == 0U && nextHeartbeat != 1U) {
            sample.value.radarPose.validFaceMask = 0;
        }
        changedSources[index] = true;
        changedStatuses[index] = nextHeartbeat;
        changedMasks[index] = bits;
        changed = true;
    }
    if (changed) {
        if (publishDeviceState(std::move(sample), utcMs, steadyNs)) {
            for (std::size_t index = 0; index < changedSources.size(); ++index) {
                if (!changedSources[index]) continue;
                publishEvent(core::model::EventCode::DeviceHeartbeatChanged,
                             core::model::EventSeverity::Warning,
                             index == 0U ? 1 : 2,
                             static_cast<qint64>(changedStatuses[index]) * 16 + changedMasks[index]);
            }
        }
        refreshControlTimer();
    }
}

/**
 * @brief 在局部副本中完成全部验证后替换任务参数；撤销旧设备快照，等待同一激活调用补齐。
 */
bool DeviceController::prepareTask(
    core::model::TaskId taskId,
    const core::task::RadarControlSettings &settings,
    const QHash<core::model::DeviceId, quint8> &deviceFaceMasks)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!taskId.isSet() || deviceFaceMasks.isEmpty()) {
        m_lastError = QStringLiteral("任务句柄和设备阵面集合不能为空");
        return false;
    }
    QString validationError;
    if (!core::task::validateRadarControlSettings(settings, validationError)) {
        m_lastError = QStringLiteral("任务控制参数无效：") + validationError;
        return false;
    }
    for (auto iterator = deviceFaceMasks.cbegin(); iterator != deviceFaceMasks.cend(); ++iterator) {
        if (!iterator.key().isSet() || iterator.value() == 0U
            || (iterator.value() & 0xF0U) != 0U) {
            m_lastError = QStringLiteral("设备句柄或阵面位图无效");
            return false;
        }
    }
    m_preparedTaskId = taskId;
    m_controlSettings = settings;
    m_deviceFaceMasks = deviceFaceMasks;
    m_controlDeviceId = {};
    m_controlConfiguration = {};
    m_controlSendFailed = false;
    auto sample = *m_state.latest()->sample;
    sample.value.radarPose.deviceId = {};
    sample.value.radarPose.validFaceMask = 0;
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publishDeviceState(std::move(sample), QDateTime::currentMSecsSinceEpoch(), steadyNs);
    refreshControlTimer();
    m_lastError.clear();
    return true;
}

/**
 * @brief 验证任务设备可编码为完整 0x0001 后替换部署快照；固定发送目标不取设备台账。
 */
bool DeviceController::configureControlSnapshot(
    core::model::DeviceId deviceId,
    const core::config::DeviceConfiguration &configuration)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_preparedTaskId.isSet() || !deviceId.isSet()
        || m_deviceFaceMasks.value(deviceId, 0U) == 0U
        || core::protocol::DcmpProtocolAdapter::encodeControl(
            m_controlSettings, configuration,
            m_deviceFaceMasks.value(deviceId, 0U), true).isEmpty()) {
        m_lastError = QStringLiteral("任务设备或0x0001快照无效");
        return false;
    }
    m_controlDeviceId = deviceId;
    m_controlConfiguration = configuration;
    auto sample = *m_state.latest()->sample;
    sample.value.radarPose.deviceId = deviceId;
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publishDeviceState(std::move(sample), QDateTime::currentMSecsSinceEpoch(), steadyNs);
    refreshControlTimer();
    m_lastError.clear();
    return true;
}

/**
 * @brief 拒绝由旧请求清除新任务；匹配时同时撤销周期发送，避免完成任务后继续发旧快照。
 */
bool DeviceController::clearPreparedTask(core::model::TaskId taskId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!taskId.isSet() || taskId != m_preparedTaskId) {
        m_lastError = QStringLiteral("待清除任务与当前准备任务不一致");
        return false;
    }
    m_preparedTaskId = {};
    m_controlSettings = {};
    m_deviceFaceMasks.clear();
    m_controlDeviceId = {};
    m_controlConfiguration = {};
    m_controlSendFailed = false;
    auto sample = *m_state.latest()->sample;
    sample.value.radarPose.deviceId = {};
    sample.value.radarPose.validFaceMask = 0;
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    publishDeviceState(std::move(sample), QDateTime::currentMSecsSinceEpoch(), steadyNs);
    refreshControlTimer();
    m_lastError.clear();
    return true;
}

/**
 * @brief 只检查非零任务句柄；准备态与心跳在线状态相互独立。
 */
bool DeviceController::hasPreparedTask() const noexcept
{
    return m_preparedTaskId.isSet();
}

/**
 * @brief 返回本线程串行维护的准备任务身份。
 */
core::model::TaskId DeviceController::preparedTaskId() const noexcept
{
    return m_preparedTaskId;
}

/**
 * @brief 读取当前任务的设备阵面选择，不存在的设备明确返回零。
 */
quint8 DeviceController::faceMask(core::model::DeviceId deviceId) const noexcept
{
    return m_deviceFaceMasks.value(deviceId, 0U);
}

/**
 * @brief 返回低频值副本，使调用方不能绕过Owner原地修改当前控制参数。
 */
core::task::RadarControlSettings DeviceController::controlSettings() const
{
    return m_controlSettings;
}

/**
 * @brief 只接受当前准备任务并做纯值拷贝，避免在持久化之后引入新的失败点。
 */
bool DeviceController::applyControlSettings(
    core::model::TaskId taskId, const core::task::RadarControlSettings &settings)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!taskId.isSet() || taskId != m_preparedTaskId) {
        return false;
    }
    m_controlSettings = settings;
    return true;
}

/**
 * @brief 核对会话后发布 Collecting 状态，再向固定 DP 目标提交一次工作命令。
 * @details 工作命令不受心跳门控；发送失败只记录诊断，本地采集与周期参数链仍按已发布状态运行。
 */
bool DeviceController::beginAcquisition(
    core::model::DeviceId deviceId,
    core::model::EndpointId endpointId,
    quint32 configurationRevision,
    const core::config::DeviceConfiguration &configuration,
    const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds)
{
    Q_ASSERT(QThread::currentThread() == thread());
    quint8 faceMask = 0;
    for (std::size_t index = 0; index < faceAcquisitionIds.size(); ++index) {
        if (!faceAcquisitionIds[index].isSet()) continue;
        faceMask |= quint8(1U) << index;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (faceAcquisitionIds[index] == faceAcquisitionIds[previous]) {
                m_lastError = QStringLiteral("逐阵面采集会话句柄重复");
                return false;
            }
        }
    }
    if (!m_preparedTaskId.isSet() || !deviceId.isSet() || !endpointId.isSet()
        || configurationRevision == 0U || faceMask == 0U
        || faceMask != m_deviceFaceMasks.value(deviceId, 0U)
        || core::protocol::DcmpProtocolAdapter::encodeControl(
            m_controlSettings, configuration, faceMask, true).isEmpty()) {
        m_lastError = QStringLiteral("采集会话与当前准备任务不一致");
        return false;
    }
    auto sample = *m_state.latest()->sample;
    core::model::DeviceRuntimeState runtime;
    runtime.faceAcquisitionIds = faceAcquisitionIds;
    runtime.deviceId = deviceId;
    runtime.primaryEndpointId = endpointId;
    runtime.configurationRevision = configurationRevision;
    runtime.validity = core::model::deviceStateFieldBit(
        core::model::DeviceStateFieldFlag::AcquisitionValid);
    runtime.connectionState = sample.value.dpHeartbeat == 1U || sample.value.spHeartbeat == 1U
        ? core::model::DeviceConnectionState::Connected
        : core::model::DeviceConnectionState::Disconnected;
    runtime.operatingState = core::model::DeviceOperatingState::Collecting;
    sample.value.devices = {runtime};
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!publishDeviceState(std::move(sample), QDateTime::currentMSecsSinceEpoch(), steadyNs)) {
        m_lastError = QStringLiteral("设备采集状态未通过运行时门禁");
        return false;
    }
    m_lastError.clear();
    publishEvent(core::model::EventCode::ControlAcquisitionChanged,
                 core::model::EventSeverity::Info, 1, 0);
    if (!sendWorkMode(true)) qWarning().noquote() << m_lastError;
    return true;
}

/**
 * @brief 从唯一DeviceState读取活动会话，不在Controller中维护第二份采集上下文。
 */
bool DeviceController::activeAcquisition(
    core::model::DeviceId &deviceId,
    core::model::EndpointId &endpointId,
    core::model::TaskId &taskId,
    std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds) const
{
    deviceId = {};
    endpointId = {};
    taskId = {};
    faceAcquisitionIds = {};
    const auto record = m_state.latest();
    if (!record || record->sample->value.devices.size() != 1) return false;
    const auto &runtime = record->sample->value.devices.front();
    if (runtime.operatingState != core::model::DeviceOperatingState::Collecting
        || !core::model::hasDeviceStateField(
            runtime.validity, core::model::DeviceStateFieldFlag::AcquisitionValid)) {
        return false;
    }
    deviceId = runtime.deviceId;
    endpointId = runtime.primaryEndpointId;
    taskId = m_preparedTaskId;
    faceAcquisitionIds = runtime.faceAcquisitionIds;
    return taskId.isSet();
}

/**
 * @brief 清除本地采集会话并发送一次待机命令；任务快照和 0x0001 周期发送保持独立。
 * @details 待机包不依赖心跳，失败不恢复已结束的本地采集。
 */
bool DeviceController::endAcquisition()
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto sample = *m_state.latest()->sample;
    if (sample.value.devices.size() != 1
        || sample.value.devices.front().operatingState
            != core::model::DeviceOperatingState::Collecting) {
        m_lastError = QStringLiteral("当前没有正在进行的本地采集");
        return false;
    }
    auto &runtime = sample.value.devices.front();
    runtime.faceAcquisitionIds = {};
    runtime.validity = 0;
    runtime.operatingState = core::model::DeviceOperatingState::Idle;
    const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!publishDeviceState(std::move(sample), QDateTime::currentMSecsSinceEpoch(), steadyNs)) {
        m_lastError = QStringLiteral("设备停止状态未通过运行时门禁");
        return false;
    }
    m_lastError.clear();
    publishEvent(core::model::EventCode::ControlAcquisitionChanged,
                 core::model::EventSeverity::Info, 0, 0);
    if (!sendWorkMode(false)) qWarning().noquote() << m_lastError;
    return true;
}

/**
 * @brief 每次发送都从最新语义参数构造临时协议包；失败只记录诊断，不改变本地采集状态。
 */
bool DeviceController::sendControl()
{
    if (!m_controlSender || !m_controlDeviceId.isSet()) {
        m_lastError = QStringLiteral("控制发送入口或任务设备未配置");
        return false;
    }
    const QByteArray datagram = core::protocol::DcmpProtocolAdapter::encodeControl(
        m_controlSettings, m_controlConfiguration,
        m_deviceFaceMasks.value(m_controlDeviceId, 0U), true);
    if (datagram.isEmpty()
        || !m_controlSender(datagram, QHostAddress(QHostAddress::LocalHost), 2345U)) {
        m_lastError = QStringLiteral("0x0001控制数据报本地提交失败");
        if (!m_controlSendFailed) {
            publishEvent(core::model::EventCode::ControlUdpSubmissionFailed,
                         core::model::EventSeverity::Error, 1, 0);
            m_controlSendFailed = true;
        }
        return false;
    }
    if (m_controlSendFailed) {
        publishEvent(core::model::EventCode::ControlUdpSubmissionRecovered,
                     core::model::EventSeverity::Info, 1, 0);
        m_controlSendFailed = false;
    }
    return true;
}

/**
 * @brief 本地采集状态切换成功后向 DP 固定地址发送一次工作或待机报文，不受心跳门控。
 * @details UDP 只确认本机提交；失败记录诊断，但不反向撤销已发生的本地会话与 PPI 状态。
 */
bool DeviceController::sendWorkMode(bool work)
{
    const QHostAddress target(QHostAddress::LocalHost);
    if (!m_controlSender
        || !m_controlSender(core::protocol::DcmpProtocolAdapter::encodeWorkMode(work), target, 1234U)) {
        m_lastError = QStringLiteral("0xFFFF工作/待机报文提交到127.0.0.1:1234失败");
        publishEvent(core::model::EventCode::ControlUdpSubmissionFailed,
                     core::model::EventSeverity::Error, 0xFFFF, work ? 1 : 0);
        return false;
    }
    return true;
}

/**
 * @brief 任务快照准备好且两路心跳在线时保留周期唤醒；采集按钮不参与门控。
 */
void DeviceController::refreshControlTimer()
{
    const auto record = m_state.latest();
    const bool shouldRun = m_preparedTaskId.isSet() && m_controlDeviceId.isSet() && record
        && record->sample->value.dpHeartbeat == 1U
        && record->sample->value.spHeartbeat == 1U;
    if (shouldRun && !m_controlTimer.isActive()) {
        m_controlTimer.start(50);
    } else if (!shouldRun) {
        m_controlTimer.stop();
    }
}

/**
 * @brief 只为已发生的低频状态变化或发送失败补齐统一元数据，发布拒绝不影响主链。
 * @details 事件编码与展示由 SystemEvent 门禁和 WebReader 负责；本 owner 不保留日志文本或第二份历史。
 */
void DeviceController::publishEvent(core::model::EventCode code,
                                    core::model::EventSeverity severity,
                                    qint64 argument0, qint64 argument1)
{
    if (!m_eventPublisher) return;
    core::model::Sample<core::model::SystemEvent> sample;
    sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    sample.meta.receiveTimeSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    sample.meta.traceId = m_nextEventTraceId++;
    if (m_nextEventTraceId == 0) m_nextEventTraceId = 1;
    sample.value.eventCode = code;
    sample.value.domain = core::model::eventDomainOf(code);
    sample.value.severity = severity;
    sample.value.argument0 = argument0;
    sample.value.argument1 = argument1;
    sample.value.flags = core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument0Valid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument1Valid);
    if (m_preparedTaskId.isSet()) {
        sample.value.taskId = m_preparedTaskId;
        sample.value.flags |= core::model::eventSubjectBit(core::model::EventSubjectFlag::TaskIdValid);
    }
    if (m_controlDeviceId.isSet()) {
        sample.value.deviceId = m_controlDeviceId;
        sample.value.flags |= core::model::eventSubjectBit(core::model::EventSubjectFlag::DeviceIdValid);
    }
    try {
        m_eventPublisher->publish(std::move(sample));
    } catch (const std::overflow_error &) {
        qWarning() << "系统事件序号耗尽，活动日志无法继续记录";
    }
}

/**
 * @brief 返回最近失败诊断；成功准备或清除后为空。
 */
QString DeviceController::lastError() const
{
    return m_lastError;
}

/**
 * @brief 以聚合状态门禁保护唯一写入口；非法完整快照不会推进revision。
 */
bool DeviceController::publishDeviceState(
    core::model::Sample<core::model::DeviceState> sample,
    qint64 utcMs,
    qint64 steadyNs)
{
    sample.meta.receiveTimeUtcMs = utcMs;
    sample.meta.receiveTimeSteadyNs = steadyNs;
    if (core::producer::RuntimeStateGate::validateDeviceState(sample, 4)
        == core::producer::RuntimeStateRejection::None) {
        m_state.set(std::move(sample));
        return true;
    }
    return false;
}

} // namespace core::control
