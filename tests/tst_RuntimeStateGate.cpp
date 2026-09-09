/**
 * @file tst_RuntimeStateGate.cpp
 * @brief 验收三个完整运行状态的字段布局、容量边界和发布前门禁。
 * @details 测试锁定 DeviceRuntimeState/TaskState/SystemState 的尺寸、对齐与关键偏移，并验证多设备完整快照、
 * 单活动任务和系统健康组合不变量。它不创建 LatestState Owner，防止在类型合同阶段提前实现控制平面。
 */

#include "core/producer/RuntimeStateGate.h"

#include <QtTest>

#include <cstddef>
#include <type_traits>

namespace {

/**
 * @brief 构造三个 State 共用的合法进程内聚合元数据。
 * @return 不绑定外部端点、不声明厂家时间，但具有有效快照形成时间的 SampleMeta。
 */
core::model::SampleMeta validStateMeta()
{
    core::model::SampleMeta meta;
    meta.receiveTimeUtcMs = 1'777'500'000'010;
    meta.receiveTimeSteadyNs = 50'000;
    meta.traceId = 71;
    return meta;
}

/**
 * @brief 构造通信正常且空闲的设备运行状态。
 * @param deviceId 用于生成严格升序的设备快照。
 * @return 无会话、无待 ACK 请求且配置 revision 有效的设备值。
 */
core::model::DeviceRuntimeState idleDevice(quint32 deviceId)
{
    core::model::DeviceRuntimeState device;
    device.deviceId = core::model::DeviceId::fromRaw(deviceId);
    device.primaryEndpointId = core::model::EndpointId::fromRaw(deviceId + 100);
    device.configurationRevision = 2;
    device.connectionState = core::model::DeviceConnectionState::Connected;
    device.operatingState = core::model::DeviceOperatingState::Idle;
    return device;
}

/**
 * @brief 构造两台设备的合法完整运行快照。
 * @return 第一台空闲、第二台携带已确认采集会话，且按 DeviceId 升序排列。
 */
core::model::Sample<core::model::DeviceState> validDeviceSample()
{
    core::model::Sample<core::model::DeviceState> sample;
    sample.meta = validStateMeta();
    sample.value.devices.append(idleDevice(1));
    auto collecting = idleDevice(2);
    collecting.acquisitionId = core::model::AcquisitionId::fromRaw(9);
    collecting.validity = core::model::deviceStateFieldBit(
        core::model::DeviceStateFieldFlag::AcquisitionValid);
    collecting.operatingState = core::model::DeviceOperatingState::Collecting;
    sample.value.devices.append(collecting);
    return sample;
}

/**
 * @brief 构造合法的唯一活动任务状态。
 * @return TaskId 非零且处于 Running 阶段的完整 Sample。
 */
core::model::Sample<core::model::TaskState> validTaskSample()
{
    core::model::Sample<core::model::TaskState> sample;
    sample.meta = validStateMeta();
    sample.value.taskId = core::model::TaskId::fromRaw(7);
    sample.value.phase = core::model::TaskPhase::Running;
    return sample;
}

/**
 * @brief 构造所有当前强制服务均已就绪的健康系统状态。
 * @return processStartedUtcMs 早于快照形成时间、Ready 位完整且无降级的 Sample。
 */
core::model::Sample<core::model::SystemState> validSystemSample()
{
    using namespace core::model;
    core::model::Sample<SystemState> sample;
    sample.meta = validStateMeta();
    sample.value.processStartedUtcMs = sample.meta.receiveTimeUtcMs - 1'000;
    sample.value.readinessFlags =
        static_cast<SystemReadinessFlags>(SystemReadinessFlag::DatabaseReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::ConfigurationLoaded)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::DataDomainReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::ArchiveReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::WebReady)
        | static_cast<SystemReadinessFlags>(SystemReadinessFlag::ControlPlaneReady);
    sample.value.phase = ApplicationPhase::Ready;
    sample.value.health = SystemHealth::Healthy;
    return sample;
}

} // namespace

static_assert(std::is_standard_layout<core::model::DeviceRuntimeState>::value,
              "DeviceRuntimeState must preserve inspectable field offsets");
static_assert(std::is_trivially_copyable<core::model::DeviceRuntimeState>::value,
              "DeviceRuntimeState elements must remain cheap value copies");
static_assert(std::is_standard_layout<core::model::TaskState>::value,
              "TaskState must preserve inspectable field offsets");
static_assert(std::is_trivially_copyable<core::model::TaskState>::value,
              "TaskState must remain a compact value");
static_assert(std::is_standard_layout<core::model::SystemState>::value,
              "SystemState must preserve inspectable field offsets");
static_assert(std::is_trivially_copyable<core::model::SystemState>::value,
              "SystemState must remain a compact value");

/** @brief 汇总运行状态布局及三个门禁的回归验收。 */
class RuntimeStateGateTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 锁定定长状态元素的尺寸、对齐和关键字段偏移。 */
    void preservesCompactLayouts();

    /** @brief 验收合法多设备快照，并拒绝容量、排序、标志和状态组合错误。 */
    void validatesCompleteDeviceSnapshot();

    /** @brief 验收 None/非 None 阶段与唯一 TaskId 的严格对应。 */
    void validatesSingleActiveTask();

    /** @brief 验收系统生命周期、就绪位、降级位和总体健康的一致性。 */
    void validatesSystemHealthSnapshot();

    /** @brief 验收三类聚合 State 共同拒绝外部来源字段和无效时间。 */
    void rejectsInvalidAggregateMeta();
};

/**
 * @brief 防止字段重排、类型扩张或隐式动态成员改变冻结的低频状态布局。
 * @details DeviceState 自身有意持有有界 QVector，因此只锁定其定长元素；TaskState 和 SystemState 直接锁定。
 */
void RuntimeStateGateTest::preservesCompactLayouts()
{
    QCOMPARE(sizeof(core::model::DeviceRuntimeState), std::size_t(40));
    QCOMPARE(alignof(core::model::DeviceRuntimeState), std::size_t(8));
    QCOMPARE(offsetof(core::model::DeviceRuntimeState, acquisitionId), std::size_t(0));
    QCOMPARE(offsetof(core::model::DeviceRuntimeState, activeRequestId), std::size_t(8));
    QCOMPARE(offsetof(core::model::DeviceRuntimeState, deviceId), std::size_t(16));
    QCOMPARE(offsetof(core::model::DeviceRuntimeState, configurationRevision), std::size_t(24));
    QCOMPARE(offsetof(core::model::DeviceRuntimeState, validity), std::size_t(32));

    QCOMPARE(sizeof(core::model::TaskState), std::size_t(8));
    QCOMPARE(alignof(core::model::TaskState), std::size_t(4));
    QCOMPARE(offsetof(core::model::TaskState, taskId), std::size_t(0));
    QCOMPARE(offsetof(core::model::TaskState, phase), std::size_t(4));

    QCOMPARE(sizeof(core::model::SystemState), std::size_t(24));
    QCOMPARE(alignof(core::model::SystemState), std::size_t(8));
    QCOMPARE(offsetof(core::model::SystemState, processStartedUtcMs), std::size_t(0));
    QCOMPARE(offsetof(core::model::SystemState, readinessFlags), std::size_t(8));
    QCOMPARE(offsetof(core::model::SystemState, degradationFlags), std::size_t(12));
    QCOMPARE(offsetof(core::model::SystemState, phase), std::size_t(16));
}

/**
 * @brief 证明一个 LatestState 值可以完整承载多设备，同时不接受截断或矛盾状态。
 * @details 门禁不对 QVector 排序，强制唯一 Owner 在 set 前形成确定性完整快照。
 */
void RuntimeStateGateTest::validatesCompleteDeviceSnapshot()
{
    using Rejection = core::producer::RuntimeStateRejection;
    auto sample = validDeviceSample();
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, 4), Rejection::None);
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, 1), Rejection::InvalidCapacity);
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, -1), Rejection::InvalidCapacity);

    sample = validDeviceSample();
    std::swap(sample.value.devices[0], sample.value.devices[1]);
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, 4),
             Rejection::DuplicateOrUnsortedDeviceId);

    sample = validDeviceSample();
    sample.value.devices[1].validity = 0;
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, 4),
             Rejection::InvalidDeviceOptionalField);

    sample = validDeviceSample();
    sample.value.devices[0].operatingState = core::model::DeviceOperatingState::Starting;
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, 4),
             Rejection::InvalidDeviceStateCombination);

    sample = validDeviceSample();
    sample.value.devices[0].currentFaultFlags = 0x80000000U;
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(sample, 4),
             Rejection::InvalidDeviceFlags);
}

/**
 * @brief 验证 TaskState 不复制设备信息，并以 TaskId/phase 二元组表达唯一活动任务。
 */
void RuntimeStateGateTest::validatesSingleActiveTask()
{
    using Rejection = core::producer::RuntimeStateRejection;
    auto sample = validTaskSample();
    QCOMPARE(core::producer::RuntimeStateGate::validateTaskState(sample), Rejection::None);

    sample.value.taskId = {};
    QCOMPARE(core::producer::RuntimeStateGate::validateTaskState(sample), Rejection::InvalidTaskIdentity);

    sample.value.phase = core::model::TaskPhase::None;
    QCOMPARE(core::producer::RuntimeStateGate::validateTaskState(sample), Rejection::None);

    sample.value.taskId = core::model::TaskId::fromRaw(1);
    QCOMPARE(core::producer::RuntimeStateGate::validateTaskState(sample), Rejection::InvalidTaskIdentity);
}

/**
 * @brief 验证 Ready 必须完整就绪，Healthy/Degraded 必须与降级位保持一致。
 */
void RuntimeStateGateTest::validatesSystemHealthSnapshot()
{
    using Rejection = core::producer::RuntimeStateRejection;
    auto sample = validSystemSample();
    QCOMPARE(core::producer::RuntimeStateGate::validateSystemState(sample), Rejection::None);

    sample.value.readinessFlags &= ~static_cast<core::model::SystemReadinessFlags>(
        core::model::SystemReadinessFlag::WebReady);
    QCOMPARE(core::producer::RuntimeStateGate::validateSystemState(sample),
             Rejection::InvalidSystemCombination);

    sample = validSystemSample();
    sample.value.health = core::model::SystemHealth::Degraded;
    QCOMPARE(core::producer::RuntimeStateGate::validateSystemState(sample),
             Rejection::InvalidSystemCombination);
    sample.value.degradationFlags = static_cast<core::model::SystemDegradationFlags>(
        core::model::SystemDegradationFlag::ArchiveUnavailable);
    QCOMPARE(core::producer::RuntimeStateGate::validateSystemState(sample), Rejection::None);

    sample = validSystemSample();
    sample.value.processStartedUtcMs = sample.meta.receiveTimeUtcMs + 1;
    QCOMPARE(core::producer::RuntimeStateGate::validateSystemState(sample),
             Rejection::InvalidSystemCombination);
}

/**
 * @brief 逐类证明聚合 State 不冒充来自某个设备端点的外部观测。
 */
void RuntimeStateGateTest::rejectsInvalidAggregateMeta()
{
    using Rejection = core::producer::RuntimeStateRejection;
    auto device = validDeviceSample();
    device.meta.endpointId = core::model::EndpointId::fromRaw(1);
    QCOMPARE(core::producer::RuntimeStateGate::validateDeviceState(device, 4), Rejection::InvalidMeta);

    auto task = validTaskSample();
    task.meta.receiveTimeSteadyNs = 0;
    QCOMPARE(core::producer::RuntimeStateGate::validateTaskState(task), Rejection::InvalidMeta);

    auto system = validSystemSample();
    system.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence);
    QCOMPARE(core::producer::RuntimeStateGate::validateSystemState(system), Rejection::InvalidMeta);
}

/**
 * @brief 创建无 GUI 的 QtTest 入口。
 * @details 本测试仅执行同步值校验和布局断言，不创建线程、Topic 或网络资源。
 */
QTEST_APPLESS_MAIN(RuntimeStateGateTest)

#include "tst_RuntimeStateGate.moc"
