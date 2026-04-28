#pragma once
#include "CommTypes.h"
#include "Endpoint.h"
#include <QObject>
#include <QMap>

class CommunicationManager : public QObject
{
    Q_OBJECT

public:
    explicit CommunicationManager(QObject* parent = nullptr);
    ~CommunicationManager() override;

    bool addEndpoint(const EndpointConfig& config);
    void removeEndpoint(const QString& id);
    Endpoint* endpoint(const QString& id) const;

    bool sendTo(const QString& endpointId, const QString& targetKey, const QByteArray& data);

    void startAll();
    void stopAll();

    int endpointCount() const { return m_endpoints.size(); }

signals:
    void dataReceived(const QString& endpointId, const QByteArray& data);
    void errorOccurred(const QString& endpointId, const QString& message);

private:
    QMap<QString, Endpoint*> m_endpoints;

    Endpoint* createEndpoint(const EndpointConfig& config);
};
