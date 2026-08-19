#include "UdpEndpoint.h"
#include <QDateTime>
#include <QDebug>
#include "metrics/MonotonicClock.h"

/**
 * @brief 创建未绑定的 UDP 端点，延迟到 open() 才分配 QUdpSocket。
 */
UdpEndpoint::UdpEndpoint(const EndpointConfig& config, QObject* parent)
    : Endpoint(config, parent)
{
}

/** @brief 关闭仍打开的 socket；close() 的幂等语义允许显式停止后再次执行。 */
UdpEndpoint::~UdpEndpoint()
{
    close();
}

/**
 * @brief 绑定配置的本地地址和端口，并接通 readyRead 接收路径。
 * @details socket 只有绑定成功后才连接信号、解析目标并置 m_running。绑定失败会立即删除临时 socket，
 *          保证对象仍可修改环境后重试 open()，不会留下半初始化网络资源。
 */
bool UdpEndpoint::open()
{
    if (m_socket) {
        return true;
    }

    m_socket = new QUdpSocket(this);

    if (!m_socket->bind(QHostAddress(m_config.localAddress), m_config.localPort)) {
        emit errorOccurred(QStringLiteral("UDP绑定失败: %1:%2 - %3")
                           .arg(m_config.localAddress)
                           .arg(m_config.localPort)
                           .arg(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &UdpEndpoint::onReadyRead);

    resolveTargets();
    m_running = true;

    qDebug() << QStringLiteral("[UdpEndpoint] '%1' 已打开 - 绑定 %2:%3")
                .arg(m_config.id)
                .arg(m_config.localAddress)
                .arg(m_config.localPort);
    return true;
}

/**
 * @brief 停止接收并安排 socket 在所属 Qt 线程安全销毁。
 * @details 先清除运行标志，再 close 并 deleteLater；立即把指针置空，使后续 send() 明确失败且允许重新 open()。
 */
void UdpEndpoint::close()
{
    m_running = false;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    qDebug() << QStringLiteral("[UdpEndpoint] '%1' 已关闭").arg(m_config.id);
}

/**
 * @brief 以 QUdpSocket 的 BoundState 为权威依据判断端点能否收发 UDP 报文。
 */
bool UdpEndpoint::isOpen() const
{
    return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

/**
 * @brief 向预解析的具名目标发送一个完整 UDP 数据报。
 * @details 未打开或 targetKey 不存在时不触碰 socket；writeDatagram 返回 -1 时转发 Qt 错误。
 */
bool UdpEndpoint::send(const QString& targetKey, const QByteArray& data)
{
    if (!m_socket) {
        emit errorOccurred(QStringLiteral("发送失败: socket 未打开"));
        return false;
    }

    auto it = m_resolvedTargets.find(targetKey);
    if (it == m_resolvedTargets.end()) {
        emit errorOccurred(QStringLiteral("发送失败: 目标 '%1' 不存在").arg(targetKey));
        return false;
    }

    qint64 sent = m_socket->writeDatagram(data, it.value().first, it.value().second);
    if (sent == -1) {
        emit errorOccurred(QStringLiteral("发送失败: %1").arg(m_socket->errorString()));
        return false;
    }
    return true;
}

/**
 * @brief 同步更新持久配置副本和发送热路径使用的地址缓存。
 * @note QHostAddress 仅直接解析数值地址；当前实现不执行异步 DNS 查询。
 */
void UdpEndpoint::addTarget(const QString& key, const QString& address, quint16 port)
{
    m_config.targets[key] = { address, port };
    m_resolvedTargets[key] = { QHostAddress(address), port };
}

/**
 * @brief 排空本轮所有 UDP 报文并在读取后立即记录双时间基准。
 * @details Windows 可能因 ICMP Port Unreachable 产生大小非正的虚假 readyRead，必须读取一次清除 socket 状态，
 *          但不能向上游传播。Unix 毫秒用于业务关联，单调纳秒用于排除系统校时影响的链路延时统计。
 */
void UdpEndpoint::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        const qint64 pending = m_socket->pendingDatagramSize();
        // 空/无效数据报：常见于 Windows loopback 发到无监听端口后
        // 内核回 ICMP Port Unreachable (WSAECONNRESET)，触发虚假 readyRead。
        // 必须读空队列才能复位，但不对外传播。
        if (pending <= 0) {
            char dummy;
            m_socket->readDatagram(&dummy, 0);
            continue;
        }

        QByteArray datagram;
        datagram.resize(static_cast<int>(pending));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 actual = m_socket->readDatagram(datagram.data(), datagram.size(),
                                                      &sender, &senderPort);
        if (actual <= 0) {
            continue;
        }
        datagram.resize(static_cast<int>(actual));
        // 在 readDatagram 之后立即捕获时间戳，排除后续 Qt 信号投递和处理的影响
        const qint64 receivedMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 receivedSteadyNs = monotonicNowNs();
        emit dataReceived(datagram, receivedMs, receivedSteadyNs);
    }
}

/**
 * @brief 从当前 EndpointConfig 全量重建 targetKey 到地址/端口的发送缓存。
 */
void UdpEndpoint::resolveTargets()
{
    m_resolvedTargets.clear();
    for (auto it = m_config.targets.constBegin(); it != m_config.targets.constEnd(); ++it) {
        m_resolvedTargets[it.key()] = { QHostAddress(it.value().address), it.value().port };
    }
}
