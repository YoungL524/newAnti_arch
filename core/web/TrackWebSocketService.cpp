#include "core/web/TrackWebSocketService.h"

#include <QJsonArray>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QWebSocket>

#include <limits>
#include <stdexcept>

namespace core::web {

/**
 * @brief 验证资源上限、预分配 ScratchBuffer，并接通 Topic Doorbell、ExecutionLane 和新连接信号。
 */
TrackWebSocketService::TrackWebSocketService(
    const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
    core::execution::ExecutionLane &executionLane,
    qsizetype maxRecordsPerTurn,
    qsizetype maxTargets,
    QObject *parent)
    : QObject(parent)
    , m_trackTopic(trackTopic)
    , m_executionLane(executionLane)
    , m_scratch(maxRecordsPerTurn)
    , m_executionSlot(executionLane.acquireSlot())
    , m_maxTargets(maxTargets)
    , m_server(QStringLiteral("NewAntiDrone Track WebReader"), QWebSocketServer::NonSecureMode, this)
{
    if (maxRecordsPerTurn <= 0 || maxTargets <= 0) {
        m_executionLane.releaseSlot(m_executionSlot);
        throw std::invalid_argument("TrackWebSocketService limits must be greater than zero");
    }
    QObject::connect(trackTopic.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, &TrackWebSocketService::onTrackTopicAdvanced, Qt::DirectConnection);
    QObject::connect(&executionLane, &core::execution::ExecutionLane::executeRequested,
                     this, &TrackWebSocketService::onExecutionRequested);
    QObject::connect(&m_server, &QWebSocketServer::newConnection,
                     this, &TrackWebSocketService::onNewConnection);
}

/**
 * @brief 先关闭网络连接，再释放 Lane 位，避免析构期间继续调度 Consumer。
 */
TrackWebSocketService::~TrackWebSocketService()
{
    stop();
    QObject::disconnect(&m_executionLane, nullptr, this, nullptr);
    m_executionLane.releaseSlot(m_executionSlot);
}

/**
 * @brief 建立 IPv4 任意地址监听；成功后主动补读监听前已经驻留的 Topic 数据。
 */
bool TrackWebSocketService::start(quint16 port)
{
    if (m_server.isListening() || !m_server.listen(QHostAddress::AnyIPv4, port)) {
        return false;
    }
    refresh();
    return true;
}

/**
 * @brief 主动关闭客户端并停止接受新连接，不改变 cursor 与最终显示态，允许同实例重新监听。
 */
void TrackWebSocketService::stop()
{
    const auto clients = m_clients;
    for (QWebSocket *client : clients) {
        client->close();
        client->deleteLater();
    }
    m_clients.clear();
    m_server.close();
}

/**
 * @brief 返回 QWebSocketServer 的实际端口，未监听时明确为零。
 */
quint16 TrackWebSocketService::serverPort() const noexcept
{
    return m_server.isListening() ? m_server.serverPort() : 0;
}

/**
 * @brief 返回只由成功折叠记录推进的 WebReader cursor。
 */
core::domain::TopicSequence TrackWebSocketService::cursor() const noexcept
{
    return m_cursor;
}

/**
 * @brief 返回当前 socket 集合大小，不访问 Topic。
 */
qsizetype TrackWebSocketService::clientCount() const noexcept
{
    return m_clients.size();
}

/**
 * @brief 在Doorbell线程仅向线程安全Lane置位，不访问Web状态，避免跨线程通知堆积。
 */
void TrackWebSocketService::onTrackTopicAdvanced(quint64 newestSequence)
{
    Q_UNUSED(newestSequence)
    m_executionLane.request(m_executionSlot);
}

/**
 * @brief Topic 非空时安排一次读取，不使用空闲轮询或定时器。
 */
void TrackWebSocketService::refresh()
{
    if (!m_trackTopic.range().empty) {
        m_executionLane.request(m_executionSlot);
    }
}

/**
 * @brief 取得服务器交出的 socket 所有权，登记断开清理，并发送当前 revision 的完整快照。
 */
void TrackWebSocketService::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QWebSocket *client = m_server.nextPendingConnection();
        if (client == nullptr) {
            continue;
        }
        if (m_clients.size() >= 8) {
            client->abort();
            client->deleteLater();
            continue;
        }
        m_clients.insert(client);
        QObject::connect(client, &QWebSocket::disconnected, this, [this, client]() {
            m_clients.remove(client);
            client->deleteLater();
        });
        sendSnapshot(*client);
    }
}

/**
 * @brief 仅消费属于本 Reader 的 pending 位；批次未追平时重新置位，让事件循环保持公平。
 */
void TrackWebSocketService::onExecutionRequested(quint64 pendingMask)
{
    const quint64 ownBit = quint64(1) << m_executionSlot;
    if ((pendingMask & ownBit) != 0 && consumeOneBatch()) {
        m_executionLane.request(m_executionSlot);
    }
}

/**
 * @brief 读取 cursor 后的固定批次，并把 gap 处理为一次完整 Web 状态重建。
 * @details Gap/Ahead 时旧最终状态无法证明连续，故清空后从当前驻留窗口重读并以 snapshot 通知客户端；正常
 * 批次只发送 upsert/removal delta。锁已在 HistoryTopic 返回前释放，JSON 和 socket 工作均不占 Topic 锁。
 */
bool TrackWebSocketService::consumeOneBatch()
{
    auto result = m_trackTopic.readAfter(m_cursor, m_scratch);
    bool rebuild = false;
    if (result.relation == core::domain::CursorRelation::Ahead) {
        m_cursor = 0;
        m_trails.clear();
        rebuild = true;
        result = m_trackTopic.readAfter(0, m_scratch);
    } else if (result.relation == core::domain::CursorRelation::Gap) {
        m_trails.clear();
        rebuild = true;
    }

    QJsonArray upserts;
    QJsonArray removals;
    for (qsizetype index = 0; index < result.count; ++index) {
        const auto &record = m_scratch[index];
        applyRecord(record, upserts, removals);
        m_cursor = record.sequence;
    }

    if (rebuild) {
        ++m_revision;
        for (QWebSocket *client : m_clients) {
            sendSnapshot(*client);
        }
    } else if (!upserts.isEmpty() || !removals.isEmpty()) {
        broadcastDelta(upserts, removals);
    }
    return result.hasMore;
}

/**
 * @brief 将一条 Sample 转为一个最终显示点，并在固定 20 点尾迹槽位上执行覆盖。
 * @details 新 TrackKey 达到目标容量时先淘汰最久未更新项；点序号接近 quint32 上限时清空本航迹，防止浏览器
 * 数值回绕破坏排序。每次覆盖都会先发 removal，再发相同批次的 upsert。
 */
void TrackWebSocketService::applyRecord(
    const core::domain::TopicRecord<core::model::TrackPacket> &record,
    QJsonArray &upserts,
    QJsonArray &removals)
{
    const auto &sample = record.sample;
    const core::model::TrackKey key = core::model::makeTrackKey(sample.value);
    auto iterator = m_trails.find(key);
    if (iterator == m_trails.end()) {
        if (m_trails.size() >= m_maxTargets) {
            evictOldestTrack(removals);
        }
        iterator = m_trails.insert(key, TrackTrail{});
    }

    TrackTrail &trail = iterator.value();
    if (trail.nextPointIndex == std::numeric_limits<quint32>::max()) {
        for (qsizetype index = 0; index < trail.size; ++index) {
            removals.append(pointKeyJson(trail.points[static_cast<std::size_t>(index)]));
        }
        trail = TrackTrail{};
    }
    if (trail.size == static_cast<qsizetype>(trail.points.size())) {
        removals.append(pointKeyJson(trail.points[static_cast<std::size_t>(trail.nextSlot)]));
    }

    DisplayPoint point;
    point.acquisitionId = sample.value.acquisitionId;
    point.deviceId = sample.value.deviceId;
    point.taskId = sample.value.taskId;
    point.trackId = sample.value.trackId;
    point.pointIndex = trail.nextPointIndex++;
    point.timestampUtcMs = sample.meta.receiveTimeUtcMs;
    point.rangeM = sample.value.rangeM;
    point.azimuthRad = sample.value.azimuthRad;
    point.elevationRad = sample.value.elevationRad;
    point.radialVelocityMps = sample.value.radialVelocityMps;
    point.amplitude = sample.value.amplitude;
    trail.points[static_cast<std::size_t>(trail.nextSlot)] = point;
    trail.nextSlot = (trail.nextSlot + 1) % static_cast<qsizetype>(trail.points.size());
    trail.size = qMin(trail.size + 1, static_cast<qsizetype>(trail.points.size()));
    trail.lastSequence = record.sequence;
    upserts.append(pointJson(point));
}

/**
 * @brief 线性查找最近最少更新航迹并移除；仅在新增目标越过固定容量时执行。
 */
void TrackWebSocketService::evictOldestTrack(QJsonArray &removals)
{
    auto oldest = m_trails.end();
    for (auto iterator = m_trails.begin(); iterator != m_trails.end(); ++iterator) {
        if (oldest == m_trails.end() || iterator.value().lastSequence < oldest.value().lastSequence) {
            oldest = iterator;
        }
    }
    if (oldest == m_trails.end()) {
        return;
    }
    const TrackTrail &trail = oldest.value();
    for (qsizetype index = 0; index < trail.size; ++index) {
        removals.append(pointKeyJson(trail.points[static_cast<std::size_t>(index)]));
    }
    m_trails.erase(oldest);
}

/**
 * @brief 生成与 UI 单位合同一致的 Web 显示点，运行时句柄在此序列化边界转为稳定字符串形式。
 */
QJsonObject TrackWebSocketService::pointJson(const DisplayPoint &point)
{
    QJsonObject object = pointKeyJson(point);
    object.insert(QStringLiteral("taskId"), QStringLiteral("task-%1").arg(point.taskId.value()));
    object.insert(QStringLiteral("timestamp"), static_cast<double>(point.timestampUtcMs));
    object.insert(QStringLiteral("range"), point.rangeM);
    object.insert(QStringLiteral("azimuth"), point.azimuthRad);
    object.insert(QStringLiteral("elevation"), point.elevationRad);
    object.insert(QStringLiteral("altitude"), 0.0);
    object.insert(QStringLiteral("radialSpeed"), point.radialVelocityMps);
    object.insert(QStringLiteral("amplitude"), point.amplitude);
    return object;
}

/**
 * @brief 生成 acquisition/device/track/point 四元删除键，AcquisitionId 用字符串避免 JavaScript 精度损失。
 */
QJsonObject TrackWebSocketService::pointKeyJson(const DisplayPoint &point)
{
    QJsonObject object;
    object.insert(QStringLiteral("acquisitionId"), QString::number(point.acquisitionId.value()));
    object.insert(QStringLiteral("deviceId"), QStringLiteral("radar-%1").arg(point.deviceId.value()));
    object.insert(QStringLiteral("trackId"), static_cast<double>(point.trackId));
    object.insert(QStringLiteral("pointIndex"), static_cast<double>(point.pointIndex));
    return object;
}

/**
 * @brief 从有界最终状态生成一个完整 snapshot；只在新连接或 gap 重建时执行。
 */
void TrackWebSocketService::sendSnapshot(QWebSocket &client) const
{
    QJsonArray tracks;
    for (auto iterator = m_trails.constBegin(); iterator != m_trails.constEnd(); ++iterator) {
        const TrackTrail &trail = iterator.value();
        for (qsizetype index = 0; index < trail.size; ++index) {
            tracks.append(pointJson(trail.points[static_cast<std::size_t>(index)]));
        }
    }
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("tracks"));
    message.insert(QStringLiteral("mode"), QStringLiteral("snapshot"));
    message.insert(QStringLiteral("revision"), static_cast<double>(m_revision));
    message.insert(QStringLiteral("timestamp"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    message.insert(QStringLiteral("tracks"), tracks);
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (client.bytesToWrite() + bytes.size() > 4 * 1024 * 1024) {
        client.abort();
        return;
    }
    client.sendTextMessage(QString::fromUtf8(bytes));
}

/**
 * @brief 为本批变化推进一次 revision，并复用同一个紧凑 JSON 文本发送给所有客户端。
 */
void TrackWebSocketService::broadcastDelta(const QJsonArray &upserts, const QJsonArray &removals)
{
    ++m_revision;
    if (m_clients.isEmpty()) {
        return;
    }
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("tracks"));
    message.insert(QStringLiteral("mode"), QStringLiteral("delta"));
    message.insert(QStringLiteral("revision"), static_cast<double>(m_revision));
    message.insert(QStringLiteral("timestamp"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    message.insert(QStringLiteral("upserts"), upserts);
    message.insert(QStringLiteral("removals"), removals);
    const QString payload = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    const auto payloadBytes = payload.toUtf8().size();
    const auto clients = m_clients;
    for (QWebSocket *client : clients) {
        if (client->bytesToWrite() + payloadBytes > 4 * 1024 * 1024) {
            client->abort();
        } else {
            client->sendTextMessage(payload);
        }
    }
}

} // namespace core::web
