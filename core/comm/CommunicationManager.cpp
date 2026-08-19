#include "CommunicationManager.h"
#include "UdpEndpoint.h"
#include <QDebug>

/**
 * @brief 创建空通信门面，等待配置装配端点。
 */
CommunicationManager::CommunicationManager(QObject* parent)
    : QObject(parent)
{
}

/** @brief 在 QObject 子对象析构前先关闭所有 socket，停止新数据进入上游链路。 */
CommunicationManager::~CommunicationManager()
{
    stopAll();
}

/**
 * @brief 校验唯一 ID、创建具体端点，并把端点信号补充 endpointId 后汇聚输出。
 * @details 捕获配置 ID 而不是 Endpoint 指针，避免信号转发依赖对象外部状态；只有信号连接完成后才写入权威映射。
 */
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

/**
 * @brief 先关闭端点，再从查找表移除并交给 Qt 事件循环销毁。
 */
void CommunicationManager::removeEndpoint(const QString& id)
{
    auto it = m_endpoints.find(id);
    if (it != m_endpoints.end()) {
        (*it)->close();
        (*it)->deleteLater();
        m_endpoints.erase(it);
    }
}

/**
 * @brief 以“全停、全删、按新快照重建、按需全开”的顺序替换通信配置。
 * @warning 中途失败时可能留下部分新端点；AppController 负责使用保存的旧配置再次调用本函数完成回滚。
 */
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

/**
 * @brief 从权威映射查询非拥有端点指针；未知 ID 返回 nullptr。
 */
Endpoint* CommunicationManager::endpoint(const QString& id) const
{
    return m_endpoints.value(id, nullptr);
}

/**
 * @brief 解析 endpointId 后把已编码字节原样交给具体端点发送。
 */
bool CommunicationManager::sendTo(const QString& endpointId, const QString& targetKey, const QByteArray& data)
{
    Endpoint* ep = endpoint(endpointId);
    if (!ep) {
        qWarning() << QStringLiteral("[CommunicationManager] 发送失败: 端点 '%1' 不存在").arg(endpointId);
        return false;
    }
    return ep->send(targetKey, data);
}

/**
 * @brief 尝试打开每个尚未打开的端点，并汇总而不短路单点失败。
 * @details 继续尝试可使其他雷达保持可用；返回 false 提醒上层系统处于部分可用状态。
 */
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

/**
 * @brief 关闭全部 socket 但保留 Endpoint 对象和配置，以支持任务停止后的快速恢复。
 */
void CommunicationManager::stopAll()
{
    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
        (*it)->close();
    }
    qDebug() << QStringLiteral("[CommunicationManager] 所有端点已停止");
}

/**
 * @brief 逐端点采样实际 socket 状态，用于健康报告的打开数量。
 */
int CommunicationManager::openEndpointCount() const
{
    int count = 0;
    for (auto it = m_endpoints.constBegin(); it != m_endpoints.constEnd(); ++it) {
        if ((*it)->isOpen())
            ++count;
    }
    return count;
}

/**
 * @brief 区分“端点未配置”和“已配置但未打开”，两者均返回 false。
 */
bool CommunicationManager::isEndpointOpen(const QString& endpointId) const
{
    Endpoint* ep = endpoint(endpointId);
    return ep && ep->isOpen();
}

/**
 * @brief 生成一次性 ID/打开状态快照，避免 Web 健康接口持有 Endpoint 指针。
 */
QHash<QString, bool> CommunicationManager::endpointOpenStates() const
{
    QHash<QString, bool> states;
    for (auto it = m_endpoints.constBegin(); it != m_endpoints.constEnd(); ++it)
        states.insert(it.key(), (*it)->isOpen());
    return states;
}

/**
 * @brief 把配置协议映射为具体 Endpoint 子类，并明确拒绝尚未实现的 TCP。
 */
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
