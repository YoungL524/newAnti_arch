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

/**
 * @brief 接受正毫秒超时并更新单次转换等待定时器；非正值保持原配置。
 */
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

/**
 * @brief 仅从 Standby 发出工作命令；成功下发后进入 Starting 并等待雷达回显。
 */
bool RadarWorkModeController::requestWork()
{
    switch (m_state) {
    case State::Standby:
        if (issueCommand(1)) {
            transitionTo(State::Starting);
            m_transitionTimer.start();
            return true;
        }
        transitionTo(State::Fault);
        return false;
    case State::Working:
        emit requestRejected(RejectReason::DuplicateRequest, m_state);
        return false;
    case State::Starting:
    case State::Stopping:
        emit requestRejected(RejectReason::TransitionInProgress, m_state);
        return false;
    case State::Fault:
        emit requestRejected(RejectReason::FaultRequiresReset, m_state);
        return false;
    }
    return false;
}

/**
 * @brief 从 Working 或错误恢复路径发出待机命令，进入 Stopping 等待回显。
 */
bool RadarWorkModeController::requestStandby()
{
    switch (m_state) {
    case State::Working:
        if (issueCommand(0)) {
            transitionTo(State::Stopping);
            m_transitionTimer.start();
            return true;
        }
        transitionTo(State::Fault);
        return false;
    case State::Starting:
        m_transitionTimer.stop();
        if (issueCommand(0)) {
            transitionTo(State::Stopping);
            m_transitionTimer.start();
            return true;
        }
        transitionTo(State::Fault);
        return false;
    case State::Standby:
        emit requestRejected(RejectReason::DuplicateRequest, m_state);
        return false;
    case State::Stopping:
        emit requestRejected(RejectReason::TransitionInProgress, m_state);
        return false;
    case State::Fault:
        emit requestRejected(RejectReason::FaultRequiresReset, m_state);
        return false;
    }
    return false;
}

/**
 * @brief 停止转换计时并把故障/未知状态恢复为初始 Standby，不向设备发送命令。
 */
bool RadarWorkModeController::reset()
{
    if (m_state == State::Fault) {
        m_transitionTimer.stop();
        transitionTo(State::Standby);
        return true;
    }
    emit requestRejected(RejectReason::ResetIgnoredOutsideFault, m_state);
    return false;
}

/**
 * @brief 用雷达 workStandby 回显确认 Starting/Stopping；不符合预期的回显不完成转换。
 */
void RadarWorkModeController::onWorkModeEcho(uint32_t mode)
{
    switch (m_state) {
    case State::Starting:
        m_transitionTimer.stop();
        if (mode == 1) {
            transitionTo(State::Working);
        } else {
            transitionTo(State::Fault);
            qWarning().noquote() << QStringLiteral("[RadarWorkMode] device=%1 雷达回显 mode=0，拒绝工作→Fault")
                                    .arg(m_deviceId);
        }
        break;
    case State::Working:
        if (mode == 0) {
            transitionTo(State::Fault);
            qWarning().noquote() << QStringLiteral("[RadarWorkMode] device=%1 雷达异常回显 mode=0，Working→Fault")
                                    .arg(m_deviceId);
        }
        break;
    case State::Stopping:
        m_transitionTimer.stop();
        if (mode == 0) {
            transitionTo(State::Standby);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief 等待回显超时后进入 Error，并发出可供 UI 展示的转换失败原因。
 */
void RadarWorkModeController::onTransitionTimeout()
{
    // 仅在过渡态生效；若状态已被外部改变（例如同步进入 Fault），不再触发
    if (m_state == State::Starting) {
        transitionTo(State::Working);
    } else if (m_state == State::Stopping) {
        transitionTo(State::Standby);
    }
}

/**
 * @brief 只在状态实际变化时更新并发出 old/new 状态信号，避免重复通知。
 */
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

/**
 * @brief 通过 IWorkModeCommandSender 下发模式，并仅在发送成功时发出 commandIssued。
 */
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

/**
 * @brief 把状态映射为稳定中文诊断文本，不参与状态判断。
 */
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

/**
 * @brief 把拒绝原因映射为稳定中文诊断文本，供日志和 Web 提示使用。
 */
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
