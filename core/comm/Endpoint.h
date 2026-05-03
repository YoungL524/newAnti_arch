#pragma once
#include "CommTypes.h"
#include <QObject>
#include <QByteArray>

class Endpoint : public QObject
{
    Q_OBJECT

public:
    explicit Endpoint(const EndpointConfig& config, QObject* parent = nullptr);
    virtual ~Endpoint() = default;

    QString id() const { return m_config.id; }
    const EndpointConfig& config() const { return m_config; }

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool send(const QString& targetKey, const QByteArray& data) = 0;

signals:
    /**
     * @brief 收到原始数据
     * @param data       原始字节（UDP 报文）
     * @param receivedMs 数据到达时间戳（currentMSecsSinceEpoch），
     *                   在 readDatagram 后立即捕获，反映系统层接收时刻
     */
    void dataReceived(const QByteArray& data, qint64 receivedMs);
    void errorOccurred(const QString& message);

protected:
    EndpointConfig m_config;
    bool m_running = false;
};
