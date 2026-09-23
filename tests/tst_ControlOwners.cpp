/**
 * @file tst_ControlOwners.cpp
 * @brief 验收最小ApplicationController与DeviceController的任务准备、所有权、50 ms发送和失败原子性。
 * @details 使用内存发送函数观察数据报，不创建真实Socket，也不等待任何设备ACK。
 */

#include "core/app/ApplicationController.h"
#include "core/control/DeviceController.h"
#include "core/datadomain/CoreDataDomain.h"
#include "core/producer/TrackIngressProducer.h"

#include <QtTest>
#include <QtEndian>

/** @brief 验证稳定身份收敛、参数副本隔离以及失败不覆盖旧准备任务。 */
class ControlOwnersTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 两项任务设备绑定必须完整转换为强类型DeviceId与原始faceMask。 */
    void preparesResolvedTask();

    /** @brief 重复或缺失设备映射必须失败，并保留此前完整准备态。 */
    void rejectsAmbiguousBindingsAtomically();

    /** @brief 只有匹配任务能够清除准备态，避免迟到请求清除新任务。 */
    void clearsOnlyMatchingTask();

    /** @brief 只有与准备态阵面完全一致的逐阵面会话才能绑定唯一航迹入口。 */
    void bindsPreparedAcquisition();
};

/**
 * @brief 构造包含两台设备和独立参数值的合法任务，不依赖SQLite测试夹具。
 */
static core::task::TaskDefinition validTask(const QUuid &first, const QUuid &second)
{
    core::task::TaskDefinition task;
    task.taskUuid = QUuid::createUuid();
    task.taskName = QStringLiteral("控制Owner测试任务");
    task.deviceBindings = {{first, 0x01U}, {second, 0x0AU}};
    task.controlSettings.common.rangeMode = 0x00U;
    task.controlSettings.faces[1].frequencyIndex = 17U;
    return task;
}

/**
 * @brief 证明ApplicationController只做稳定身份编排，DeviceController保存不可被调用方原地修改的值副本。
 */
void ControlOwnersTest::preparesResolvedTask()
{
    core::domain::CoreDataDomain domain(2, 2);
    core::control::DeviceController deviceController(domain.deviceState());
    core::producer::TrackIngressProducer trackIngress(domain.trackTopic());
    core::app::ApplicationController applicationController(deviceController, trackIngress);
    const QUuid first = QUuid::createUuid();
    const QUuid second = QUuid::createUuid();
    auto task = validTask(first, second);
    const auto taskId = core::model::TaskId::fromRaw(7U);
    const auto firstDevice = core::model::DeviceId::fromRaw(11U);
    const auto secondDevice = core::model::DeviceId::fromRaw(12U);
    const QHash<QUuid, core::model::DeviceId> runtimeIds {
        {first, firstDevice}, {second, secondDevice}
    };

    QVERIFY(applicationController.prepareTask(taskId, task, runtimeIds));
    QVERIFY(deviceController.hasPreparedTask());
    QCOMPARE(deviceController.preparedTaskId(), taskId);
    QCOMPARE(deviceController.faceMask(firstDevice), quint8(0x01U));
    QCOMPARE(deviceController.faceMask(secondDevice), quint8(0x0AU));
    QCOMPARE(deviceController.controlSettings().faces[1].frequencyIndex, quint32(17U));

    task.controlSettings.faces[1].frequencyIndex = 3U;
    QCOMPARE(deviceController.controlSettings().faces[1].frequencyIndex, quint32(17U));
}

/**
 * @brief 先建立合法准备态，再用重复稳定UUID和非法参数攻击，验收旧值不发生部分替换。
 */
void ControlOwnersTest::rejectsAmbiguousBindingsAtomically()
{
    core::domain::CoreDataDomain domain(2, 2);
    core::control::DeviceController deviceController(domain.deviceState());
    core::producer::TrackIngressProducer trackIngress(domain.trackTopic());
    core::app::ApplicationController applicationController(deviceController, trackIngress);
    const QUuid first = QUuid::createUuid();
    const QUuid second = QUuid::createUuid();
    auto task = validTask(first, second);
    const auto originalTaskId = core::model::TaskId::fromRaw(1U);
    const QHash<QUuid, core::model::DeviceId> runtimeIds {
        {first, core::model::DeviceId::fromRaw(21U)},
        {second, core::model::DeviceId::fromRaw(22U)}
    };
    QVERIFY(applicationController.prepareTask(originalTaskId, task, runtimeIds));

    task.deviceBindings[1].deviceUuid = first;
    QVERIFY(!applicationController.prepareTask(
        core::model::TaskId::fromRaw(2U), task, runtimeIds));
    QCOMPARE(deviceController.preparedTaskId(), originalTaskId);

    task = validTask(first, second);
    task.controlSettings.frame.waveformWritePosition = 0U;
    QVERIFY(!applicationController.prepareTask(
        core::model::TaskId::fromRaw(3U), task, runtimeIds));
    QCOMPARE(deviceController.preparedTaskId(), originalTaskId);
}

/**
 * @brief 证明迟到的旧任务清除请求不会破坏当前准备态，匹配请求则完整归零。
 */
void ControlOwnersTest::clearsOnlyMatchingTask()
{
    core::domain::CoreDataDomain domain(2, 2);
    core::control::DeviceController deviceController(domain.deviceState());
    core::producer::TrackIngressProducer trackIngress(domain.trackTopic());
    core::app::ApplicationController applicationController(deviceController, trackIngress);
    const QUuid deviceUuid = QUuid::createUuid();
    auto task = validTask(deviceUuid, QUuid::createUuid());
    const auto taskId = core::model::TaskId::fromRaw(9U);
    const QHash<QUuid, core::model::DeviceId> runtimeIds {
        {task.deviceBindings[0].deviceUuid, core::model::DeviceId::fromRaw(31U)},
        {task.deviceBindings[1].deviceUuid, core::model::DeviceId::fromRaw(32U)}
    };
    QVERIFY(applicationController.prepareTask(taskId, task, runtimeIds));
    QVERIFY(!applicationController.clearPreparedTask(core::model::TaskId::fromRaw(8U)));
    QVERIFY(deviceController.hasPreparedTask());
    QVERIFY(applicationController.clearPreparedTask(taskId));
    QVERIFY(!deviceController.hasPreparedTask());
    QCOMPARE(deviceController.faceMask(core::model::DeviceId::fromRaw(31U)), quint8(0U));
}

/**
 * @brief 验收本地开始拒绝错阵面，并证明DP/SP共同门控50 ms控制包且停止包写入零。
 */
void ControlOwnersTest::bindsPreparedAcquisition()
{
    core::domain::CoreDataDomain domain(2, 2);
    QList<QByteArray> datagrams;
    core::control::DeviceController deviceController(
        domain.deviceState(),
        [&datagrams](const QByteArray &bytes, const QHostAddress &address, quint16 port) {
            if (address != QHostAddress::LocalHost || port != 3456U) return false;
            datagrams.append(bytes);
            return true;
        });
    core::producer::TrackIngressProducer trackIngress(domain.trackTopic());
    core::app::ApplicationController applicationController(deviceController, trackIngress);
    const QUuid deviceUuid = QUuid::createUuid();
    core::task::TaskDefinition task;
    task.taskUuid = QUuid::createUuid();
    task.taskName = QStringLiteral("四面阵局部调试");
    task.deviceBindings = {{deviceUuid, 0x05U}};
    const auto taskId = core::model::TaskId::fromRaw(20U);
    const auto deviceId = core::model::DeviceId::fromRaw(21U);
    QVERIFY(applicationController.prepareTask(taskId, task, {{deviceUuid, deviceId}}));

    std::array<core::model::AcquisitionId, 4> acquisitions {};
    acquisitions[0] = core::model::AcquisitionId::fromRaw(100U);
    QVERIFY(!applicationController.startAcquisition(
        core::model::EndpointId::fromRaw(30U), deviceId, taskId, acquisitions));
    acquisitions[2] = core::model::AcquisitionId::fromRaw(102U);
    QVERIFY(applicationController.startAcquisition(
        core::model::EndpointId::fromRaw(30U), deviceId, taskId, acquisitions));
    core::config::DeviceConfiguration configuration;
    configuration.deviceUuid = deviceUuid;
    configuration.protocolConfigJson = QStringLiteral(
        R"({"communication":{"targets":{"dph":{"address":"127.0.0.1","port":3456}}}})");
    configuration.longitudeDeg = 118.65303;
    configuration.latitudeDeg = 31.93494;
    configuration.altitudeM = 63.9288;
    QVERIFY(deviceController.beginAcquisition(
        deviceId, core::model::EndpointId::fromRaw(30U), 1U,
        configuration, acquisitions));
    QTest::qWait(70);
    QVERIFY(datagrams.isEmpty());
    const qint64 utcMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 steadyNs = 1'000'000'000LL;
    deviceController.acceptHeartbeat(false, 1U, 0x05U, utcMs, steadyNs);
    QTest::qWait(70);
    QVERIFY(datagrams.isEmpty());
    deviceController.acceptHeartbeat(true, 1U, 0x05U, utcMs, steadyNs);
    QTRY_VERIFY_WITH_TIMEOUT(!datagrams.isEmpty(), 200);
    QCOMPARE(datagrams.back().size(), 944);
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(datagrams.back().constData() + 44 + 32)), quint32(1U));
    const qsizetype sentBeforeTimeout = datagrams.size();
    deviceController.checkHeartbeatDeadlines(utcMs + 6000, steadyNs + 6'000'000'000LL);
    QTest::qWait(70);
    QCOMPARE(datagrams.size(), sentBeforeTimeout);
    deviceController.acceptHeartbeat(false, 1U, 0x05U,
                                     utcMs + 7000, steadyNs + 7'000'000'000LL);
    deviceController.acceptHeartbeat(true, 1U, 0x05U,
                                     utcMs + 7000, steadyNs + 7'000'000'000LL);
    QTRY_VERIFY_WITH_TIMEOUT(datagrams.size() > sentBeforeTimeout, 200);
    const auto collecting = domain.deviceState().latest();
    QVERIFY(collecting.has_value());
    QCOMPARE(collecting->sample->value.devices.size(), 1);
    QCOMPARE(collecting->sample->value.devices.front().operatingState,
             core::model::DeviceOperatingState::Collecting);
    QVERIFY(deviceController.endAcquisition());
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(datagrams.back().constData() + 44 + 32)), quint32(0U));
    const auto stopped = domain.deviceState().latest();
    QCOMPARE(stopped->sample->value.devices.front().operatingState,
             core::model::DeviceOperatingState::Idle);
    QVERIFY(!core::model::hasDeviceStateField(
        stopped->sample->value.devices.front().validity,
        core::model::DeviceStateFieldFlag::AcquisitionValid));
}

/**
 * @brief 创建仅含Qt事件循环的无GUI入口，以验收50 ms定时器而不启动网络服务。
 */
QTEST_GUILESS_MAIN(ControlOwnersTest)

#include "tst_ControlOwners.moc"
