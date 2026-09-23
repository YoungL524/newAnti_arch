#include "core/producer/RuntimeStateGate.h"

#include <algorithm>

namespace {

/**
 * @brief 校验进程内聚合 State 共用的 SampleMeta。
 * @details State 不是某个外部端点的一条观测，故 endpoint/source 字段必须保持未绑定；UTC 与 steady 接收
 * 时间标记快照形成时刻。未知质量枚举或 validity 位会被拒绝，防止污染三个 LatestState。
 */
bool validStateMeta(const core::model::SampleMeta &meta) noexcept
{
    const auto quality = static_cast<quint8>(meta.quality);
    return meta.receiveTimeUtcMs > 0
        && meta.receiveTimeSteadyNs > 0
        && !meta.endpointId.isSet()
        && meta.sourceTimeUtcMs == 0
        && meta.sourceSequence == 0
        && meta.validity == 0
        && quality <= static_cast<quint8>(core::model::SampleQuality::Suspect);
}

/**
 * @brief 判断连接状态枚举是否处于当前合同登记范围。
 */
bool validConnectionState(core::model::DeviceConnectionState state) noexcept
{
    return static_cast<quint8>(state)
        <= static_cast<quint8>(core::model::DeviceConnectionState::Faulted);
}

/**
 * @brief 判断设备工作状态枚举是否处于当前合同登记范围。
 */
bool validOperatingState(core::model::DeviceOperatingState state) noexcept
{
    return static_cast<quint8>(state)
        <= static_cast<quint8>(core::model::DeviceOperatingState::Faulted);
}

/**
 * @brief 校验一台设备的可选字段和状态组合。
 * @details 该函数不查询注册表；非零句柄的存在性仍由 DeviceController/DeviceConfigService 在形成快照前保证。
 */
core::producer::RuntimeStateRejection validateDeviceEntry(
    const core::model::DeviceRuntimeState &device) noexcept
{
    using namespace core::model;
    using Rejection = core::producer::RuntimeStateRejection;
    constexpr DeviceStateFieldFlags knownValidity =
        deviceStateFieldBit(DeviceStateFieldFlag::AcquisitionValid);
    constexpr DeviceFaultFlags knownFaults =
        static_cast<DeviceFaultFlags>(DeviceFaultFlag::CommunicationUnavailable)
        | static_cast<DeviceFaultFlags>(DeviceFaultFlag::DeviceReportedFault)
        | static_cast<DeviceFaultFlags>(DeviceFaultFlag::ControlRecoveryRequired)
        | static_cast<DeviceFaultFlags>(DeviceFaultFlag::ConfigurationRejected);

    if (!device.deviceId.isSet()) {
        return Rejection::UnboundDeviceId;
    }
    if (!device.primaryEndpointId.isSet()) {
        return Rejection::UnboundEndpointId;
    }
    if (device.configurationRevision == 0) {
        return Rejection::InvalidConfigurationRevision;
    }
    if (!validConnectionState(device.connectionState)
        || !validOperatingState(device.operatingState)) {
        return Rejection::InvalidDeviceEnum;
    }
    if ((device.validity & ~knownValidity) != 0
        || (device.currentFaultFlags & ~knownFaults) != 0) {
        return Rejection::InvalidDeviceFlags;
    }

    const bool hasAcquisitionFlag = hasDeviceStateField(
        device.validity, DeviceStateFieldFlag::AcquisitionValid);
    const bool hasAcquisition = std::any_of(
        device.faceAcquisitionIds.cbegin(), device.faceAcquisitionIds.cend(),
        [](core::model::AcquisitionId id) { return id.isSet(); });
    if (hasAcquisitionFlag != hasAcquisition) {
        return Rejection::InvalidDeviceOptionalField;
    }
    for (std::size_t left = 0; left < device.faceAcquisitionIds.size(); ++left) {
        for (std::size_t right = left + 1; right < device.faceAcquisitionIds.size(); ++right) {
            if (device.faceAcquisitionIds[left].isSet()
                && device.faceAcquisitionIds[left] == device.faceAcquisitionIds[right]) {
                return Rejection::InvalidDeviceOptionalField;
            }
        }
    }

    if ((device.operatingState == DeviceOperatingState::Collecting && !hasAcquisition)
        || (device.operatingState == DeviceOperatingState::Idle && hasAcquisition)
        || ((device.connectionState == DeviceConnectionState::Faulted
             || device.operatingState == DeviceOperatingState::Faulted)
            && device.currentFaultFlags == 0)
        || (device.currentFaultFlags != 0
            && device.connectionState != DeviceConnectionState::Degraded
            && device.connectionState != DeviceConnectionState::Faulted
            && device.operatingState != DeviceOperatingState::Faulted)) {
        return Rejection::InvalidDeviceStateCombination;
    }
    return Rejection::None;
}

} // namespace

namespace core::producer {

/**
 * @brief 先校验公共元数据和容量，再按 DeviceId 顺序验证每个设备。
 * @details 严格升序同时保证唯一性和确定性输出；函数只遍历调用方已有 QVector，不复制或排序快照。
 */
RuntimeStateRejection RuntimeStateGate::validateDeviceState(
    const core::model::Sample<core::model::DeviceState> &sample,
    qsizetype maxRuntimeDevices) noexcept
{
    if (!validStateMeta(sample.meta)) {
        return RuntimeStateRejection::InvalidMeta;
    }
    if (maxRuntimeDevices < 0 || sample.value.devices.size() > maxRuntimeDevices) {
        return RuntimeStateRejection::InvalidCapacity;
    }

    core::model::DeviceId previousId;
    if (sample.value.dpHeartbeat > 2 || sample.value.spHeartbeat > 2
        || (sample.value.fpOnlineMask & 0xFFF0U) || (sample.value.fpDataMask & 0xFFF0U)
        || (sample.value.dpHeartbeat == 0 && sample.value.fpOnlineMask != 0)
        || (sample.value.spHeartbeat == 0 && sample.value.fpDataMask != 0)) {
        return RuntimeStateRejection::InvalidDeviceStateCombination;
    }
    for (const auto &device : sample.value.devices) {
        const RuntimeStateRejection entryResult = validateDeviceEntry(device);
        if (entryResult != RuntimeStateRejection::None) {
            return entryResult;
        }
        if (previousId.isSet() && !(previousId < device.deviceId)) {
            return RuntimeStateRejection::DuplicateOrUnsortedDeviceId;
        }
        previousId = device.deviceId;
    }
    return RuntimeStateRejection::None;
}

/**
 * @brief 校验 TaskState 只表达“无活动任务”或一个非零活动任务。
 * @details 任务是否存在于 TaskService 注册域由唯一 Owner 在调用门禁前保证，本函数只锁定可独立验证的值合同。
 */
RuntimeStateRejection RuntimeStateGate::validateTaskState(
    const core::model::Sample<core::model::TaskState> &sample) noexcept
{
    if (!validStateMeta(sample.meta)) {
        return RuntimeStateRejection::InvalidMeta;
    }
    const auto phaseValue = static_cast<quint8>(sample.value.phase);
    if (phaseValue > static_cast<quint8>(core::model::TaskPhase::Faulted)) {
        return RuntimeStateRejection::InvalidTaskEnum;
    }
    const bool noTask = sample.value.phase == core::model::TaskPhase::None;
    if (noTask == sample.value.taskId.isSet()) {
        return RuntimeStateRejection::InvalidTaskIdentity;
    }
    return RuntimeStateRejection::None;
}

/**
 * @brief 校验系统状态登记位、启动时间以及生命周期与健康度的一致性。
 * @details Ready 要求六项当前强制能力全部就绪；Stopping/Stopped 不再宣告 Ready。Healthy 必须无降级位，
 * Degraded 必须有原因，Faulted 健康度必须与 Faulted 生命周期一致。
 */
RuntimeStateRejection RuntimeStateGate::validateSystemState(
    const core::model::Sample<core::model::SystemState> &sample) noexcept
{
    using namespace core::model;
    if (!validStateMeta(sample.meta)) {
        return RuntimeStateRejection::InvalidMeta;
    }
    const auto phaseValue = static_cast<quint8>(sample.value.phase);
    const auto healthValue = static_cast<quint8>(sample.value.health);
    if (phaseValue > static_cast<quint8>(ApplicationPhase::Faulted)
        || healthValue > static_cast<quint8>(SystemHealth::Faulted)) {
        return RuntimeStateRejection::InvalidSystemEnum;
    }

    constexpr SystemReadinessFlags knownReadiness =
        static_cast<SystemReadinessFlags>(SystemReadinessFlag::DatabaseReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::ConfigurationLoaded)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::DataDomainReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::ArchiveReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::WebReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::ControlPlaneReady);
    constexpr SystemDegradationFlags knownDegradation =
        static_cast<SystemDegradationFlags>(SystemDegradationFlag::ArchiveUnavailable)
        | static_cast<SystemDegradationFlags>(SystemDegradationFlag::ArchiveGapDetected)
        | static_cast<SystemDegradationFlags>(SystemDegradationFlag::EventGapDetected)
        | static_cast<SystemDegradationFlags>(SystemDegradationFlag::StorageSpaceLow)
        | static_cast<SystemDegradationFlags>(SystemDegradationFlag::WebUnavailable)
        | static_cast<SystemDegradationFlags>(SystemDegradationFlag::ResourcePressure);
    if ((sample.value.readinessFlags & ~knownReadiness) != 0
        || (sample.value.degradationFlags & ~knownDegradation) != 0) {
        return RuntimeStateRejection::InvalidSystemFlags;
    }

    const bool healthyConsistent =
        (sample.value.health == SystemHealth::Healthy && sample.value.degradationFlags == 0)
        || (sample.value.health == SystemHealth::Degraded && sample.value.degradationFlags != 0)
        || sample.value.health == SystemHealth::Faulted;
    const bool faultConsistent =
        (sample.value.phase == ApplicationPhase::Faulted)
        == (sample.value.health == SystemHealth::Faulted);
    if (sample.value.processStartedUtcMs <= 0
        || sample.value.processStartedUtcMs > sample.meta.receiveTimeUtcMs
        || !healthyConsistent
        || !faultConsistent
        || (sample.value.phase == ApplicationPhase::Ready
            && sample.value.readinessFlags != knownReadiness)
        || ((sample.value.phase == ApplicationPhase::Stopping
             || sample.value.phase == ApplicationPhase::Stopped)
            && sample.value.readinessFlags != 0)) {
        return RuntimeStateRejection::InvalidSystemCombination;
    }
    return RuntimeStateRejection::None;
}

} // namespace core::producer
