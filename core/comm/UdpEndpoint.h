#pragma once
#include "Endpoint.h"
#include <QUdpSocket>
#include <QHostAddress>

class UdpEndpoint : public Endpoint
{
    Q_OBJECT

public:
    explicit UdpEndpoint(const EndpointConfig& config, QObject* parent = nullptr);
    ~UdpEndpoint() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool send(const QString& targetKey, const QByteArray& data) override;

    void addTarget(const QString& key, const QString& address, quint16 port);

private slots:
    void onReadyRead();

private:
    QUdpSocket* m_socket = nullptr;
    QMap<QString, QPair<QHostAddress, quint16>> m_resolvedTargets;

    void resolveTargets();
};
