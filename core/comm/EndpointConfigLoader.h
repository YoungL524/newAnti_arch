#pragma once
#include "CommTypes.h"
#include <QJsonObject>
#include <QVector>
#include <QString>

class EndpointConfigLoader
{
public:
    static QVector<EndpointConfig> loadFromJson(const QString& filePath);
    static bool saveToJson(const QString& filePath, const QVector<EndpointConfig>& configs);
    static QVector<EndpointConfig> parseFromJsonObject(const QJsonObject& root);
    static QJsonObject toJsonObject(const QVector<EndpointConfig>& configs);
    static EndpointConfig parseEndpoint(const QJsonObject& obj);
    static QJsonObject endpointToJson(const EndpointConfig& cfg);
};
