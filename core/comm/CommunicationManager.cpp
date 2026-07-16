#include "CommunicationManager.h"
#include "UdpEndpoint.h"
#include <QDebug>

CommunicationManager::CommunicationManager(QObject* parent)
    : QObject(parent)
{
}

CommunicationManager::~CommunicationManager()
{
    stopAll();
}

bool CommunicationManager::addEndpoint(const EndpointConfig& config)
{
    if (config.id.isEmpty()) {
        qWarning() << QStringLiteral("[CommunicationManager] 添加端点失败: id 为空");
        return false;
    }
    if (m_endpoints.contains(config.id)) {
        qWarning() << QStringLiteral("[CommunicationManager] 添加端点失败: '%1' 已存在").arg(config.id);
        return false;
    }

    Endpoint* ep = createEndpoint(config);
    if (!ep) {
        return false;
    }

    connect(ep, &Endpoint::dataReceived, this,
            [this, id = config.id](const QByteArray& data, qint64 receivedMs,
                                   qint64 receivedSteadyNs) {
        emit dataReceived(id, data, receivedMs, receivedSteadyNs);
    });
    connect(ep, &Endpoint::errorOccurred, this, [this, id = config.id](const QString& msg) {
        emit errorOccurred(id, msg);
    });

    m_endpoints.insert(config.id, ep);
    qDebug() << QStringLiteral("[CommunicationManager] 已添加端点: '%1' (%2)")
                .arg(config.id, config.description);
    return true;
}

void CommunicationManager::removeEndpoint(const QString& id)
{
    auto it = m_endpoints.find(id);
    if (it != m_endpoints.end()) {
        (*it)->close();
        (*it)->deleteLater();
        m_endpoints.erase(it);
    }
}

bool CommunicationManager::reloadEndpoints(const QVector<EndpointConfig>& configs, bool openAfter)
{
    stopAll();
    const auto ids = m_endpoints.keys();
    for (const QString& id : ids)
        removeEndpoint(id);

    for (const auto& cfg : configs) {
        if (cfg.id.isEmpty()) {
            qWarning() << QStringLiteral("[CommunicationManager] 跳过重载：端点 id 为空");
            continue;
        }
        if (!addEndpoint(cfg))
            return false;
    }

    if (openAfter && !startAll())
        return false;
    return true;
}

Endpoint* CommunicationManager::endpoint(const QString& id) const
{
    return m_endpoints.value(id, nullptr);
}

bool CommunicationManager::sendTo(const QString& endpointId, const QString& targetKey, const QByteArray& data)
{
    Endpoint* ep = endpoint(endpointId);
    if (!ep) {
        qWarning() << QStringLiteral("[CommunicationManager] 发送失败: 端点 '%1' 不存在").arg(endpointId);
        return false;
    }
    return ep->send(targetKey, data);
}

bool CommunicationManager::startAll()
{
    bool allOpened = true;
    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
        if (!(*it)->isOpen()) {
            if (!(*it)->open()) {
                qWarning() << QStringLiteral("[CommunicationManager] 端点 '%1' 打开失败").arg(it.key());
                allOpened = false;
            }
        }
    }
    if (allOpened)
        qDebug() << QStringLiteral("[CommunicationManager] 所有端点已启动, 共 %1 个").arg(m_endpoints.size());
    return allOpened;
}

void CommunicationManager::stopAll()
{
    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
        (*it)->close();
    }
    qDebug() << QStringLiteral("[CommunicationManager] 所有端点已停止");
}

int CommunicationManager::openEndpointCount() const
{
    int count = 0;
    for (auto it = m_endpoints.constBegin(); it != m_endpoints.constEnd(); ++it) {
        if ((*it)->isOpen())
            ++count;
    }
    return count;
}

bool CommunicationManager::isEndpointOpen(const QString& endpointId) const
{
    Endpoint* ep = endpoint(endpointId);
    return ep && ep->isOpen();
}

QHash<QString, bool> CommunicationManager::endpointOpenStates() const
{
    QHash<QString, bool> states;
    for (auto it = m_endpoints.constBegin(); it != m_endpoints.constEnd(); ++it)
        states.insert(it.key(), (*it)->isOpen());
    return states;
}

Endpoint* CommunicationManager::createEndpoint(const EndpointConfig& config)
{
    switch (config.protocol) {
    case Protocol::UDP:
        return new UdpEndpoint(config, this);
    case Protocol::TCP:
        qWarning() << QStringLiteral("[CommunicationManager] TCP 端点暂未实现");
        return nullptr;
    default:
        qWarning() << QStringLiteral("[CommunicationManager] 未知协议类型");
        return nullptr;
    }
}
