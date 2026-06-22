/**
 * @file tst_RadarWorkModeController.cpp
 * @brief RadarWorkModeController 单元测试 (Qt Test)
 * @details 对应业务文档 doc/business/雷达工作状态控制.md
 *          对应测试文档 doc/test/雷达工作状态控制-单元测试.md
 *
 * 运行方式：
 *   ctest --test-dir build --output-on-failure
 *   或直接运行 build/bin/tst_RadarWorkModeController(.exe)
 */

#include <QtTest>
#include <QObject>
#include <QSignalSpy>
#include <QVector>
#include <QPair>

#include <memory>

#include "RadarWorkModeController.h"
#include "IWorkModeCommandSender.h"

namespace {

constexpr uint16_t kDevice = 1;   // DEVICE_RADAR_NH_KU_1
constexpr uint16_t kDeviceB = 2;  // DEVICE_RADAR_NH_KU_2
constexpr int kFastTimeoutMs = 5; // 测试用快速超时

/**
 * @brief 测试用 sender
 * @details 不依赖 SystemBus；可控制返回值并记录调用。
 */
class FakeWorkModeCommandSender : public IWorkModeCommandSender
{
public:
    bool nextResult = true;
    QVector<QPair<uint16_t, uint32_t>> calls;

    bool sendWorkMode(uint16_t deviceId, uint32_t mode) override
    {
        calls.append({deviceId, mode});
        return nextResult;
    }

    void reset()
    {
        nextResult = true;
        calls.clear();
    }
};

} // namespace

// 让 QSignalSpy 能识别 enum 类型参数
Q_DECLARE_METATYPE(RadarWorkModeController::State)
Q_DECLARE_METATYPE(RadarWorkModeController::RejectReason)

class TestRadarWorkModeController : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ===== 正常路径 =====
    void initial_state_is_standby();
    void standby_to_starting_on_work_request();
    void starting_to_working_on_timeout();
    void working_to_stopping_on_standby_request();
    void stopping_to_standby_on_timeout();

    // ===== 故障路径 =====
    void send_failure_in_standby_goes_to_fault();
    void send_failure_in_working_goes_to_fault();
    void fault_reset_returns_to_standby();

    // ===== 拒绝路径 =====
    void duplicate_standby_request_is_rejected();
    void duplicate_work_request_is_rejected();
    void requests_during_starting_are_rejected();
    void requests_during_stopping_are_rejected();
    void fault_rejects_business_requests();
    void reset_outside_fault_is_rejected();
    void reset_during_transition_is_rejected();

    // ===== 信号 =====
    void rejected_request_does_not_emit_state_changed();
    void work_mode_command_issued_carries_correct_args();

    // ===== 多设备隔离 =====
    void two_controllers_are_independent();

    // ===== workModeEcho 回显验证 =====
    void echo_in_starting_mode1_goes_to_working();
    void echo_in_starting_mode0_goes_to_fault();
    void echo_in_stopping_mode0_goes_to_standby();
    void echo_in_other_states_is_ignored();

private:
    /**
     * @brief 创建一个 controller 并把超时设小，便于推进过渡
     */
    static std::unique_ptr<RadarWorkModeController>
    makeController(uint16_t device, FakeWorkModeCommandSender& sender);

    /**
     * @brief 把 controller 推进到指定状态（仅供测试夹具使用）
     */
    static void driveTo(RadarWorkModeController& c,
                        FakeWorkModeCommandSender& sender,
                        RadarWorkModeController::State target);
};

void TestRadarWorkModeController::initTestCase()
{
    qRegisterMetaType<RadarWorkModeController::State>("RadarWorkModeController::State");
    qRegisterMetaType<RadarWorkModeController::RejectReason>("RadarWorkModeController::RejectReason");
    qRegisterMetaType<uint16_t>("uint16_t");
    qRegisterMetaType<uint32_t>("uint32_t");
}

std::unique_ptr<RadarWorkModeController>
TestRadarWorkModeController::makeController(uint16_t device,
                                            FakeWorkModeCommandSender& sender)
{
    auto c = std::make_unique<RadarWorkModeController>(device, &sender);
    c->setTransitionTimeoutMs(kFastTimeoutMs);
    return c;
}

void TestRadarWorkModeController::driveTo(RadarWorkModeController& c,
                                          FakeWorkModeCommandSender& sender,
                                          RadarWorkModeController::State target)
{
    using S = RadarWorkModeController::State;
    sender.nextResult = true;

    switch (target) {
    case S::Standby:
        // 默认初始状态
        return;
    case S::Starting:
        c.requestWork();
        QCOMPARE(c.state(), S::Starting);
        return;
    case S::Working:
        c.requestWork();
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), S::Working, 500);
        return;
    case S::Stopping:
        c.requestWork();
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), S::Working, 500);
        c.requestStandby();
        QCOMPARE(c.state(), S::Stopping);
        return;
    case S::Fault:
        sender.nextResult = false;
        c.requestWork();
        QCOMPARE(c.state(), S::Fault);
        sender.nextResult = true;
        return;
    }
}

// ============================================================================
// 正常路径
// ============================================================================

void TestRadarWorkModeController::initial_state_is_standby()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
    QCOMPARE(c->deviceId(), kDevice);
    QVERIFY(sender.calls.isEmpty());
}

void TestRadarWorkModeController::standby_to_starting_on_work_request()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);
    QSignalSpy issuedSpy(c.get(), &RadarWorkModeController::workModeCommandIssued);

    c->requestWork();

    QCOMPARE(c->state(), RadarWorkModeController::State::Starting);
    QCOMPARE(sender.calls.size(), 1);
    QCOMPARE(sender.calls.first().first, kDevice);
    QCOMPARE(sender.calls.first().second, 1u);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(issuedSpy.count(), 1);
}

void TestRadarWorkModeController::starting_to_working_on_timeout()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    c->requestWork();
    QCOMPARE(c->state(), RadarWorkModeController::State::Starting);

    QTRY_COMPARE_WITH_TIMEOUT(c->state(), RadarWorkModeController::State::Working, 500);
}

void TestRadarWorkModeController::working_to_stopping_on_standby_request()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Working);
    sender.calls.clear();

    QSignalSpy issuedSpy(c.get(), &RadarWorkModeController::workModeCommandIssued);

    c->requestStandby();

    QCOMPARE(c->state(), RadarWorkModeController::State::Stopping);
    QCOMPARE(sender.calls.size(), 1);
    QCOMPARE(sender.calls.first().first, kDevice);
    QCOMPARE(sender.calls.first().second, 0u);
    QCOMPARE(issuedSpy.count(), 1);
}

void TestRadarWorkModeController::stopping_to_standby_on_timeout()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Stopping);

    QTRY_COMPARE_WITH_TIMEOUT(c->state(), RadarWorkModeController::State::Standby, 500);
}

// ============================================================================
// 故障路径
// ============================================================================

void TestRadarWorkModeController::send_failure_in_standby_goes_to_fault()
{
    FakeWorkModeCommandSender sender;
    sender.nextResult = false;
    auto c = makeController(kDevice, sender);

    QSignalSpy issuedSpy(c.get(), &RadarWorkModeController::workModeCommandIssued);

    c->requestWork();

    QCOMPARE(c->state(), RadarWorkModeController::State::Fault);
    QCOMPARE(sender.calls.size(), 1); // sender 被调用了一次
    QCOMPARE(issuedSpy.count(), 0);   // 但失败时不应发出 issued 信号
}

void TestRadarWorkModeController::send_failure_in_working_goes_to_fault()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Working);

    sender.nextResult = false;
    c->requestStandby();

    QCOMPARE(c->state(), RadarWorkModeController::State::Fault);
}

void TestRadarWorkModeController::fault_reset_returns_to_standby()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Fault);

    sender.calls.clear();
    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);

    c->reset();

    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
    QCOMPARE(stateSpy.count(), 1);
    QVERIFY(sender.calls.isEmpty()); // reset 不应触发任何下行命令
}

// ============================================================================
// 拒绝路径
// ============================================================================

void TestRadarWorkModeController::duplicate_standby_request_is_rejected()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);
    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);

    c->requestStandby(); // 已经是 Standby

    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(stateSpy.count(), 0);
    QVERIFY(sender.calls.isEmpty());
}

void TestRadarWorkModeController::duplicate_work_request_is_rejected()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Working);
    sender.calls.clear();

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);

    c->requestWork(); // 已经是 Working

    QCOMPARE(c->state(), RadarWorkModeController::State::Working);
    QCOMPARE(rejectSpy.count(), 1);
    QVERIFY(sender.calls.isEmpty());
}

void TestRadarWorkModeController::requests_during_starting_are_rejected()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Starting);
    sender.calls.clear();

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);
    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);

    QVERIFY(!c->requestWork());
    QVERIFY(c->requestStandby());

    QCOMPARE(c->state(), RadarWorkModeController::State::Stopping);
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(sender.calls.size(), 1);
    QCOMPARE(sender.calls.last().second, 0u);
    QCOMPARE(stateSpy.count(), 1);
}

void TestRadarWorkModeController::requests_during_stopping_are_rejected()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Stopping);
    sender.calls.clear();

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);

    c->requestWork();
    c->requestStandby();

    QCOMPARE(c->state(), RadarWorkModeController::State::Stopping);
    QCOMPARE(rejectSpy.count(), 2);
    QVERIFY(sender.calls.isEmpty());
}

void TestRadarWorkModeController::fault_rejects_business_requests()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Fault);
    sender.calls.clear();

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);

    c->requestWork();
    c->requestStandby();

    QCOMPARE(c->state(), RadarWorkModeController::State::Fault);
    QCOMPARE(rejectSpy.count(), 2);
    QVERIFY(sender.calls.isEmpty());
}

void TestRadarWorkModeController::reset_outside_fault_is_rejected()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);
    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);

    c->reset(); // 当前 Standby

    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(stateSpy.count(), 0);

    // 推到 Working 再 reset
    driveTo(*c, sender, RadarWorkModeController::State::Working);
    rejectSpy.clear();
    stateSpy.clear();

    c->reset();

    QCOMPARE(c->state(), RadarWorkModeController::State::Working);
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(stateSpy.count(), 0);
}

void TestRadarWorkModeController::reset_during_transition_is_rejected()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Starting);

    QSignalSpy rejectSpy(c.get(), &RadarWorkModeController::requestRejected);

    c->reset();

    QCOMPARE(c->state(), RadarWorkModeController::State::Starting);
    QCOMPARE(rejectSpy.count(), 1);
}

// ============================================================================
// 信号
// ============================================================================

void TestRadarWorkModeController::rejected_request_does_not_emit_state_changed()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);

    // 一连串非法操作
    c->requestStandby(); // 重复
    c->reset();          // 非 Fault
    c->reset();          // 非 Fault

    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
    QCOMPARE(stateSpy.count(), 0);
}

void TestRadarWorkModeController::work_mode_command_issued_carries_correct_args()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    QSignalSpy issuedSpy(c.get(), &RadarWorkModeController::workModeCommandIssued);

    c->requestWork();

    QCOMPARE(issuedSpy.count(), 1);
    const auto args = issuedSpy.takeFirst();
    QCOMPARE(args.at(0).value<uint16_t>(), kDevice);
    QCOMPARE(args.at(1).value<uint32_t>(), 1u);
}

// ============================================================================
// 多设备隔离
// ============================================================================

void TestRadarWorkModeController::two_controllers_are_independent()
{
    FakeWorkModeCommandSender senderA;
    FakeWorkModeCommandSender senderB;
    auto a = makeController(kDevice, senderA);
    auto b = makeController(kDeviceB, senderB);

    a->requestWork();
    QTRY_COMPARE_WITH_TIMEOUT(a->state(), RadarWorkModeController::State::Working, 500);

    QCOMPARE(b->state(), RadarWorkModeController::State::Standby);
    QVERIFY(senderB.calls.isEmpty());

    QCOMPARE(senderA.calls.size(), 1);
    QCOMPARE(senderA.calls.first().first, kDevice);
}

// ============================================================================
// workModeEcho 回显验证（替代超时）
// ============================================================================

void TestRadarWorkModeController::echo_in_starting_mode1_goes_to_working()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    c->requestWork();
    QCOMPARE(c->state(), RadarWorkModeController::State::Starting);

    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);
    c->onWorkModeEcho(1);

    QCOMPARE(c->state(), RadarWorkModeController::State::Working);
    QCOMPARE(stateSpy.count(), 1);
    // 超时不应再触发
    QTest::qWait(kFastTimeoutMs + 10);
    QCOMPARE(c->state(), RadarWorkModeController::State::Working);
}

void TestRadarWorkModeController::echo_in_starting_mode0_goes_to_fault()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    c->requestWork();
    QCOMPARE(c->state(), RadarWorkModeController::State::Starting);

    c->onWorkModeEcho(0);

    QCOMPARE(c->state(), RadarWorkModeController::State::Fault);
    // 超时不应再触发
    QTest::qWait(kFastTimeoutMs + 10);
    QCOMPARE(c->state(), RadarWorkModeController::State::Fault);
}

void TestRadarWorkModeController::echo_in_stopping_mode0_goes_to_standby()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);
    driveTo(*c, sender, RadarWorkModeController::State::Stopping);

    QSignalSpy stateSpy(c.get(), &RadarWorkModeController::stateChanged);
    c->onWorkModeEcho(0);

    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
    QCOMPARE(stateSpy.count(), 1);
    // 超时不应再触发
    QTest::qWait(kFastTimeoutMs + 10);
    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);
}

void TestRadarWorkModeController::echo_in_other_states_is_ignored()
{
    FakeWorkModeCommandSender sender;
    auto c = makeController(kDevice, sender);

    // Standby 忽略回显
    c->onWorkModeEcho(0);
    QCOMPARE(c->state(), RadarWorkModeController::State::Standby);

    // Working 忽略 mode=1 回显
    driveTo(*c, sender, RadarWorkModeController::State::Working);
    c->onWorkModeEcho(1);
    QCOMPARE(c->state(), RadarWorkModeController::State::Working);

    // Fault 忽略回显
    driveTo(*c, sender, RadarWorkModeController::State::Fault);
    c->onWorkModeEcho(0);
    QCOMPARE(c->state(), RadarWorkModeController::State::Fault);
}

QTEST_MAIN(TestRadarWorkModeController)
#include "tst_RadarWorkModeController.moc"
