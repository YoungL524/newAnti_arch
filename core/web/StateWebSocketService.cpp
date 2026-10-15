#include "core/web/StateWebSocketService.h"
#include "core/web/WebSocketGateway.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QWebSocket>

#include <cstddef>

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
 * @brief 将三类 State Doorbell 接入同一 Lane，并为新连接读取最新值；信号只调度，不搬运完整状态。
 */
StateWebSocketService::StateWebSocketService(
    const core::domain::LatestState<core::model::DeviceState> &state,
    const core::domain::LatestState<core::model::SystemState> &systemState,
    const core::domain::LatestState<core::model::TaskState> &taskState,
    core::execution::ExecutionLane &lane,
    WebSocketGateway &gateway,
    const core::model::StableIdRegistry &stableIds)
    : m_state(state)
    , m_systemState(systemState)
    , m_taskState(taskState)
    , m_lane(lane)
    , m_gateway(gateway)
    , m_stableIds(stableIds)
    , m_slot(lane.acquireSlot())
{
    QObject::connect(state.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, [this](quint64) { m_lane.request(m_slot); }, Qt::DirectConnection);
    QObject::connect(systemState.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, [this](quint64) { m_lane.request(m_slot); }, Qt::DirectConnection);
    QObject::connect(taskState.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, [this](quint64) { m_lane.request(m_slot); }, Qt::DirectConnection);
    QObject::connect(&lane, &core::execution::ExecutionLane::executeRequested,
                     this, [this](quint64 mask) {
        if ((mask & (quint64(1) << m_slot)) == 0) return;
        if (m_revision != m_state.revision()) {
            m_revision = m_state.revision();
            const QJsonObject current = message();
            if (!current.isEmpty()) m_gateway.broadcast(current);
        }
        if (m_systemRevision != m_systemState.revision()) {
            m_systemRevision = m_systemState.revision();
            const QJsonObject current = systemMessage();
            if (!current.isEmpty()) m_gateway.broadcast(current);
        }
        if (m_taskRevision != m_taskState.revision()) {
            m_taskRevision = m_taskState.revision();
            const QJsonObject current = taskMessage();
            if (!current.isEmpty()) m_gateway.broadcast(current);
        }
    });
    QObject::connect(&gateway, &WebSocketGateway::clientConnected,
                     this, [this](QWebSocket *client) { send(client); });
}

/**
 * @brief 先断开三类状态及其他外部信号再释放 Lane 位，Gateway 与 State 仍由组合根继续拥有。
 */
StateWebSocketService::~StateWebSocketService()
{
    QObject::disconnect(m_state.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(m_systemState.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(m_taskState.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(&m_lane, nullptr, this, nullptr);
    QObject::disconnect(&m_gateway, nullptr, this, nullptr);
    m_lane.releaseSlot(m_slot);
}

/**
 * @brief 新连接分别取得设备、系统与任务完整快照，不等待下一次更新；发送失败由 Gateway 处理慢连接。
 */
void StateWebSocketService::send(QWebSocket *socket)
{
    if (!socket) return;
    const QJsonObject current = message();
    if (!current.isEmpty()) m_gateway.send(*socket, current);
    const QJsonObject system = systemMessage();
    if (!system.isEmpty()) m_gateway.send(*socket, system);
    const QJsonObject task = taskMessage();
    if (!task.isEmpty()) m_gateway.send(*socket, task);
}

/**
 * @brief 只序列化 TaskService 当前阶段，用注册表稳定身份关联台账，不从设备状态推算任务状态。
 * @details None 省略 taskId；连续 Completed/None 更新可以合并，生命周期历史仍由 Event/SQLite 承担。
 */
QJsonObject StateWebSocketService::taskMessage() const
{
    const auto record = m_taskState.latest();
    if (!record) return {};
    const auto &value = record->sample->value;
    QString phase;
    switch (value.phase) {
    case core::model::TaskPhase::None: phase = QStringLiteral("none"); break;
    case core::model::TaskPhase::Ready: phase = QStringLiteral("ready"); break;
    case core::model::TaskPhase::Running: phase = QStringLiteral("running"); break;
    case core::model::TaskPhase::Completed: phase = QStringLiteral("completed"); break;
    case core::model::TaskPhase::Faulted: phase = QStringLiteral("faulted"); break;
    default: return {};
    }
    QJsonObject payload{{QStringLiteral("phase"), phase}};
    if (value.phase != core::model::TaskPhase::None) {
        const QUuid taskId = m_stableIds.resolveTask(value.taskId);
        if (taskId.isNull()) return {};
        payload.insert(QStringLiteral("taskId"), taskId.toString(QUuid::WithoutBraces));
    }
    return QJsonObject{{"type", "taskState"}, {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(record->revision)}, {"payload", payload}};
}

/**
 * @brief 将系统生命周期的冻结枚举与位图序列化；不由 WebSocket 连接数推断系统健康。
 * @details 数值枚举与 RuntimeState 合同一致；revision 保留十进制字符串，时间为 UTC 毫秒。
 */
QJsonObject StateWebSocketService::systemMessage() const
{
    const auto record = m_systemState.latest();
    if (!record) return {};
    const auto &value = record->sample->value;
    return QJsonObject{{"type", "systemState"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(record->revision)},
        {"payload", QJsonObject{
            {"processStartedUtcMs", value.processStartedUtcMs},
            {"phase", static_cast<int>(value.phase)},
            {"health", static_cast<int>(value.health)},
            {"readinessFlags", static_cast<int>(value.readinessFlags)},
            {"degradationFlags", static_cast<int>(value.degradationFlags)}}}};
}

/**
 * @brief 在Web边界把心跳、逐阵面实时位姿映射为设备状态快照，revision保持DeviceState独立作用域。
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
    QJsonObject payload{{"dpHeartbeat", heartbeatStatusName(value.dpHeartbeat)},
        {"spHeartbeat", heartbeatStatusName(value.spHeartbeat)},
        {"fpOnlineMask", value.fpOnlineMask}, {"fpDataMask", value.fpDataMask},
        {"devices", devices}};
    const auto &pose = value.radarPose;
    const QUuid poseDeviceId = m_stableIds.resolveDevice(pose.deviceId);
    if (value.dpHeartbeat == 1U && pose.validFaceMask != 0U && !poseDeviceId.isNull()) {
        QJsonArray faces;
        for (int index = 0; index < 4; ++index) {
            if ((pose.validFaceMask & (1U << index)) == 0U) continue;
            const auto &attitude = pose.faceAttitudeCdeg[std::size_t(index)];
            faces.append(QJsonObject{{"faceIndex", index},
                {"headingDeg", attitude[0] / 100.0},
                {"pitchDeg", attitude[1] / 100.0},
                {"rollDeg", attitude[2] / 100.0}});
        }
        payload.insert(QStringLiteral("radarPose"), QJsonObject{
            {"deviceId", poseDeviceId.toString(QUuid::WithoutBraces)},
            {"longitudeDeg", pose.longitudeE6 / 1'000'000.0},
            {"latitudeDeg", pose.latitudeE6 / 1'000'000.0},
            {"altitudeM", pose.altitudeCm / 100.0},
            {"validFaceMask", pose.validFaceMask}, {"faces", faces}});
    }
    return QJsonObject{{"type", "deviceState"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(record->revision)}, {"payload", payload}};
}

} // namespace core::web
