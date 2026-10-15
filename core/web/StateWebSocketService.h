/**
 * @file StateWebSocketService.h
 * @brief 分别读取 DeviceState、TaskState 与 SystemState 完整快照，通过唯一 WebSocketGateway 输出。
 * @details Doorbell只请求共享Lane；每个Gateway连接首次读取最新值，后续按revision合并推送。本类不拥有
 * socket、不判断DP/SP掉线、不保存第二份状态，也不处理Command；新人应从构造函数及三个独立 revision 阅读。
 */
#pragma once

#include "core/datadomain/LatestState.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/RuntimeState.h"
#include "core/model/StableIdRegistry.h"

#include <QJsonObject>

QT_FORWARD_DECLARE_CLASS(QWebSocket)

namespace core::web {

class WebSocketGateway;

/** @brief 与 Lane 同线程执行的低频状态 Reader，所有借用依赖必须覆盖本对象生命周期。 */
class StateWebSocketService final : public QObject
{
public:
    /** @brief 绑定只读状态、已有执行通道和唯一Gateway；不启动线程或网络端口。 */
    StateWebSocketService(
        const core::domain::LatestState<core::model::DeviceState> &state,
        const core::domain::LatestState<core::model::SystemState> &systemState,
        const core::domain::LatestState<core::model::TaskState> &taskState,
        core::execution::ExecutionLane &lane,
        WebSocketGateway &gateway,
        const core::model::StableIdRegistry &stableIds);

    /** @brief 断开状态、Lane 和 Gateway 信号并释放调度位，必须在所属线程析构。 */
    ~StateWebSocketService() override;

private:
    /** @brief 主动读取完整State并向新连接发送一次快照；未初始化State时不伪造全零消息。 */
    void send(QWebSocket *socket);

    /** @brief 形成当前DeviceState网络DTO；State未初始化时返回空对象。 */
    QJsonObject message() const;
    /** @brief 读取 SystemState 并形成独立 revision 的系统快照；尚未初始化时不宣告 Ready。 */
    QJsonObject systemMessage() const;
    /** @brief 读取唯一 TaskState 并解析稳定 UUID；未初始化或非 None 身份无法解析时不伪造消息。 */
    QJsonObject taskMessage() const;

    const core::domain::LatestState<core::model::DeviceState> &m_state; ///< 唯一事实来源。
    const core::domain::LatestState<core::model::SystemState> &m_systemState; ///< 系统状态只读借用，不复制设备状态。
    const core::domain::LatestState<core::model::TaskState> &m_taskState; ///< TaskService 唯一写入的当前阶段，存储线程更新。
    core::execution::ExecutionLane &m_lane; ///< 借用当前事件循环执行器。
    WebSocketGateway &m_gateway; ///< 借用唯一网络连接owner。
    const core::model::StableIdRegistry &m_stableIds; ///< 只读稳定UUID映射，不保存第二份设备状态。
    core::execution::ExecutionSlot m_slot; ///< Reader在共享Lane中的调度位。
    quint64 m_revision = 0; ///< 已处理的DeviceState独立修订号。
    quint64 m_systemRevision = 0; ///< 已处理的 SystemState 独立修订号，不能与设备 revision 比较。
    quint64 m_taskRevision = 0; ///< 已处理的 TaskState 独立修订号，不表示任务台账版本。
};

} // namespace core::web
