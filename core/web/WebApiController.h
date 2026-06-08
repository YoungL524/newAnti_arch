#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

#include "AppTypes.h"

namespace web { class WebService; }
class RadarWorkModeController;
class DeviceRegistry;

/**
 * @brief Web API 胶水层：REST 路由注册 + WS 消息拼装。
 *
 * 负责将 core 业务层的数据（工作模式状态、航迹、日志、健康报告）
 * 拼装为 JSON 并通过 WebService 发布。
 * 不管理生命周期，交给 AppController 组合。
 */
class WebApiController : public QObject {
    Q_OBJECT
public:
    using HealthProvider = std::function<AppHealth()>;

    WebApiController(web::WebService* ws,
                     RadarWorkModeController* wmc,
                     DeviceRegistry* deviceRegistry,
                     HealthProvider healthProvider,
                     QObject* parent = nullptr);
    ~WebApiController() override;

    /// 注册 REST 路由（/api/status, /api/logs, /api/start, /api/stop）
    void registerRestRoutes();

    /// 连接 WS 推送（SystemBus 航迹、Logger 日志、1s 状态定时器）
    void connectWebPush();

    /// 关闭时释放订阅（取消 SystemBus 订阅、清 Logger 回调）
    void shutdown();

private:
    QJsonArray buildDeviceStatusArray() const;

    /// Device id → radar_cpu controller mapping helpers
    bool mapsToRadarCpu(const QString& deviceId, const QString& endpointId) const;
    QSet<QString> radarCpuDeviceIds() const;

    web::WebService*           m_ws = nullptr;
    RadarWorkModeController*   m_wmc = nullptr;
    DeviceRegistry*            m_deviceReg = nullptr;
    HealthProvider             m_health;
    qint64                     m_lastStartMs = 0;   ///< 上次开始采集时间戳（节流）
    qint64                     m_lastStopMs = 0;    ///< 上次停止采集时间戳（节流）
    uint32_t                   m_trackSubId = 0;
};
