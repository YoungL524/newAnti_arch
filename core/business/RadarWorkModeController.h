#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <cstdint>

class IWorkModeCommandSender;

/**
 * @file RadarWorkModeController.h
 * @brief 雷达工作状态控制器
 * @details 业务文档：doc/business/雷达工作状态控制.md
 *          单元测试：doc/test/雷达工作状态控制-单元测试.md
 *
 * 职责：
 * - 维护单个雷达设备的工作状态机
 * - 接收 UI / 命令行的请求并按规则转换状态
 * - 通过 IWorkModeCommandSender 下发 workStandby 命令
 * - 发出 stateChanged / workModeCommandIssued / requestRejected 事件
 *
 * 状态转换：见业务文档第二节
 *
 * 线程模型：
 * - 当前实现运行在所属 QThread 的事件循环中（默认主线程）
 * - QTimer 必须由 QObject 所在线程驱动，故 requestX 应在所属线程或通过
 *   queued connection 调用
 */
class RadarWorkModeController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 工作状态枚举
     */
    enum class State : uint8_t {
        Standby = 0,
        Starting,
        Working,
        Stopping,
        Fault
    };
    Q_ENUM(State)

    /**
     * @brief 请求被拒绝的原因
     */
    enum class RejectReason : uint8_t {
        DuplicateRequest = 0,         ///< 已处于目标状态
        TransitionInProgress,         ///< 处于 Starting / Stopping 过渡态
        FaultRequiresReset,           ///< Fault 状态下只接受 reset
        ResetIgnoredOutsideFault      ///< 非 Fault 状态下的 reset
    };
    Q_ENUM(RejectReason)

    /**
     * @brief 构造函数
     * @param deviceId 目标雷达设备 ID（DeviceType 枚举值）
     * @param sender   命令发送器，生命周期需覆盖本对象；不能为空
     * @param parent   Qt parent
     */
    explicit RadarWorkModeController(uint16_t deviceId,
                                     IWorkModeCommandSender* sender,
                                     QObject* parent = nullptr);

    ~RadarWorkModeController() override;

    /**
     * @brief 当前状态
     */
    State state() const { return m_state; }

    /**
     * @brief 关联的雷达设备 ID
     */
    uint16_t deviceId() const { return m_deviceId; }

    /**
     * @brief 当前过渡超时（毫秒）
     */
    int transitionTimeoutMs() const { return m_timeoutMs; }

    /**
     * @brief 设置过渡超时（毫秒）
     * @details 当前为本地计时器实现的占位值。未来引入 ACK 协议后
     *          本接口的语义会演进为 ACK 等待超时阈值。
     *          单元测试通常将其设置为很小的值（如 5ms）以加速。
     */
    void setTransitionTimeoutMs(int ms);

    /**
     * @brief 状态枚举到字符串（日志/调试用）
     */
    static QString stateName(State s);

    /**
     * @brief 拒绝原因到字符串（日志/调试用）
     */
    static QString rejectReasonName(RejectReason r);

public slots:
    /**
     * @brief 请求进入工作状态
     * @return 命令是否被接受（拒绝时发出 requestRejected）
     */
    bool requestWork();

    /**
     * @brief 请求进入待机状态
     * @return 命令是否被接受（拒绝时发出 requestRejected）
     */
    bool requestStandby();

    /**
     * @brief 复位（仅 Fault 状态有效）
     */
    void reset();

    /**
     * @brief 处理雷达回显的 workStandby 模式（ACK 替代超时）
     * @param mode 回显的 Work_Standby_Mode (1=工作, 0=待机)
     */
    void onWorkModeEcho(uint32_t mode);

signals:
    /**
     * @brief 状态发生真实变化时发出
     * @param oldState 旧状态
     * @param newState 新状态
     */
    void stateChanged(RadarWorkModeController::State oldState,
                      RadarWorkModeController::State newState);

    /**
     * @brief 成功向 sender 提交命令时发出
     * @param deviceId 目标雷达设备 ID
     * @param mode 1=工作, 0=待机
     */
    void workModeCommandIssued(uint16_t deviceId, uint32_t mode);

    /**
     * @brief 请求被拒绝时发出（状态不变化）
     * @param reason 拒绝原因
     * @param currentState 当前状态
     */
    void requestRejected(RadarWorkModeController::RejectReason reason,
                         RadarWorkModeController::State currentState);

private slots:
    /**
     * @brief 一次性过渡超时处理
     */
    void onTransitionTimeout();

private:
    /**
     * @brief 切换到目标状态并发出 stateChanged
     * @details 若 newState == m_state 则不发事件
     */
    void transitionTo(State newState);

    /**
     * @brief 调用 sender 下发命令
     * @return 发送是否成功
     */
    bool issueCommand(uint32_t mode);

private:
    uint16_t m_deviceId;
    IWorkModeCommandSender* m_sender;
    State m_state = State::Standby;
    int m_timeoutMs = 300;
    QTimer m_transitionTimer;
};
