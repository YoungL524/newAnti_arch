#include "EndpointConfigLoader.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

QVector<EndpointConfig> EndpointConfigLoader::loadFromJson(const QString& filePath)
{
    QVector<EndpointConfig> result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << QStringLiteral("[ConfigLoader] 无法打开配置文件:") << filePath;
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << QStringLiteral("[ConfigLoader] JSON解析错误:") << parseError.errorString();
        return result;
    }

    QJsonObject root = doc.object();
    QJsonArray endpoints = root[QStringLiteral("endpoints")].toArray();

    for (const QJsonValue& val : endpoints) {
        QJsonObject obj = val.toObject();
        EndpointConfig cfg = parseEndpoint(obj);
        if (cfg.id.isEmpty()) {
            qWarning() << QStringLiteral("[ConfigLoader] 跳过无 id 的端点配置");
            continue;
        }

        result.append(cfg);
        qDebug() << QStringLiteral("[ConfigLoader] 已加载端点: '%1' (%2)")
                    .arg(cfg.id, cfg.description);
    }

    qDebug() << QStringLiteral("[ConfigLoader] 共加载 %1 个端点配置").arg(result.size());
    return result;
}

EndpointConfig EndpointConfigLoader::parseEndpoint(const QJsonObject& obj)
{
    EndpointConfig cfg;
    cfg.id           = obj[QStringLiteral("id")].toString();
    cfg.localAddress = obj[QStringLiteral("localAddress")].toString(QStringLiteral("127.0.0.1"));
    cfg.localPort    = static_cast<quint16>(obj[QStringLiteral("localPort")].toInt(0));
    cfg.description  = obj[QStringLiteral("description")].toString();

    const QString proto = obj[QStringLiteral("protocol")].toString(QStringLiteral("udp")).toLower();
    cfg.protocol = (proto == QStringLiteral("tcp")) ? Protocol::TCP : Protocol::UDP;

    const QJsonObject targetsObj = obj[QStringLiteral("targets")].toObject();
    for (auto it = targetsObj.constBegin(); it != targetsObj.constEnd(); ++it) {
        const QJsonObject tgt = it.value().toObject();
        Target target;
        target.address = tgt[QStringLiteral("address")].toString();
        target.port    = static_cast<quint16>(tgt[QStringLiteral("port")].toInt(0));
        cfg.targets.insert(it.key(), target);
    }
    return cfg;
}

QJsonObject EndpointConfigLoader::endpointToJson(const EndpointConfig& cfg)
{
    QJsonObject obj;
    obj[QStringLiteral("id")]           = cfg.id;
    obj[QStringLiteral("protocol")]     = (cfg.protocol == Protocol::TCP) ? QStringLiteral("tcp") : QStringLiteral("udp");
    obj[QStringLiteral("localAddress")] = cfg.localAddress;
    obj[QStringLiteral("localPort")]    = cfg.localPort;
    obj[QStringLiteral("description")]  = cfg.description;

    QJsonObject targetsObj;
    for (auto it = cfg.targets.constBegin(); it != cfg.targets.constEnd(); ++it) {
        QJsonObject tgt;
        tgt[QStringLiteral("address")] = it.value().address;
        tgt[QStringLiteral("port")]    = it.value().port;
        targetsObj[it.key()] = tgt;
    }
    obj[QStringLiteral("targets")] = targetsObj;
    return obj;
}

QVector<EndpointConfig> EndpointConfigLoader::parseFromJsonObject(const QJsonObject& root)
{
    QVector<EndpointConfig> result;
    for (const auto& val : root[QStringLiteral("endpoints")].toArray())
        result.append(parseEndpoint(val.toObject()));
    return result;
}

QJsonObject EndpointConfigLoader::toJsonObject(const QVector<EndpointConfig>& configs)
{
    QJsonArray endpoints;
    for (const auto& cfg : configs)
        endpoints.append(endpointToJson(cfg));
    QJsonObject root;
    root[QStringLiteral("endpoints")] = endpoints;
    return root;
}

bool EndpointConfigLoader::saveToJson(const QString& filePath, const QVector<EndpointConfig>& configs)
{
    const QJsonObject root = toJsonObject(configs);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << QStringLiteral("[ConfigLoader] 无法写入配置文件:") << filePath;
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
