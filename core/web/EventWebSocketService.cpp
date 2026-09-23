#include "core/web/EventWebSocketService.h"
#include "core/web/WebSocketGateway.h"

#include <QWebSocket>
#include <QDateTime>

namespace core::web {

/**
 * @brief 先登记 Lane 位，再连接只携带水位的 Doorbell 和浏览器新连接信号。
 */
EventWebSocketService::EventWebSocketService(
    const core::domain::EventTopic<core::model::SystemEvent> &topic,
    core::execution::ExecutionLane &lane,
    WebSocketGateway &gateway,
    const core::model::StableIdRegistry &stableIds)
    : m_topic(topic)
    , m_lane(lane)
    , m_gateway(gateway)
    , m_stableIds(stableIds)
    , m_slot(lane.acquireSlot())
{
    QObject::connect(topic.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, [this](quint64) { m_lane.request(m_slot); }, Qt::DirectConnection);
    QObject::connect(&lane, &core::execution::ExecutionLane::executeRequested,
                     this, [this](quint64 pendingMask) { drain(pendingMask); });
    QObject::connect(&gateway, &WebSocketGateway::clientConnected,
                     this, [this](QWebSocket *client) { sendRecent(client); });
}

/**
 * @brief 停止接收通知后释放 Lane 位，避免对象销毁后的 queued 唤醒使用旧位。
 */
EventWebSocketService::~EventWebSocketService()
{
    QObject::disconnect(m_topic.doorbell(), nullptr, this, nullptr);
    QObject::disconnect(&m_lane, nullptr, this, nullptr);
    QObject::disconnect(&m_gateway, nullptr, this, nullptr);
    m_lane.releaseSlot(m_slot);
}

/**
 * @brief 在组合根完成网络监听后主动读取一次，补偿 Reader 注册前的事件。
 */
void EventWebSocketService::refresh()
{
    m_lane.request(m_slot);
}

/**
 * @brief 把当前有界批次发送到 Gateway，再推进独立 cursor；积压由 Lane 合并为下一次唤醒。
 */
void EventWebSocketService::drain(quint64 pendingMask)
{
    if ((pendingMask & (quint64(1) << m_slot)) == 0) return;
    const auto result = m_topic.readAfter(m_cursor, m_scratch);
    if (result.relation == core::domain::CursorRelation::Ahead || result.count == 0) return;
    if (result.relation == core::domain::CursorRelation::Gap) {
        m_gateway.broadcast(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("log")},
            {QStringLiteral("level"), QStringLiteral("WARN")},
            {QStringLiteral("message"), QStringLiteral("实时事件缺失：序号 %1 至 %2")
                .arg(result.gap.firstLost).arg(result.gap.lastLost)},
            {QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()}});
    }
    for (qsizetype index = 0; index < result.count; ++index) {
        const auto &record = m_scratch[index];
        const QJsonObject log = message(record);
        if (!log.isEmpty()) m_gateway.broadcast(log);
    }
    m_cursor = m_scratch[result.count - 1].sequence;
    if (result.hasMore) m_lane.request(m_slot);
}

/**
 * @brief 新连接只回放共享 Topic 当前仍驻留的最近事件，保留原有 Web cursor 不变。
 */
void EventWebSocketService::sendRecent(QWebSocket *client)
{
    if (!client) return;
    const auto range = m_topic.range();
    if (range.empty) return;
    const auto first = range.newest > 100 ? range.newest - 100 : 0;
    const auto result = m_topic.readAfter(first, m_scratch);
    for (qsizetype index = 0; index < result.count; ++index) {
        const QJsonObject log = message(m_scratch[index]);
        if (!log.isEmpty() && !m_gateway.send(*client, log)) return;
    }
}

/**
 * @brief 在 Web 边界从稳定码形成中文活动日志；未知码保留十六进制编号以便诊断。
 */
QJsonObject EventWebSocketService::message(
    const core::domain::TopicRecord<core::model::SystemEvent> &record) const
{
    const auto &event = record.sample.value;
    QString text;
    if (event.eventCode == core::model::EventCode::ControlTaskLifecycleChanged) {
        switch (event.argument0) {
        case 1: text = QStringLiteral("任务已创建"); break;
        case 2: text = QStringLiteral("任务已激活，可开始采集"); break;
        case 3: text = QStringLiteral("任务已完成"); break;
        case 4: text = QStringLiteral("任务已归档，历史事实保留"); break;
        default: return {};
        }
    } else {
        text = QStringLiteral("系统事件 0x%1")
            .arg(static_cast<quint32>(event.eventCode), 8, 16, QLatin1Char('0'));
    }
    const QString level = event.severity == core::model::EventSeverity::Info
        ? QStringLiteral("INFO")
        : event.severity == core::model::EventSeverity::Warning
            ? QStringLiteral("WARN") : QStringLiteral("ERROR");
    QJsonObject log {{QStringLiteral("type"), QStringLiteral("log")},
        {QStringLiteral("level"), level},
        {QStringLiteral("message"), text},
        {QStringLiteral("timestamp"), record.sample.meta.receiveTimeUtcMs},
        {QStringLiteral("sequence"), QString::number(record.sequence)}};
    if (core::model::hasEventSubject(event.flags, core::model::EventSubjectFlag::TaskIdValid)) {
        const QUuid uuid = m_stableIds.resolveTask(event.taskId);
        if (!uuid.isNull()) log.insert(QStringLiteral("taskId"), uuid.toString(QUuid::WithoutBraces));
    }
    if (core::model::hasEventSubject(event.flags, core::model::EventSubjectFlag::DeviceIdValid)) {
        const QUuid uuid = m_stableIds.resolveDevice(event.deviceId);
        if (!uuid.isNull()) log.insert(QStringLiteral("deviceId"), uuid.toString(QUuid::WithoutBraces));
    }
    return log;
}

} // namespace core::web
