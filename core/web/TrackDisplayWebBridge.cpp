#include "TrackDisplayWebBridge.h"
#include "WebService.h"
#include "bus/SystemBus.h"
#include "TrackTypes.h"
#include "metrics/MonotonicClock.h"
#include "metrics/PipelineLatencyMonitor.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QWebSocket>
#include <QDateTime>

/**
 * @brief 绑定 Web 服务，并为新 WebSocket 客户端建立初始快照发送入口。
 * @param ws 非拥有型 Web 服务指针；应比桥接器存活更久。
 * @param parent Qt 父对象。
 */
TrackDisplayWebBridge::TrackDisplayWebBridge(web::WebService* ws, QObject* parent)
    : QObject(parent)
    , m_ws(ws)
{
    if (m_ws) {
        connect(m_ws, &web::WebService::webSocketConnected,
                this, &TrackDisplayWebBridge::sendSnapshotToClient);
    }
}

TrackDisplayWebBridge::~TrackDisplayWebBridge()
{
    stop();
}

/**
 * @brief 替换非拥有延时监控器，不改变桥维护的窗口或 revision。
 */
void TrackDisplayWebBridge::setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor)
{
    m_latencyMonitor = latencyMonitor;
}

/**
 * @brief 订阅显示更新并监听新 WebSocket 连接；重复调用不重复登记。
 * @details 总线回调可能来自发布线程，因此只捕获 QPointer 并把更新排入桥接器所属线程；实际 JSON 构造和网络发送不阻塞总线发布者。
 */
void TrackDisplayWebBridge::start()
{
    if (m_subId != 0 || !m_ws)
        return;

    m_active.store(true, std::memory_order_release);
    QPointer<TrackDisplayWebBridge> guard(this);
    m_subId = SystemBus::getInstance().subscribeBySource(
        SOURCE_TRACK_DISPLAY,
        [guard](const DataPacket* packet) {
            if (guard && packet)
                guard->onDataPacket(*packet);
        });

    if (m_subId == 0) {
        m_active.store(false, std::memory_order_release);
        qWarning() << "[TrackDisplayWebBridge] 订阅 SystemBus 失败";
    } else {
        qDebug() << "[TrackDisplayWebBridge] 已订阅 SOURCE_TRACK_DISPLAY";
    }
}

/**
 * @brief 停止接收新的航迹显示更新并注销总线订阅。
 * @details 先清除 active 标志，使已经排队的回调失效，再按订阅 ID 注销；未启动时调用不会产生副作用。
 */
void TrackDisplayWebBridge::stop()
{
    m_active.store(false, std::memory_order_release);
    if (m_subId == 0)
        return;
    SystemBus::getInstance().unsubscribe(m_subId);
    m_subId = 0;
}

/**
 * @brief 校验显示更新、检查修订连续性并排队到 Web 线程维护窗口和广播。
 * @details 按所属模块契约完成校验、状态转换与后续分发，并隔离无效输入。
 */
void TrackDisplayWebBridge::onDataPacket(const DataPacket& packet)
{
    if (!m_active.load(std::memory_order_acquire))
        return;

    if (packet.command != DataCommand::TrackDisplayUpdate)
        return;
    auto updatePtr = packet.sharedPayloadAs<TrackDisplayUpdate>();
    if (!updatePtr)
        return;

    const qint64 queuedSteadyNs = monotonicNowNs();
    if (m_latencyMonitor && updatePtr->publishedSteadyNs > 0) {
        m_latencyMonitor->recordNs(PipelineLatencyStage::DisplayUpdateBusDispatch,
                                   queuedSteadyNs - updatePtr->publishedSteadyNs);
    }

    QPointer<TrackDisplayWebBridge> guard(this);
    QMetaObject::invokeMethod(this, [guard, updatePtr, queuedSteadyNs]() {
        if (guard && guard->m_active.load(std::memory_order_acquire))
            guard->pushUpdate(*updatePtr, queuedSteadyNs);
    }, Qt::QueuedConnection);
}

/**
 * @brief 将显示点的几何、样式和交互字段完整转换为 WebSocket JSON。
 */
QJsonObject TrackDisplayWebBridge::pointToJson(const TrackDisplayPoint& dp)
{
    QJsonObject t;
    t[QStringLiteral("trackId")]     = static_cast<qint64>(dp.trackId);
    t[QStringLiteral("deviceId")]    = dp.deviceId;
    t[QStringLiteral("taskId")]      = dp.taskId;
    t[QStringLiteral("timestamp")]   = dp.timestamp;
    t[QStringLiteral("range")]       = dp.range;
    t[QStringLiteral("azimuth")]     = dp.azimuth;
    t[QStringLiteral("elevation")]   = dp.elevation;
    t[QStringLiteral("radialSpeed")] = dp.radialSpeed;
    t[QStringLiteral("altitude")]    = dp.altitude;
    t[QStringLiteral("pointIndex")]  = static_cast<qint64>(dp.pointIndex);
    if (dp.pointIndex >= 3 || dp.interaction.selected || dp.interaction.trackingEnabled) {
        QJsonObject style;
        style[QStringLiteral("color")]   = dp.style.color;
        style[QStringLiteral("size")]    = dp.style.size;
        style[QStringLiteral("opacity")] = dp.style.opacity;
        t[QStringLiteral("style")] = style;
    }
    if (dp.interaction.selected || dp.interaction.trackingEnabled) {
        QJsonObject interaction;
        interaction[QStringLiteral("selected")]        = dp.interaction.selected;
        interaction[QStringLiteral("trackingEnabled")] = dp.interaction.trackingEnabled;
        t[QStringLiteral("interaction")] = interaction;
    }
    return t;
}

/**
 * @brief 将删除键编码为 deviceId/trackId/pointIndex，避免删除整条航迹。
 */
QJsonObject TrackDisplayWebBridge::keyToJson(const TrackDisplayPointKey& key)
{
    return QJsonObject{
        {QStringLiteral("deviceId"), key.deviceId},
        {QStringLiteral("trackId"), static_cast<qint64>(key.trackId)},
        {QStringLiteral("pointIndex"), static_cast<qint64>(key.pointIndex)}
    };
}

/**
 * @brief 从当前权威窗口生成仅发给单个新连接的 snapshot 消息。
 */
QJsonObject TrackDisplayWebBridge::snapshotMessage() const
{
    QJsonArray tracks;
    for (auto it = m_points.constBegin(); it != m_points.constEnd(); ++it)
        tracks.append(pointToJson(it.value()));
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("tracks")},
        {QStringLiteral("mode"), QStringLiteral("snapshot")},
        {QStringLiteral("revision"), static_cast<qint64>(m_revision)},
        {QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("tracks"), tracks}
    };
}

/**
 * @brief 向刚建立连接的客户端发送当前有界航迹状态快照。
 * @param client 新连接的 WebSocket 客户端。
 * @details 仅在桥接器处于活动状态时发送；后续变化通过增量更新和删除消息传递，避免反复发送不断增长的全量集合。
 */
void TrackDisplayWebBridge::sendSnapshotToClient(QWebSocket* client)
{
    if (m_ws && m_active.load(std::memory_order_acquire))
        m_ws->sendToClient(client, snapshotMessage());
}

void TrackDisplayWebBridge::pushUpdate(const TrackDisplayUpdate& update,
                                       qint64 queuedSteadyNs)
{
    if (!m_ws)
        return;

    const qint64 startedSteadyNs = monotonicNowNs();
    if (m_latencyMonitor) {
        m_latencyMonitor->recordNs(PipelineLatencyStage::WebQueue,
                                   startedSteadyNs - queuedSteadyNs);
    }

    if (update.mode == TrackDisplayUpdateMode::Delta
        && m_revision != 0 && update.revision != m_revision + 1) {
        qWarning() << "[TrackDisplayWebBridge] 显示增量 revision 不连续，申请全量重同步:"
                   << m_revision << "->" << update.revision;
        emit resyncRequired();
        return;
    }

    if (update.mode == TrackDisplayUpdateMode::Snapshot) {
        m_points.clear();
    } else {
        for (const auto& key : update.removals)
            m_points.remove(key);
    }
    for (const auto& point : update.upserts) {
        m_points.insert({point.deviceId, point.trackId, point.pointIndex}, point);
    }
    m_revision = update.revision;

    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("tracks");
    msg[QStringLiteral("mode")] = update.mode == TrackDisplayUpdateMode::Snapshot
        ? QStringLiteral("snapshot") : QStringLiteral("delta");
    msg[QStringLiteral("revision")] = static_cast<qint64>(update.revision);
    msg[QStringLiteral("timestamp")] = update.timestamp;

    if (update.mode == TrackDisplayUpdateMode::Snapshot) {
        QJsonArray tracks;
        for (const auto& point : update.upserts)
            tracks.append(pointToJson(point));
        msg[QStringLiteral("tracks")] = tracks;
    } else {
        QJsonArray upserts;
        for (const auto& point : update.upserts)
            upserts.append(pointToJson(point));
        QJsonArray removals;
        for (const auto& key : update.removals)
            removals.append(keyToJson(key));
        msg[QStringLiteral("upserts")] = upserts;
        msg[QStringLiteral("removals")] = removals;
    }

    const int bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact).size();
    if (update.mode == TrackDisplayUpdateMode::Delta && bytes > 48 * 1024) {
        static qint64 lastWarnMs = 0;
        const qint64 now = update.timestamp;
        if (now - lastWarnMs > 5000) {
            lastWarnMs = now;
            qWarning() << "[TrackDisplayWebBridge] WS tracks delta 超过 48KiB:"
                       << bytes << "bytes," << update.upserts.size() << "upserts,"
                       << update.removals.size() << "removals";
        }
    }
    m_ws->broadcast(msg);
    if (m_latencyMonitor) {
        const qint64 completedSteadyNs = monotonicNowNs();
        m_latencyMonitor->recordNs(PipelineLatencyStage::WebPrepareAndBroadcast,
                                   completedSteadyNs - startedSteadyNs);
        if (update.latestIngressSteadyNs > 0) {
            const qint64 ingressToWebSendNs = completedSteadyNs - update.latestIngressSteadyNs;
            m_latencyMonitor->recordNs(PipelineLatencyStage::IngressToWebSend,
                                       ingressToWebSendNs);

            if (update.latestDisplayReadySteadyNs > 0
                && update.buildStartedSteadyNs >= update.latestDisplayReadySteadyNs) {
                const qint64 updateWaitNs = update.buildStartedSteadyNs
                    - update.latestDisplayReadySteadyNs;
                m_latencyMonitor->recordNs(PipelineLatencyStage::DisplayUpdateWait,
                                           updateWaitNs);
                m_latencyMonitor->recordNs(PipelineLatencyStage::ActiveBackendToWebSend,
                                           ingressToWebSendNs - updateWaitNs);
            }
        }
    }
}
