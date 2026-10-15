#include "core/web/TrackWebSocketService.h"
#include "core/web/WebSocketGateway.h"

#include <QJsonArray>
#include <QDateTime>
#include <QWebSocket>

#include <limits>
#include <stdexcept>

namespace core::web {

/**
 * @brief 验证批次上限、预分配 ScratchBuffer，并接通航迹与采集状态 Doorbell。
 */
TrackWebSocketService::TrackWebSocketService(
    const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
    const core::domain::LatestState<core::model::DeviceState> &deviceState,
    core::execution::ExecutionLane &executionLane,
    WebSocketGateway &gateway,
    const core::model::StableIdRegistry &stableIds,
    qsizetype maxRecordsPerTurn,
    QObject *parent)
    : QObject(parent)
    , m_trackTopic(trackTopic)
    , m_deviceState(deviceState)
    , m_executionLane(executionLane)
    , m_gateway(gateway)
    , m_stableIds(stableIds)
    , m_scratch(maxRecordsPerTurn)
    , m_executionSlot(executionLane.acquireSlot())
{
    if (maxRecordsPerTurn <= 0) {
        m_executionLane.releaseSlot(m_executionSlot);
        throw std::invalid_argument("TrackWebSocketService batch limit must be greater than zero");
    }
    QObject::connect(trackTopic.doorbell(), &core::domain::TopicDoorbell::advanced,
                     this, &TrackWebSocketService::onTrackTopicAdvanced, Qt::DirectConnection);
    QObject::connect(deviceState.doorbell(), &core::domain::TopicDoorbell::advanced,
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
 * @brief 航迹或设备状态 Doorbell 只向线程安全 Lane 置位，不在通知线程访问 Web 状态。
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
 * @brief 先核对当前采集身份，再读取固定批次；会话变化和 gap 均重建 Web 显示态。
 * @details Gap/Ahead 时旧最终状态无法证明连续，故清空后从当前驻留窗口重读并以 snapshot 通知客户端；正常
 * 批次只发送 upsert/removal delta。锁已在 HistoryTopic 返回前释放，JSON 和 socket 工作均不占 Topic 锁。
 */
bool TrackWebSocketService::consumeOneBatch()
{
    const auto deviceRecord = m_deviceState.latest();
    core::model::DeviceId currentDeviceId;
    std::array<core::model::AcquisitionId, 4> currentAcquisitionIds {};
    if (deviceRecord) {
        for (const auto &device : deviceRecord->sample->value.devices) {
            if (device.operatingState == core::model::DeviceOperatingState::Collecting) {
                currentDeviceId = device.deviceId;
                currentAcquisitionIds = device.faceAcquisitionIds;
                break;
            }
        }
    }
    bool rebuild = currentDeviceId != m_currentDeviceId
        || currentAcquisitionIds != m_currentAcquisitionIds;
    if (rebuild) {
        m_currentDeviceId = currentDeviceId;
        m_currentAcquisitionIds = currentAcquisitionIds;
        m_trails.clear();
    }
    if (!currentDeviceId.isSet()) {
        m_cursor = m_trackTopic.range().newest;
        if (rebuild) {
            ++m_revision;
            m_gateway.broadcast(QJsonObject{{"type", "trackSnapshot"},
                {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
                {"revision", QString::number(m_revision)},
                {"payload", QJsonObject{{"tracks", QJsonArray{}}}}});
        }
        return false;
    }
    auto result = m_trackTopic.readAfter(m_cursor, m_scratch);
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
    QJsonArray retirements;
    for (qsizetype index = 0; index < result.count; ++index) {
        const auto &record = m_scratch[index];
        applyRecord(record, upserts, removals, retirements);
        m_cursor = record.sequence;
    }
    trimDisplayTargets(upserts, removals, retirements);

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
    } else if (!upserts.isEmpty() || !removals.isEmpty() || !retirements.isEmpty()) {
        broadcastDelta(upserts, removals, retirements);
    }
    return result.hasMore;
}

/**
 * @brief 将当前会话的存在 Sample 转为显示点，或依据消亡 Sample 删除该 TrackKey 的近期尾迹。
 * @details 点序号接近 quint32 上限时清空本航迹，防止浏览器
 * 数值回绕破坏排序。packFlag 为 0 时明确输出消亡键，不建立不存在目标的空派生状态。
 */
void TrackWebSocketService::applyRecord(
    const core::domain::TopicRecord<core::model::TrackPacket> &record,
    QJsonArray &upserts,
    QJsonArray &removals,
    QJsonArray &retirements)
{
    const auto &sample = record.sample;
    if (sample.value.deviceId != m_currentDeviceId
        || sample.value.faceIndex >= m_currentAcquisitionIds.size()
        || sample.value.acquisitionId != m_currentAcquisitionIds[sample.value.faceIndex]) return;
    if (!hasStableIdentity(sample.value)) return;
    const core::model::TrackKey key = core::model::makeTrackKey(sample.value);
    auto iterator = m_trails.find(key);
    if (sample.value.packFlag == 0U) {
        if (iterator != m_trails.end()) {
            const TrackTrail &trail = iterator.value();
            retirements.append(pointKeyJson(trail.points[0]));
            for (qsizetype index = 0; index < trail.size; ++index) {
                removals.append(pointKeyJson(trail.points[static_cast<std::size_t>(index)]));
            }
            m_trails.erase(iterator);
        }
        return;
    }
    if (iterator == m_trails.end()) iterator = m_trails.insert(key, TrackTrail{});

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
    trail.lastSequence = record.sequence;
    trail.points[static_cast<std::size_t>(trail.nextSlot)] = point;
    trail.nextSlot = (trail.nextSlot + 1) % static_cast<qsizetype>(trail.points.size());
    trail.size = qMin(trail.size + 1, static_cast<qsizetype>(trail.points.size()));
    /** @brief 同批消亡后重建可能复用点序号，删除仅约束先前版本，不能覆盖较新的 upsert。 */
    const auto newKey = pointKeyJson(point);
    QJsonArray remainingRemovals;
    for (const auto &value : removals) if (value.toObject() != newKey) remainingRemovals.append(value);
    removals = remainingRemovals;
    upserts.append(pointJson(point));
}

/**
 * @brief 只淘汰最久未更新的显示目标，不修改 Topic 或归档；本批 upsert 必须同步移除以免客户端保留幽灵目标。
 */
void TrackWebSocketService::trimDisplayTargets(QJsonArray &upserts, QJsonArray &removals, QJsonArray &retirements)
{
    while (m_trails.size() > 256) {
        auto oldest = m_trails.begin();
        for (auto it = m_trails.begin(); it != m_trails.end(); ++it)
            if (it->lastSequence < oldest->lastSequence) oldest = it;
        const auto identity = pointKeyJson(oldest->points[0]);
        retirements.append(identity);
        for (qsizetype i = 0; i < oldest->size; ++i) removals.append(pointKeyJson(oldest->points[static_cast<std::size_t>(i)]));
        QJsonArray retained;
        for (const auto &value : upserts) {
            const auto point = value.toObject();
            if (point.value("deviceId") != identity.value("deviceId")
                || point.value("acquisitionId") != identity.value("acquisitionId")
                || point.value("faceIndex") != identity.value("faceIndex")
                || point.value("trackId") != identity.value("trackId")) retained.append(value);
        }
        upserts = retained;
        m_trails.erase(oldest);
    }
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
 * @brief 新连接仅接收仍匹配当前 DeviceState 会话的近期点，避免 Lane 尚未清理时泄露旧会话。
 */
void TrackWebSocketService::sendSnapshot(QWebSocket &client) const
{
    QJsonArray tracks;
    bool matchesCurrentSession = false;
    const auto state = m_deviceState.latest();
    if (state) {
        for (const auto &device : state->sample->value.devices) {
            if (device.operatingState != core::model::DeviceOperatingState::Collecting) continue;
            matchesCurrentSession = device.deviceId == m_currentDeviceId
                && device.faceAcquisitionIds == m_currentAcquisitionIds;
            break;
        }
    }
    if (matchesCurrentSession) {
        for (auto iterator = m_trails.constBegin(); iterator != m_trails.constEnd(); ++iterator) {
            const TrackTrail &trail = iterator.value();
            for (qsizetype index = 0; index < trail.size; ++index) {
                tracks.append(pointJson(trail.points[static_cast<std::size_t>(index)]));
            }
        }
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("tracks"), tracks);
    m_gateway.send(client, QJsonObject{{"type", "trackSnapshot"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(m_revision)}, {"payload", payload}});
}

/**
 * @brief 为本批变化推进一次 revision；目标消亡键与近期点覆盖键分开传输。
 */
void TrackWebSocketService::broadcastDelta(const QJsonArray &upserts, const QJsonArray &removals,
                                           const QJsonArray &retirements)
{
    ++m_revision;
    QJsonObject payload;
    payload.insert(QStringLiteral("upserts"), upserts);
    payload.insert(QStringLiteral("removals"), removals);
    payload.insert(QStringLiteral("retirements"), retirements);
    m_gateway.broadcast(QJsonObject{{"type", "trackDelta"},
        {"sentUtcMs", QDateTime::currentMSecsSinceEpoch()},
        {"revision", QString::number(m_revision)}, {"payload", payload}});
}

} // namespace core::web
