#pragma once
#include "CommTypes.h"
#include <QVector>
#include <QString>

class EndpointConfigLoader
{
public:
    static QVector<EndpointConfig> loadFromJson(const QString& filePath);
    static bool saveToJson(const QString& filePath, const QVector<EndpointConfig>& configs);
};
