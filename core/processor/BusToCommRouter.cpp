#include "BusToCommRouter.h"
#include "CommunicationManager.h"
#include "SystemBus.h"

#include <QDebug>
#include <cstring>

BusToCommRouter::BusToCommRouter(CommunicationManager* commManager, QObject* parent)
    : QObject(parent)
    , m_commManager(commManager)
    , m_defaultRoute{ QStringLiteral("radar_cpu"), QStringLiteral("workmode") }
{
    Q_ASSERT(m_commManager != nullptr);

    // 注册 Qt 元类型，使 DataPacket 可通过 Qt::QueuedConnection 传递。
    // 多次调用安全；若 SystemBus 已注册则此调用只是计数增长。
    qRegisterMetaType<DataPacket>("DataPacket");

    // 路由表由 AppController 统一装配（从 processor.json 或默认值），
    // 此处不预注册路由，避免与装配器的 registerDefaultRoutes() 重复。
}

BusToCommRouter::~BusToCommRouter()
{
    unsubscribe();
    qDebug() << QStringLiteral("[BusToCommRouter] 销毁：共路由 %1 个包").arg(m_routedCount);
}

void BusToCommRouter::registerRoute(uint16_t payloadType, const Route& route)
{
    m_routes.insert(payloadType, route);
    qDebug() << QStringLiteral("[BusToCommRouter] 注册路由: payloadType=%1 → %2/%3")
                .arg(payloadType).arg(route.endpointId, route.targetName);
}

void BusToCommRouter::setDefaultRoute(const Route& route)
{
    m_defaultRoute = route;
}

uint32_t BusToCommRouter::subscribe(uint16_t targetDevice)
{
    if (m_subscriptionId != 0) {
        return m_subscriptionId;
    }

    m_subscriptionId = SystemBus::getInstance().subscribeByTarget(
        targetDevice,
        [this](const DataPacket* pkt) {
            if (!pkt) return;
            // 回调运行在 bus 工作线程；通过事件循环切到本对象所在线程
            DataPacket copy = *pkt;
            QMetaObject::invokeMethod(this, "handlePacket",
                                      Qt::QueuedConnection,
                                      Q_ARG(DataPacket, copy));
        });

    if (m_subscriptionId == 0) {
        qCritical() << QStringLiteral("[BusToCommRouter] 订阅 SystemBus 失败（target=%1）")
                       .arg(targetDevice);
    } else {
        qDebug() << QStringLiteral("[BusToCommRouter] 已订阅反向流: target=%1, subId=%2")
                    .arg(targetDevice).arg(m_subscriptionId);
    }
    return m_subscriptionId;
}

void BusToCommRouter::unsubscribe()
{
    if (m_subscriptionId == 0) return;
    SystemBus::getInstance().unsubscribe(m_subscriptionId);
    qDebug() << QStringLiteral("[BusToCommRouter] 已取消订阅 subId=%1").arg(m_subscriptionId);
    m_subscriptionId = 0;
}

BusToCommRouter::Route BusToCommRouter::resolveRoute(uint16_t payloadType) const
{
    const auto it = m_routes.find(payloadType);
    return (it != m_routes.end()) ? it.value() : m_defaultRoute;
}

QByteArray BusToCommRouter::extractBytes(const DataPacket& packet) const
{
    // 按实际 payloadType 提取；均采用模板函数 unpackDataFromPayload 保证类型一致
    switch (packet.payloadType) {
    case PAYLOAD_TYPE_WORK_STANDBY: {
        workStandby d;
        if (unpackDataFromPayload(packet, d)) {
            return QByteArray(reinterpret_cast<const char*>(&d), sizeof(d));
        }
        break;
    }
    case PAYLOAD_TYPE_TARGET_TYPE: {
        TargetType d;
        if (unpackDataFromPayload(packet, d)) {
            return QByteArray(reinterpret_cast<const char*>(&d), sizeof(d));
        }
        break;
    }
    case PAYLOAD_TYPE_GENERAL_PACKAGE: {
        GeneralPackage d;
        if (unpackDataFromPayload(packet, d)) {
            return QByteArray(reinterpret_cast<const char*>(&d), sizeof(d));
        }
        break;
    }
    default:
        // 未知类型：退回到 rawData 原样透传
        if (packet.payloadSize > 0 && packet.payloadSize <= MAX_PAYLOAD_SIZE) {
            return QByteArray(reinterpret_cast<const char*>(packet.payload.rawData),
                              static_cast<int>(packet.payloadSize));
        }
        break;
    }
    return QByteArray();
}

void BusToCommRouter::handlePacket(DataPacket packet)
{
    if (!m_commManager) return;

    // 只处理 UI → 设备 方向。SystemBus 的 subscribeByTarget 已过滤 dispatchTarget，
    // 但保留此防御性检查，避免订阅配置错误时把正向流数据再发一遍。
    // 用 isRadarDevice() 覆盖全部雷达设备枚举，支持多雷达场景。
    if (isRadarDevice(packet.sourceType)) {
        // 来源是雷达自己 → 不是反向流，丢弃
        return;
    }

    const QByteArray bytes = extractBytes(packet);
    if (bytes.isEmpty()) {
        qWarning() << QStringLiteral("[BusToCommRouter] 解包失败: payloadType=%1, size=%2")
                      .arg(packet.payloadType).arg(packet.payloadSize);
        return;
    }

    const Route r = resolveRoute(packet.payloadType);
    const bool ok = m_commManager->sendTo(r.endpointId, r.targetName, bytes);
    if (ok) {
        ++m_routedCount;
        qDebug() << QStringLiteral("[BusToCommRouter] 路由成功: payloadType=%1, %2B → %3/%4, seq=%5")
                    .arg(packet.payloadType)
                    .arg(bytes.size())
                    .arg(r.endpointId, r.targetName)
                    .arg(packet.packetSequence);
    } else {
        qWarning() << QStringLiteral("[BusToCommRouter] 发送失败: %1/%2")
                      .arg(r.endpointId, r.targetName);
    }
}
