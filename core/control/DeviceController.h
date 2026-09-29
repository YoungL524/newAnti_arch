/**
 * @file DeviceController.h
 * @brief 定义设备状态与当前任务控制参数的唯一运行时 Owner。
 * @details 上游由协议 Adapter 交付 DP/SP 心跳、ApplicationController 交付已解析任务；下游仅写入
 * LatestState<DeviceState>，并经组合根借入的发送函数提交0x0001周期控制数据报。本类不拥有Socket，
 * 不等待ACK，也不把控制报文写入Topic；新人应先阅读 beginAcquisition() 与 acceptHeartbeat()。
 */

#pragma once

#include "core/datadomain/LatestState.h"
#include "core/config/DeviceConfiguration.h"
#include "core/model/RuntimeState.h"
#include "core/task/RadarControlSettings.h"

#include <QHash>
#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>

#include <array>
#include <functional>

namespace core::control {

/**
 * @brief 串行拥有设备心跳期限、当前任务参数副本和任务阵面选择。
 * @warning 全部方法必须在对象所属 Qt 线程调用；借用的 LatestState 必须比本对象存活更久。
 */
class DeviceController final : public QObject
{
public:
    /** @brief 同线程提交一个已编码控制数据报；true仅表示UDP本地提交成功，不表示DP执行。 */
    using ControlSender = std::function<bool(
        const QByteArray &, const QHostAddress &, quint16)>;

    /** @brief 初始化灰色完整设备状态，但不启动心跳期限定时器。 */
    explicit DeviceController(
        core::domain::LatestState<core::model::DeviceState> &state,
        ControlSender controlSender = {},
        QObject *parent = nullptr);

    /** @brief 清除旧心跳期限和在线位图，并启动100 ms期限检查。 */
    void start();

    /** @brief 停止期限检查；不会伪造新的设备报文或清除已准备任务。 */
    void stop();

    /**
     * @brief 接收 Adapter 已校验的 DP 或 SP 心跳并更新完整 DeviceState。
     * @param sp false 表示DP心跳，true表示SP心跳。
     * @param online 协议在线值，只接受0或1。
     * @param mask DP的FP在线位或SP的数据位，只允许低四位。
     * @param utcMs Socket接收完整报文时的Unix epoch UTC毫秒。
     * @param steadyNs 同一接收点的进程内单调纳秒。
     */
    void acceptHeartbeat(bool sp, quint16 online, quint16 mask, qint64 utcMs, qint64 steadyNs);

    /** @brief 按显式单调时刻执行2秒迟到、5秒超时判定，供定时器和确定性测试共用。 */
    void checkHeartbeatDeadlines(qint64 utcMs, qint64 steadyNs);

    /**
     * @brief 原子替换当前任务的运行参数副本与逐设备阵面位图。
     * @param taskId TaskService 已解析的当前进程非零任务句柄。
     * @param settings 创建任务时冻结且通过合同校验的独立参数快照。
     * @param deviceFaceMasks 非空设备集合；键为非零 DeviceId，值只允许低四位且不得为零。
     * @return 全部输入合法时替换旧准备态；失败保持原值并记录原因。
     * @note 准备态不表示采集已经开始，也不会触发 UDP 发送或修改 TaskState。
     */
    bool prepareTask(
        core::model::TaskId taskId,
        const core::task::RadarControlSettings &settings,
        const QHash<core::model::DeviceId, quint8> &deviceFaceMasks);

    /** @brief 仅在任务句柄匹配时清除准备态；不结束采集会话或发送待机命令。 */
    bool clearPreparedTask(core::model::TaskId taskId);

    /** @brief 返回是否持有一项已校验但尚未代表开始采集的任务。 */
    bool hasPreparedTask() const noexcept;

    /** @brief 返回当前准备任务句柄；没有准备任务时为零。 */
    core::model::TaskId preparedTaskId() const noexcept;

    /** @brief 返回指定设备在当前任务中的低四位阵面位图；未绑定时返回零。 */
    quint8 faceMask(core::model::DeviceId deviceId) const noexcept;

    /** @brief 返回当前任务参数值副本；仅供后续发送边界读取，不含协议包头和保留字段。 */
    core::task::RadarControlSettings controlSettings() const;

    /**
     * @brief 仅在准备任务匹配时一次替换运行参数副本，供 50 ms 定时器下一周期读取。
     * @param taskId 期望同步的任务句柄；零或与当前准备任务不一致时不修改。
     * @param settings 调用方已通过结构、值域和任务状态校验的完整参数。
     * @return 当前准备任务恰为 taskId 并完成替换返回 true；否则保持旧值返回 false。
     * @note 只做纯值拷贝，不编码、不发送、不改 DeviceState、不启停定时器；返回 false 只表示该任务此刻
     * 没有运行副本，调用方不应把它当作数据损坏。
     */
    bool applyControlSettings(
        core::model::TaskId taskId,
        const core::task::RadarControlSettings &settings);

    /**
     * @brief 将已创建的逐阵面会话发布为本地采集事实，使PPI进入扫描状态。
     * @note 本地状态切换不依赖心跳或DP执行结果；随后仅在DP、SP均在线时启动50 ms控制发送。
     */
    bool beginAcquisition(
        core::model::DeviceId deviceId,
        core::model::EndpointId endpointId,
        quint32 configurationRevision,
        const core::config::DeviceConfiguration &configuration,
        const std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds);

    /** @brief 返回当前唯一采集设备及会话身份；没有采集时返回false且清空输出。 */
    bool activeAcquisition(
        core::model::DeviceId &deviceId,
        core::model::EndpointId &endpointId,
        core::model::TaskId &taskId,
        std::array<core::model::AcquisitionId, 4> &faceAcquisitionIds) const;

    /** @brief 清除当前本地采集事实并回到Idle；不清除任务准备态。 */
    bool endAcquisition();

    /** @brief 返回最近一次任务准备或清除失败原因。 */
    QString lastError() const;

private:
    /** @brief 为完整快照补齐聚合元数据，通过门禁后才替换共享 DeviceState。 */
    bool publishDeviceState(
        core::model::Sample<core::model::DeviceState> sample,
        qint64 utcMs,
        qint64 steadyNs);

    /** @brief 在当前线程临时编码并提交一包0x0001；心跳门控由调用点决定。 */
    bool sendControl(bool acquisitionControl);

    /** @brief 按本地采集与DP/SP在线事实启停50 ms定时器；恢复在线时不补发离线周期。 */
    void refreshControlTimer();

    core::domain::LatestState<core::model::DeviceState> &m_state; ///< 借用的设备完整状态；本类是唯一写入者。
    std::array<qint64, 2> m_lastHeartbeatSteadyNs {}; ///< DP/SP最近有效心跳单调时刻；零表示从未收到。
    QTimer m_heartbeatTimer; ///< 只驱动期限检查，不轮询 Topic 或设备业务数据。
    QTimer m_controlTimer; ///< 仅在本地采集且DP/SP均在线时运行；每50 ms发送最新控制快照。
    ControlSender m_controlSender; ///< 借用组合根的唯一sock2Internal发送能力。
    core::model::TaskId m_preparedTaskId; ///< 当前准备任务；零表示尚未准备。
    core::task::RadarControlSettings m_controlSettings; ///< 当前任务独立参数的唯一运行副本。
    QHash<core::model::DeviceId, quint8> m_deviceFaceMasks; ///< 当前任务逐设备启用阵面；不保存历史。
    core::model::DeviceId m_activeDeviceId; ///< 当前正在采集的完整雷达句柄；Idle时为零。
    core::config::DeviceConfiguration m_activeConfiguration; ///< 采集开始时冻结的设备部署参数。
    QHostAddress m_controlTargetAddress; ///< 当前设备protocolConfig中的DPH目标地址。
    quint16 m_controlTargetPort = 0; ///< 当前设备protocolConfig中的DPH目标端口。
    QString m_lastError; ///< 最近低频控制准备失败说明，不进入状态或事件数据域。
};

} // namespace core::control
