#pragma once
/**
 * @file Endpoint.h
 * @brief 定义单个网络端点的协议无关生命周期和原始字节收发接口。
 * @details Endpoint 是通信层与上层处理器的边界：它只负责 socket、报文边界和接收时间，不解释厂家协议。
 *          QObject 线程亲和性决定 socket 所在线程；open、close、send 应在该线程调用。
 */

#include "CommTypes.h"
#include <QObject>
#include <QByteArray>

/** @brief 由 CommunicationManager 拥有的抽象通信端点。 */
class Endpoint : public QObject
{
    Q_OBJECT

public:
    /** @brief 保存不可变端点身份和初始目标配置，不打开网络资源。 */
    explicit Endpoint(const EndpointConfig& config, QObject* parent = nullptr);
    virtual ~Endpoint() = default;

    /** @brief 返回用于配置、设备绑定和日志关联的稳定端点 ID。 */
    QString id() const { return m_config.id; }
    /** @brief 返回端点当前配置的只读引用；引用有效期与端点对象相同。 */
    const EndpointConfig& config() const { return m_config; }

    /** @brief 绑定本地地址并开始接收；失败时通过返回值和 errorOccurred 报告。 */
    virtual bool open() = 0;
    /** @brief 停止接收并释放 socket；必须可重复调用。 */
    virtual void close() = 0;
    /** @brief 查询底层 socket 是否处于可收发状态。 */
    virtual bool isOpen() const = 0;
    /** @brief 向配置中的具名目标发送一个完整报文，不负责协议编码。 */
    virtual bool send(const QString& targetKey, const QByteArray& data) = 0;

signals:
    /**
     * @brief 收到原始数据
     * @param data       原始字节（UDP 报文）
     * @param receivedMs 墙上时间，用于业务时间和日志关联
     * @param receivedSteadyNs 单调时钟时间，用于进程内精确耗时计算
     */
    void dataReceived(const QByteArray& data, qint64 receivedMs, qint64 receivedSteadyNs);
    /** @brief 报告绑定、解析目标、接收或发送失败的可诊断文本。 */
    void errorOccurred(const QString& message);

protected:
    EndpointConfig m_config;    ///< 端点配置副本；子类可在运行前扩展目标。
    bool m_running = false;     ///< 逻辑运行标志，与子类 isOpen() 的 socket 状态共同维护。
};
