#include "RadarWorkModeController.h"
#include "IWorkModeCommandSender.h"

#include <QDebug>
#include <QMetaType>

namespace {
/**
 * @brief 静态元类型注册器
 * @details Qt 元对象系统不认识 C 标准类型名 uint16_t / uint32_t；
 *          QSignalSpy、Qt::QueuedConnection 等机制需要按"参数类型名字符串"
 *          反查 metatype，所以必须显式注册一次。
 *
 *          这里使用文件作用域静态对象在程序启动时完成注册，
 *          上层调用方（包括测试）无需关心。
 *
 *          State / RejectReason 已通过 Q_ENUM 自动注册，无需重复处理。
 */
struct WorkModeMetaTypeRegistrar
{
    WorkModeMetaTypeRegistrar()
    {
        qRegisterMetaType<uint16_t>("uint16_t");
        qRegisterMetaType<uint32_t>("uint32_t");
    }
};
static const WorkModeMetaTypeRegistrar s_workModeMetaTypeRegistrar;
} // namespace

RadarWorkModeController::RadarWorkModeController(uint16_t deviceId,
                                                 IWorkModeCommandSender* sender,
                                                 QObject* parent)
    : QObject(parent)
    , m_deviceId(deviceId)
    , m_sender(sender)
{
    m_transitionTimer.setSingleShot(true);
    m_transitionTimer.setInterval(m_timeoutMs);
    connect(&m_transitionTimer, &QTimer::timeout,
            this, &RadarWorkModeController::onTransitionTimeout);
}

RadarWorkModeController::~RadarWorkModeController() = default;

void RadarWorkModeController::setTransitionTimeoutMs(int ms)
{
    if (ms < 0) {
        qWarning().noquote() << QStringLiteral(
            "[RadarWorkMode] device=%1 忽略非法超时值 %2 ms").arg(m_deviceId).arg(ms);
        return;
    }
    m_timeoutMs = ms;
    m_transitionTimer.setInterval(ms);
}

void RadarWorkModeController::requestWork()
{
    switch (m_state) {
    case State::Standby:
        if (issueCommand(1)) {
            transitionTo(State::Starting);
            m_transitionTimer.start();
        } else {
            transitionTo(State::Fault);
        }
        return;
    case State::Working:
        emit requestRejected(RejectReason::DuplicateRequest, m_state);
        return;
    case State::Starting:
    case State::Stopping:
        emit requestRejected(RejectReason::TransitionInProgress, m_state);
        return;
    case State::Fault:
        emit requestRejected(RejectReason::FaultRequiresReset, m_state);
        return;
    }
}

void RadarWorkModeController::requestStandby()
{
    switch (m_state) {
    case State::Working:
        if (issueCommand(0)) {
            transitionTo(State::Stopping);
            m_transitionTimer.start();
        } else {
            transitionTo(State::Fault);
        }
        return;
    case State::Standby:
        emit requestRejected(RejectReason::DuplicateRequest, m_state);
        return;
    case State::Starting:
    case State::Stopping:
        emit requestRejected(RejectReason::TransitionInProgress, m_state);
        return;
    case State::Fault:
        emit requestRejected(RejectReason::FaultRequiresReset, m_state);
        return;
    }
}

void RadarWorkModeController::reset()
{
    if (m_state == State::Fault) {
        m_transitionTimer.stop();
        transitionTo(State::Standby);
        return;
    }
    emit requestRejected(RejectReason::ResetIgnoredOutsideFault, m_state);
}

void RadarWorkModeController::onTransitionTimeout()
{
    // 仅在过渡态生效；若状态已被外部改变（例如同步进入 Fault），不再触发
    if (m_state == State::Starting) {
        transitionTo(State::Working);
    } else if (m_state == State::Stopping) {
        transitionTo(State::Standby);
    }
}

void RadarWorkModeController::transitionTo(State newState)
{
    if (newState == m_state) {
        return;
    }
    const State old = m_state;
    m_state = newState;
    qInfo().noquote() << QStringLiteral("[RadarWorkMode] device=%1 %2 -> %3")
                             .arg(m_deviceId)
                             .arg(stateName(old), stateName(newState));
    emit stateChanged(old, newState);
}

bool RadarWorkModeController::issueCommand(uint32_t mode)
{
    if (!m_sender) {
        qWarning().noquote() << QStringLiteral(
            "[RadarWorkMode] device=%1 sender 为空，命令无法下发").arg(m_deviceId);
        return false;
    }
    const bool ok = m_sender->sendWorkMode(m_deviceId, mode);
    if (ok) {
        emit workModeCommandIssued(m_deviceId, mode);
        qInfo().noquote() << QStringLiteral(
            "[RadarWorkMode] device=%1 已下发 workmode=%2").arg(m_deviceId).arg(mode);
    } else {
        qWarning().noquote() << QStringLiteral(
            "[RadarWorkMode] device=%1 下发 workmode=%2 失败").arg(m_deviceId).arg(mode);
    }
    return ok;
}

QString RadarWorkModeController::stateName(State s)
{
    switch (s) {
    case State::Standby:  return QStringLiteral("Standby");
    case State::Starting: return QStringLiteral("Starting");
    case State::Working:  return QStringLiteral("Working");
    case State::Stopping: return QStringLiteral("Stopping");
    case State::Fault:    return QStringLiteral("Fault");
    }
    return QStringLiteral("Unknown");
}

QString RadarWorkModeController::rejectReasonName(RejectReason r)
{
    switch (r) {
    case RejectReason::DuplicateRequest:         return QStringLiteral("DuplicateRequest");
    case RejectReason::TransitionInProgress:     return QStringLiteral("TransitionInProgress");
    case RejectReason::FaultRequiresReset:       return QStringLiteral("FaultRequiresReset");
    case RejectReason::ResetIgnoredOutsideFault: return QStringLiteral("ResetIgnoredOutsideFault");
    }
    return QStringLiteral("Unknown");
}
