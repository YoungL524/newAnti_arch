#pragma once
/**
 * @file UdpEndpoint.h
 * @brief 实现保持 UDP 报文边界的接收端点和具名目标发送。
 * @details readyRead 到达后会排空 socket 中的全部 pending datagram；每个报文独立复制到 QByteArray，
 *          并同时记录 Unix 毫秒和单调纳秒时间后向上游发射。目标地址在 open 前集中解析，发送热路径不做 DNS。
 */

#include "Endpoint.h"
#include <QUdpSocket>
#include <QHostAddress>

/** @brief 基于 QUdpSocket 的单线程 UDP Endpoint。 */
class UdpEndpoint : public Endpoint
{
    Q_OBJECT

public:
    /** @brief 创建未绑定 socket 的端点，并保存待解析的目标配置。 */
    explicit UdpEndpoint(const EndpointConfig& config, QObject* parent = nullptr);
    ~UdpEndpoint() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool send(const QString& targetKey, const QByteArray& data) override;

    /** @brief 增加或替换一个具名发送目标，并立即解析其地址供后续 send() 使用。 */
    void addTarget(const QString& key, const QString& address, quint16 port);

private slots:
    /** @brief 排空所有待收 UDP 报文，并逐报文发出带双时间基准的 dataReceived。 */
    void onReadyRead();

private:
    QUdpSocket* m_socket = nullptr; ///< 由本对象作为 QObject parent 持有；close 后对象可保留但 socket 关闭。
    QMap<QString, QPair<QHostAddress, quint16>> m_resolvedTargets; ///< 已解析的 targetKey 到地址/端口映射。

    /** @brief 从 m_config.targets 重建解析缓存；无法解析的目标通过 errorOccurred 报告并跳过。 */
    void resolveTargets();
};
