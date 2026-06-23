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
    bool reloadEndpoints(const QVector<EndpointConfig>& configs, bool openAfter);
    Endpoint* endpoint(const QString& id) const;

    bool sendTo(const QString& endpointId, const QString& targetKey, const QByteArray& data);

    void startAll();
    void stopAll();

    int endpointCount() const { return m_endpoints.size(); }
    int openEndpointCount() const;

signals:
    /**
     * @brief 接收到来自端点的数据（转发自 Endpoint::dataReceived）
     * @param endpointId 来源端点的 id
     * @param data       原始字节
     * @param receivedMs 数据到达时间戳（来自 Endpoint 的接收时刻）
     */
    void dataReceived(const QString& endpointId, const QByteArray& data, qint64 receivedMs);
    void errorOccurred(const QString& endpointId, const QString& message);

private:
    QMap<QString, Endpoint*> m_endpoints;

    Endpoint* createEndpoint(const EndpointConfig& config);
};
