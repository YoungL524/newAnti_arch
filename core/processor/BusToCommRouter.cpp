#include "BusToCommRouter.h"
#include "CommunicationManager.h"
#include "SystemBus.h"

#include <QDateTime>
#include <QDebug>
#include <cstring>

/**
 * @brief 保存通信门面并注册 DataPacket Qt 元类型，为总线线程到 QObject 线程的排队投递做准备。
 * @details 路由表由 AppController 从 processor.json 统一装配，构造函数不偷偷加入业务路由。
 */
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

/** @brief 先取消总线订阅，保证析构后不会再有捕获 this 的回调进入队列。 */
BusToCommRouter::~BusToCommRouter()
{
    unsubscribe();
    qDebug() << QStringLiteral("[BusToCommRouter] 销毁：共路由 %1 个包").arg(m_routedCount);
}

/**
 * @brief 新增或覆盖一个 command 到发送端点的显式路由。
 */
void BusToCommRouter::registerRoute(uint16_t payloadType, const Route& route)
{
    m_routes.insert(payloadType, route);
    qDebug() << QStringLiteral("[BusToCommRouter] 注册路由: payloadType=%1 → %2/%3")
                .arg(payloadType).arg(route.endpointId, route.targetName);
}

/**
 * @brief 设置未命中显式路由时使用的兜底端点和目标名。
 */
void BusToCommRouter::setDefaultRoute(const Route& route)
{
    m_defaultRoute = route;
}

/**
 * @brief 订阅发往指定设备类型的反向消息，并把处理切回本对象线程。
 * @details 总线回调只复制 DataPacket 并 queued invoke，不直接访问 QUdpSocket；重复调用返回已有订阅 ID。
 */
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

/**
 * @brief 取消当前反向流订阅并清零句柄，使后续 subscribe() 可以重新登记。
 */
void BusToCommRouter::unsubscribe()
{
    if (m_subscriptionId == 0) return;
    SystemBus::getInstance().unsubscribe(m_subscriptionId);
    qDebug() << QStringLiteral("[BusToCommRouter] 已取消订阅 subId=%1").arg(m_subscriptionId);
    m_subscriptionId = 0;
}

/**
 * @brief 优先返回 command 的显式路由，否则返回装配阶段设置的默认路由。
 */
BusToCommRouter::Route BusToCommRouter::resolveRoute(uint16_t payloadType) const
{
    const auto it = m_routes.find(payloadType);
    return (it != m_routes.end()) ? it.value() : m_defaultRoute;
}

/**
 * @brief 把已登记协议值载荷编码为结构体原始布局，或显式透传 QByteArray。
 * @details workStandby/TargetType 必须通过命令字和 variant 双重检查；未知业务对象不能被误当作原始字节发送。
 */
QByteArray BusToCommRouter::extractBytes(const DataPacket& packet) const
{
    // 按实际 payloadType 提取；均采用模板函数 unpackDataFromPayload 保证类型一致
    switch (packet.commandValue()) {
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
    default:
        // 只有显式 QByteArray 载荷才允许原样透传。
        if (const auto raw = std::get_if<QByteArray>(&packet.payload))
            return *raw;
        break;
    }
    return QByteArray();
}

/**
 * @brief 在 QObject 线程完成方向过滤、载荷编码、路由选择和 UDP 发送。
 * @details sourceType 属于雷达表示上行回显，必须过滤以防数据回环。只有 sendTo 成功才增加 routedCount
 *          和下行延时样本；无法编码或发送失败都保留明确日志且不伪造成功指标。
 */
void BusToCommRouter::handlePacket(const DataPacket& packet)
{
    if (!m_commManager) return;

    // 只处理 UI → 设备 方向
    if (isRadarDevice(packet.sourceType)) {
        return;
    }

    const QByteArray bytes = extractBytes(packet);
    if (bytes.isEmpty()) {
        qWarning() << QStringLiteral("[BusToCommRouter] 解包失败: command=%1")
                      .arg(packet.commandValue());
        return;
    }

    const Route r = resolveRoute(static_cast<uint16_t>(packet.commandValue()));
    const bool ok = m_commManager->sendTo(r.endpointId, r.targetName, bytes);
    if (ok) {
        ++m_routedCount;

        // 下行链路延时：从 DataPacket 创建（packet.timeStamp）到 UDP 发出
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 latencyMs = nowMs - static_cast<qint64>(packet.timeStamp);
        if (latencyMs >= 0) {
            m_downlinkLatency.addSample(latencyMs);
        }

        // 每 200 包打印一次延时概况
        if (m_downlinkLatency.sampleCount % 200 == 0) {
            qDebug() << QStringLiteral("[BusToCommRouter] 下行延时: avg=%1ms min=%2ms max=%3ms [%4 samples]")
                        .arg(m_downlinkLatency.avgMs)
                        .arg(m_downlinkLatency.minMs)
                        .arg(m_downlinkLatency.maxMs)
                        .arg(m_downlinkLatency.sampleCount);
        }

        qDebug() << QStringLiteral("[BusToCommRouter] 路由成功: command=%1, %2B → %3/%4, seq=%5")
                    .arg(packet.commandValue())
                    .arg(bytes.size())
                    .arg(r.endpointId, r.targetName)
                    .arg(packet.packetSequence);
    } else {
        qWarning() << QStringLiteral("[BusToCommRouter] 发送失败: %1/%2")
                      .arg(r.endpointId, r.targetName);
    }
}
