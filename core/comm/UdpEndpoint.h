/**
 * @file UdpEndpoint.h
 * @brief 提供协议无关的单个 UDP 端点，连接网络数据报与同线程 ProtocolAdapter。
 * @details 上游为操作系统 UDP socket，下游为组合根指定的唯一同步接收处理函数。
 * 从 start()、send() 阅读；本类不识别心跳或命令，不发布 Topic，不持有消费者队列，不创建线程。
 * sock2Internal 的部署地址由调用方传入127.0.0.1:7788，外部端点同样使用本类而不是另建业务链路。
 */
#pragma once

#include <QObject>
#include <QUdpSocket>
#include <array>
#include <functional>

namespace core::comm {

/** @brief 有界收取完整数据报的传输端点；构造、收发、停止和析构均须在所属事件循环线程执行。 */
class UdpEndpoint final : public QObject
{
    Q_OBJECT
public:
    static constexpr int maxSendPayloadBytes = 65507; ///< 本端点发送策略上限（字节），独立于接收缓冲，IPv4/IPv6统一采用此保守上限。
    /**
     * @brief 唯一同步接收入口；不构造新的通用业务消息。
     * @details 参数依次为借用字节、字节数、来源地址、来源端口、接收UTC毫秒、接收单调纳秒。
     * 缓冲仅在回调内有效；回调必须有界执行，不得销毁端点或递归驱动事件循环，可调用stop()。
     */
    using ReceiveHandler = std::function<void(const char *, qint64, const QHostAddress &, quint16, qint64, qint64)>;

    /** @brief 构造未绑定端点；空handler不抛异常，start()会拒绝启动；处理函数依赖必须存活至端点停止。 */
    explicit UdpEndpoint(ReceiveHandler handler, QObject *parent = nullptr);
    /** @brief 独占绑定明确的本地地址；端口0允许测试分配临时端口，空处理函数、重复启动或绑定失败返回false。 */
    bool start(const QHostAddress &localAddress, quint16 localPort);
    /** @brief 立即解绑并作废待执行续读，可重复调用，不向协议层发送伪造离线数据。 */
    void stop();
    /** @brief 向显式目标发送一个完整数据报；未绑定、无效目标、空包或超长包返回false，无重试/排队。 */
    bool send(const QByteArray &bytes, const QHostAddress &targetAddress, quint16 targetPort);
    /** @brief 返回当前绑定端口，停止后为0；仅在所属线程查询。 */
    quint16 localPort() const;
    /** @brief 返回最近一次操作失败说明，成功的start/send会清空；不作为状态真源。 */
    QString lastError() const;
    /** @brief 返回端点生命周期累计因超过固定缓冲而整包丢弃的数据报数。 */
    quint64 oversizedDatagrams() const;

Q_SIGNALS:
    /** @brief 报告socket读取/绑定/发送失败的诊断文字，不携带业务数据；由组合根处理。 */
    void transportError(const QString &description);

private:
    /** @brief 在所属事件循环收取至多64个数据报，剩余数据通过一次合并续读处理。 */
    void drain();
    /** @brief 保存错误并发出诊断信号，返回false以供失败分支直接返回。 */
    bool fail(const QString &description);

    QUdpSocket m_socket; ///< 本端点唯一socket，随QObject父子关系保持线程亲和。
    ReceiveHandler m_handler; ///< 唯一同步ProtocolAdapter入口，不缓存消息。
    std::array<char, 65507> m_buffer {}; ///< IPv4最大UDP载荷缓冲，构造时分配且跨数据报复用。
    QString m_lastError; ///< 最近传输错误，低频诊断文本。
    quint64 m_generation = 0; ///< 启停代次，防止旧续读处理新socket。
    quint64 m_oversized = 0; ///< 超长整包丢弃计数。
    bool m_active = false; ///< 当前socket已经成功绑定。
    bool m_continuationPending = false; ///< 传输层唯一续读唤醒，不保存数据或Consumer调度状态。
    bool m_draining = false; ///< 拒绝同步重入，保护借用缓冲有效期。
};
} // namespace core::comm
