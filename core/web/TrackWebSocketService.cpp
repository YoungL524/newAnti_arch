#include "core/web/TrackWebSocketService.h"
#include "core/web/WebSocketGateway.h"

#include <QJsonArray>
#include <QDateTime>
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
    WebSocketGateway &gateway,
    const core::model::StableIdRegistry &stableIds,
    qsizetype maxRecordsPerTurn,
    qsizetype maxTargets,
    QObject *parent)
    : QObject(parent)
    , m_trackTopic(trackTopic)
    , m_executionLane(executionLane)
    , m_gateway(gateway)
    , m_stableIds(stableIds)
    , m_scratch(maxRecordsPerTurn)
    , m_executionSlot(executionLane.acquireSlot())
    , m_maxTargets(maxTargets)
{
    if (maxRecordsPerTurn <= 0 || maxTargets <= 0) {
        m_executionLane.releaseSlot(m_executionSlot);
        throw std::invalid_argument("TrackWebSocketService limits must be greater than zero");
    }
    QObject::connect(trackTopic.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, &TrackWebSocketService::onTrackTopicAdvanced, Qt::DirectConnection);
    QObject::connect(&executionLane, &core::execution::ExecutionLane::executeRequested,
                     this, &TrackWebSocketService::onExecutionRequested);
    QObject::connect(&gateway, &WebSocketGateway::clientConnected,
                     this, [this](QWebSocket *client) {
        if (client) sendSnapshot(*client);
    });
}

/**
 * @brief 先断开借用依赖，再释放Lane位；网络连接仍由Gateway拥有。
 */
TrackWebSocketService::~TrackWebSocketService()
{
    QObject::disconnect(&m_gateway, nullptr, this, nullptr);
    QObject::disconnect(&m_executionLane, nullptr, this, nullptr);
    m_executionLane.releaseSlot(m_executionSlot);
}

/**
 * @brief 返回只由成功折叠记录推进的 WebReader cursor。
 */
core::domain::TopicSequence TrackWebSocketService::cursor() const noexcept
{
    return m_cursor;
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
        QJsonArray tracks;
        for (auto iterator = m_trails.constBegin(); iterator != m_trails.constEnd(); ++iterator) {
            const TrackTrail &trail = iterator.value();
            for (qsizetype index = 0; index < trail.size; ++index) {
                tracks.append(pointJson(trail.points[static_cast<std::size_t>(index)]));
            }
        }
        QJsonObject payload;
        payload.insert(QStringLiteral("tracks"), tracks);
        m_gateway.broadcast(QJsonObject{{"type", "trackSnapshot"},
            {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
            {"revision", QString::number(m_revision)}, {"payload", payload}});
    } else if (!upserts.isEmpty() || !removals.isEmpty()) {
        broadcastDelta(upserts, removals);
    }
    return result.hasMore;
}

/**
 * @brief 将存在Sample转为显示点，或依据消亡Sample删除该TrackKey的完整尾迹。
 * @details 新TrackKey达到目标容量时先淘汰最久未更新项；点序号接近quint32上限时清空本航迹，防止浏览器
 * 数值回绕破坏排序。packFlag为0时只产生removal，不建立不存在目标的空派生状态。
 */
void TrackWebSocketService::applyRecord(
    const core::domain::TopicRecord<core::model::TrackPacket> &record,
    QJsonArray &upserts,
    QJsonArray &removals)
{
    const auto &sample = record.sample;
    if (!hasStableIdentity(sample.value)) return;
    const core::model::TrackKey key = core::model::makeTrackKey(sample.value);
    auto iterator = m_trails.find(key);
    if (sample.value.packFlag == 0U) {
        if (iterator != m_trails.end()) {
            const TrackTrail &trail = iterator.value();
            for (qsizetype index = 0; index < trail.size; ++index) {
                removals.append(pointKeyJson(trail.points[static_cast<std::size_t>(index)]));
            }
            m_trails.erase(iterator);
        }
        return;
    }
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
    point.trackId = sample.value.trackId;
    point.faceIndex = sample.value.faceIndex;
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
QJsonObject TrackWebSocketService::pointJson(const DisplayPoint &point) const
{
    QJsonObject object = pointKeyJson(point);
    object.insert(QStringLiteral("receiveUtcMs"), static_cast<double>(point.timestampUtcMs));
    object.insert(QStringLiteral("rangeM"), point.rangeM);
    object.insert(QStringLiteral("azimuthRad"), point.azimuthRad);
    object.insert(QStringLiteral("elevationRad"), point.elevationRad);
    object.insert(QStringLiteral("radialVelocityMps"), point.radialVelocityMps);
    object.insert(QStringLiteral("amplitude"), point.amplitude);
    return object;
}

/**
 * @brief 生成 acquisition/device/face/track/point 删除键，AcquisitionId 用字符串避免 JavaScript 精度损失。
 */
QJsonObject TrackWebSocketService::pointKeyJson(const DisplayPoint &point) const
{
    QJsonObject object;
    object.insert(QStringLiteral("acquisitionId"), m_stableIds.resolveAcquisition(point.acquisitionId)
        .toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("deviceId"), m_stableIds.resolveDevice(point.deviceId)
        .toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("trackId"), static_cast<double>(point.trackId));
    object.insert(QStringLiteral("faceIndex"), static_cast<int>(point.faceIndex));
    object.insert(QStringLiteral("pointIndex"), static_cast<double>(point.pointIndex));
    return object;
}

/**
 * @brief 在形成 Web 最终状态前验证稳定身份，防止泄露 CompactId 或产生无法删除的临时键。
 */
bool TrackWebSocketService::hasStableIdentity(const core::model::TrackPacket &track) const
{
    return !m_stableIds.resolveDevice(track.deviceId).isNull()
        && !m_stableIds.resolveAcquisition(track.acquisitionId).isNull();
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
    QJsonObject payload;
    payload.insert(QStringLiteral("tracks"), tracks);
    m_gateway.send(client, QJsonObject{{"type", "trackSnapshot"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(m_revision)}, {"payload", payload}});
}

/**
 * @brief 为本批变化推进一次 revision，并复用同一个紧凑 JSON 文本发送给所有客户端。
 */
void TrackWebSocketService::broadcastDelta(const QJsonArray &upserts, const QJsonArray &removals)
{
    ++m_revision;
    QJsonObject payload;
    payload.insert(QStringLiteral("upserts"), upserts);
    payload.insert(QStringLiteral("removals"), removals);
    m_gateway.broadcast(QJsonObject{{"type", "trackDelta"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(m_revision)}, {"payload", payload}});
}

} // namespace core::web
