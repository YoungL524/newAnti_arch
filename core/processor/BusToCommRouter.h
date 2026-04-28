#pragma once

#include <QObject>
#include <QMap>
#include <cstdint>
#include "GenericPackets.h"
#include "StructureReceive.h"

class CommunicationManager;

/**
 * @file BusToCommRouter.h
 * @brief 总线 → 通信层反向路由器（阶段 B）
 * @details 订阅 SystemBus 中以"设备"为目标的数据包，按载荷类型解包并调用
 *          `CommunicationManager::sendTo` 发送到对应端点/目标。
 *
 * 本类作为普通 QObject 运行在主事件循环中，符合 Qt 的推荐用法。
 *
 * 路由规则：
 * - 按订阅目标 `DEVICE_RADAR_NH_KU_1` 接收所有发给雷达的反向指令；
 * - 按 `payloadType` 决定发到哪个 endpoint + target（可用 registerRoute 扩展）；
 * - 默认：workStandby → (radar_cpu, workmode)，其它按 rawData 直接透传到相同通道。
 */
class BusToCommRouter : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 路由目标：端点 + 子目标名
     */
    struct Route {
        QString endpointId;   ///< 对应 endpoints.json 中的 id
        QString targetName;   ///< 端点下的子目标 key（如 "workmode"）
    };

    /**
     * @brief 构造函数
     * @param commManager 通信管理器（非持有，生命周期需覆盖本对象）
     * @param parent Qt parent
     */
    explicit BusToCommRouter(CommunicationManager* commManager,
                             QObject* parent = nullptr);

    ~BusToCommRouter() override;

    /**
     * @brief 注册一条载荷类型 → 路由 的映射
     * @param payloadType 载荷类型常量（见 GenericPackets.h 的 PAYLOAD_TYPE_* ）
     * @param route 发送路由
     * @details 多次调用同一 payloadType 会覆盖旧路由。
     */
    void registerRoute(uint16_t payloadType, const Route& route);

    /**
     * @brief 设置默认路由（未注册的 payloadType 走此路由）
     */
    void setDefaultRoute(const Route& route);

    /**
     * @brief 订阅 SystemBus 的反向数据流
     * @param targetDevice 目标设备类型（默认 DEVICE_RADAR_NH_KU_1）
     * @return 订阅 ID（0 表示失败）
     * @details 构造函数不自动订阅；由 AppController 显式调用，方便控制生命周期。
     */
    uint32_t subscribe(uint16_t targetDevice = DEVICE_RADAR_NH_KU_1);

    /**
     * @brief 取消订阅（析构时自动调用）
     */
    void unsubscribe();

    /**
     * @brief 累计已路由的包数（用于诊断）
     */
    uint64_t routedCount() const { return m_routedCount; }

private slots:
    /**
     * @brief 在本对象所属线程处理 DataPacket
     * @details 由 SystemBus 回调通过 Qt::QueuedConnection 投递过来
     */
    void handlePacket(DataPacket packet);

private:
    /**
     * @brief 根据 payloadType 提取待发送的字节流
     * @return 成功返回待发送数据；失败返回空数组
     */
    QByteArray extractBytes(const DataPacket& packet) const;

    /**
     * @brief 查询路由
     */
    Route resolveRoute(uint16_t payloadType) const;

private:
    CommunicationManager* m_commManager = nullptr;  ///< 通信管理器（非持有）
    QMap<uint16_t, Route> m_routes;                  ///< payloadType → Route
    Route m_defaultRoute;                            ///< 默认路由
    uint32_t m_subscriptionId = 0;                   ///< SystemBus 订阅 ID
    uint64_t m_routedCount = 0;                      ///< 路由成功的包数
};
