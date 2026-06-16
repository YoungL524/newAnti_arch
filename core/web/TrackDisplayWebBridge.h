#pragma once

#include "bus/BusEnvelope.h"
#include "bus/ISystemBus.h"
#include <QObject>

namespace web { class WebService; }
struct TrackDisplaySnapshot;

/**
 * @brief 航迹显示 snapshot 总线事件 → WebSocket DTO 适配器
 * @details 订阅 SystemBus 上的 TrackDisplaySnapshot 事件，转换为固定 Web 协议并 broadcast。
 *          不依赖 TrackDisplayManager 信号，不参与航迹业务状态管理。
 */
class TrackDisplayWebBridge : public QObject {
    Q_OBJECT
public:
    explicit TrackDisplayWebBridge(web::WebService* ws, QObject* parent = nullptr);
    ~TrackDisplayWebBridge() override;

    void start();
    void stop();

private:
    void onBusEnvelope(const BusEnvelope& env);
    void pushSnapshot(const TrackDisplaySnapshot& snap);

    web::WebService* m_ws = nullptr;
    SubscriptionId m_subId = 0;
    bool m_active = false;
};
