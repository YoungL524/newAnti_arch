#include "core/web/StateWebSocketService.h"
#include "core/web/WebSocketGateway.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QWebSocket>

namespace {

/**
 * @brief 把Core已冻结的三态心跳值映射为V1网络枚举，不在Web边界细分新状态。
 * @param status DeviceController写入DeviceState的0、1或2。
 * @return 与前后端接口合同一致的稳定字符串；门禁外数值按未知处理。
 */
QString heartbeatStatusName(quint8 status)
{
    if (status == 1U) return QStringLiteral("online");
    if (status == 2U) return QStringLiteral("offlineOrLate");
    return QStringLiteral("unknownOrTimedOut");
}

} // namespace

namespace core::web {

/**
 * @brief 将Doorbell接入共享Lane，将Gateway新连接接入初始快照；两类信号均不搬运业务事实。
 */
StateWebSocketService::StateWebSocketService(
    const core::domain::LatestState<core::model::DeviceState> &state,
    core::execution::ExecutionLane &lane,
    WebSocketGateway &gateway,
    const core::model::StableIdRegistry &stableIds)
    : m_state(state)
    , m_lane(lane)
    , m_gateway(gateway)
    , m_stableIds(stableIds)
    , m_slot(lane.acquireSlot())
{
    QObject::connect(state.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, [this](quint64) { m_lane.request(m_slot); }, Qt::DirectConnection);
    QObject::connect(&lane, &core::execution::ExecutionLane::executeRequested,
                     this, [this](quint64 mask) {
        if ((mask & (quint64(1) << m_slot)) == 0 || m_revision == m_state.revision()) return;
        m_revision = m_state.revision();
        const QJsonObject current = message();
        if (!current.isEmpty()) m_gateway.broadcast(current);
    });
    QObject::connect(&gateway, &WebSocketGateway::clientConnected,
                     this, [this](QWebSocket *client) { send(client); });
}

/**
 * @brief 先断开所有外部信号再释放Lane位，Gateway与State仍由组合根继续拥有。
 */
StateWebSocketService::~StateWebSocketService()
{
    QObject::disconnect(m_state.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(&m_lane, nullptr, this, nullptr);
    QObject::disconnect(&m_gateway, nullptr, this, nullptr);
    m_lane.releaseSlot(m_slot);
}

/**
 * @brief 新连接只取得当前完整快照；发送失败由Gateway按慢连接策略处理。
 */
void StateWebSocketService::send(QWebSocket *socket)
{
    if (!socket) return;
    const QJsonObject current = message();
    if (!current.isEmpty()) m_gateway.send(*socket, current);
}

/**
 * @brief 在Web边界把Core三态数值映射为字符串并嵌套payload，revision保持DeviceState独立作用域。
 */
QJsonObject StateWebSocketService::message() const
{
    const auto record = m_state.latest();
    if (!record) return {};
    const auto &value = record->sample->value;
    QJsonArray devices;
    for (const auto &device : value.devices) {
        const QUuid stableId = m_stableIds.resolveDevice(device.deviceId);
        if (stableId.isNull()) continue;
        const bool acquisition = device.operatingState
            == core::model::DeviceOperatingState::Collecting;
        devices.append(QJsonObject {
            {QStringLiteral("id"), stableId.toString(QUuid::WithoutBraces)},
            {QStringLiteral("state"), acquisition ? QStringLiteral("Working")
                                                   : QStringLiteral("Standby")},
            {QStringLiteral("acquisition"), acquisition}});
    }
    const QJsonObject payload{{"dpHeartbeat", heartbeatStatusName(value.dpHeartbeat)},
        {"spHeartbeat", heartbeatStatusName(value.spHeartbeat)},
        {"fpOnlineMask", value.fpOnlineMask}, {"fpDataMask", value.fpDataMask},
        {"devices", devices}};
    return QJsonObject{{"type", "deviceState"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(record->revision)}, {"payload", payload}};
}

} // namespace core::web
