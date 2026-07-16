#include "UdpEndpoint.h"
#include <QDateTime>
#include <QDebug>
#include "metrics/MonotonicClock.h"

UdpEndpoint::UdpEndpoint(const EndpointConfig& config, QObject* parent)
    : Endpoint(config, parent)
{
}

UdpEndpoint::~UdpEndpoint()
{
    close();
}

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

bool UdpEndpoint::isOpen() const
{
    return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

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

void UdpEndpoint::addTarget(const QString& key, const QString& address, quint16 port)
{
    m_config.targets[key] = { address, port };
    m_resolvedTargets[key] = { QHostAddress(address), port };
}

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

void UdpEndpoint::resolveTargets()
{
    m_resolvedTargets.clear();
    for (auto it = m_config.targets.constBegin(); it != m_config.targets.constEnd(); ++it) {
        m_resolvedTargets[it.key()] = { QHostAddress(it.value().address), it.value().port };
    }
}
