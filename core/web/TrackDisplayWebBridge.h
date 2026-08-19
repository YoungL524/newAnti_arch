/**
 * @file TrackDisplayWebBridge.h
 * @brief 把 TrackDisplayUpdate 总线事实转换为 WebSocket 初始快照和增量 DTO。
 * @details 桥维护最新权威显示窗口和修订号：已有连接广播 delta，新连接单独发送当前 snapshot；发现总线修订
 *          不连续时请求 TrackDisplayManager 重发快照。桥不重新计算尾迹或业务可见性。
 */

#pragma once

#include "bus/GenericPackets.h"
#include "bus/ISystemBus.h"
#include "TrackTypes.h"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <atomic>

namespace web { class WebService; }
class QWebSocket;
class PipelineLatencyMonitor;

/**
 * @brief 航迹显示 update 总线事件 → WebSocket DTO 适配器
 * @details 平时广播 delta，并为每个新连接单独发送当前权威 snapshot。
 *          不依赖 TrackDisplayManager 信号，不参与航迹业务状态管理。
 */
class TrackDisplayWebBridge : public QObject {
    Q_OBJECT
public:
    explicit TrackDisplayWebBridge(web::WebService* ws, QObject* parent = nullptr);
    ~TrackDisplayWebBridge() override;

    void start();
    void stop();
    void setLatencyMonitor(PipelineLatencyMonitor* latencyMonitor);

signals:
    void resyncRequired();

private:
    void onDataPacket(const DataPacket& packet);
    void pushUpdate(const TrackDisplayUpdate& update, qint64 queuedSteadyNs);
    void sendSnapshotToClient(QWebSocket* client);
    QJsonObject snapshotMessage() const;
    static QJsonObject pointToJson(const TrackDisplayPoint& point);
    static QJsonObject keyToJson(const TrackDisplayPointKey& key);

    web::WebService* m_ws = nullptr;
    SubscriptionId m_subId = 0;
    std::atomic_bool m_active{false};
    PipelineLatencyMonitor* m_latencyMonitor = nullptr;
    QHash<TrackDisplayPointKey, TrackDisplayPoint> m_points;
    uint64_t m_revision = 0;
};
