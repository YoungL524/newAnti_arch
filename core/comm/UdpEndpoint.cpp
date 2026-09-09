#include "core/comm/UdpEndpoint.h"
#include <QDateTime>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <utility>

namespace core::comm {
/**
 * @brief 只建立socket就绪通知，不绑定端口；原始字节使用同步调用，不进入Qt消息队列。
 */
UdpEndpoint::UdpEndpoint(ReceiveHandler handler, QObject *parent)
    : QObject(parent), m_socket(this), m_handler(std::move(handler))
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &UdpEndpoint::drain);
}

/**
 * @brief 使用独占绑定暴露端口冲突；失败不留下半启动状态，也不改变已有绑定。
 */
bool UdpEndpoint::start(const QHostAddress &localAddress, quint16 localPort)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_handler) {
        return fail(QStringLiteral("UDP缺少接收处理函数，不能启动"));
    }
    if (m_active || m_draining || localAddress.isNull()) {
        return fail(QStringLiteral("UDP重复启动、接收中重启或本地地址无效"));
    }
    if (!m_socket.bind(localAddress, localPort, QAbstractSocket::DontShareAddress)) {
        return fail(m_socket.errorString());
    }
    ++m_generation;
    m_active = true;
    m_continuationPending = false;
    m_lastError.clear();
    return true;
}

/**
 * @brief 先使回调代次失效，再关闭socket，阻止已排队的旧续读在重新绑定后执行。
 */
void UdpEndpoint::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_active = false;
    ++m_generation;
    m_continuationPending = false;
    m_socket.close();
}

/**
 * @brief 只提交当前数据报；UDP提交成功不代表设备已收到，更不代表命令ACK成功。
 */
bool UdpEndpoint::send(const QByteArray &bytes, const QHostAddress &targetAddress, quint16 targetPort)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_active || targetAddress.isNull() || targetPort == 0 || bytes.isEmpty()
        || bytes.size() > maxSendPayloadBytes) {
        return fail(QStringLiteral("UDP未绑定或发送目标/长度无效"));
    }
    if (m_socket.writeDatagram(bytes, targetAddress, targetPort) != bytes.size()) {
        return fail(m_socket.errorString());
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 查询socket真实绑定结果，便于启动日志和临时端口联试。
 */
quint16 UdpEndpoint::localPort() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_active ? m_socket.localPort() : 0;
}

/**
 * @brief 返回错误文本副本，调用方不借用端点内部字符串存储。
 */
QString UdpEndpoint::lastError() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_lastError;
}

/**
 * @brief 读取累计超长丢弃量，诊断时不触发socket收取。
 */
quint64 UdpEndpoint::oversizedDatagrams() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_oversized;
}

/**
 * @brief 读取后立即同步交给协议入口；每轮限制64包以避免持续UDP输入饿死同线程定时器。
 * @details 超长包整包丢弃。UTC用于记录、steady用于超时，均在readDatagram成功后采集。
 * 零字节UDP仍交给协议层校验；剩余数据只有一个续读事件，空闲时没有定时轮询。
 */
void UdpEndpoint::drain()
{
    if (!m_active || m_draining || m_continuationPending) {
        return;
    }
    m_draining = true;
    for (int count = 0; count < 64 && m_active && m_socket.hasPendingDatagrams(); ++count) {
        const qint64 expected = m_socket.pendingDatagramSize();
        if (expected > static_cast<qint64>(m_buffer.size())) {
            m_socket.readDatagram(m_buffer.data(), 0);
            ++m_oversized;
            continue;
        }
        QHostAddress sender;
        quint16 port = 0;
        const qint64 size = m_socket.readDatagram(m_buffer.data(), m_buffer.size(), &sender, &port);
        if (size < 0) {
            fail(m_socket.errorString());
            break;
        }
        const qint64 utcMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_handler(m_buffer.data(), size, sender, port, utcMs, steadyNs);
    }
    m_draining = false;
    if (m_active && m_socket.hasPendingDatagrams()) {
        m_continuationPending = true;
        const quint64 generation = m_generation;
        QTimer::singleShot(0, this, [this, generation] {
            if (generation != m_generation) {
                return;
            }
            m_continuationPending = false;
            drain();
        });
    }
}

/**
 * @brief 将低频传输错误交给组合根观察，不直接修改设备State或归档数据库。
 */
bool UdpEndpoint::fail(const QString &description)
{
    m_lastError = description;
    emit transportError(description);
    return false;
}
} // namespace core::comm
