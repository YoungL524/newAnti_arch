#include "SystemBus.h"
#include "BusEnvelope.h"
#include <algorithm>
#include <QDebug>
#include "GenericPackets.h"

// ========== 单例实现 ==========
SystemBus& SystemBus::getInstance() {
    static SystemBus instance;
    return instance;
}

// ========== 构造函数和析构函数 ==========

SystemBus::SystemBus()
    : m_nextSubscriptionId(1)
    , m_publishedCount(0)
{
    m_worker = new BusWorker(this);
    startProcessing();
}

SystemBus::~SystemBus()
{
    stopProcessing();
    clearAllSubscriptions();
}

void SystemBus::shutdown()
{
    State expected = State::Running;
    if (!m_state.compare_exchange_strong(expected, State::ShutDown)) {
        return;
    }

    qDebug() << QStringLiteral("[SystemBus] 正在关闭总线...");
    stopProcessing();
    clearAllSubscriptions();
    qDebug() << QStringLiteral("[SystemBus] 总线已关闭（累计发布: %1）").arg(m_publishedCount.load());
}

// ========== ISystemBus接口实现 ==========

bool SystemBus::publish(const DataPacket* dataPacket)
{
    if (isShutdown()) {
        return false;
    }
    if (!dataPacket) {
        qWarning() << QStringLiteral("[SystemBus] 发布失败: dataPacket 为空");
        return false;
    }

    return publish(*dataPacket);
}

bool SystemBus::publish(const DataPacket& dataPacket)
{
    if (isShutdown()) {
        return false;
    }

    auto env = std::make_unique<BusEnvelope>();
    env->header = dataPacket;
    return publish(std::move(*env));
}

bool SystemBus::publish(DataPacket&& dataPacket)
{
    if (isShutdown()) {
        return false;
    }

    auto env = std::make_unique<BusEnvelope>();
    env->header = std::move(dataPacket);
    return publish(std::move(*env));
}

bool SystemBus::publish(BusEnvelope&& envelope)
{
    if (isShutdown()) {
        return false;
    }

    auto ptr = std::make_unique<BusEnvelope>(std::move(envelope));
    if (!m_messageQueue.enqueue(std::move(ptr))) {
        qWarning() << QStringLiteral("[SystemBus] 发布失败: 队列已满（容量=%1）")
                      .arg(m_messageQueue.maxSize());
        return false;
    }

    m_publishedCount.fetch_add(1);
    return true;
}

bool SystemBus::validateCallback(const BusCallback& callback, const QString& operation) const
{
    if (callback) {
        return true;
    }

    qWarning() << QStringLiteral("[SystemBus] %1 失败: 回调为空").arg(operation);
    return false;
}

bool SystemBus::validateEnvelopeCallback(const BusEnvelopeCallback& callback,
                                         const QString& operation) const
{
    if (callback) {
        return true;
    }

    qWarning() << QStringLiteral("[SystemBus] %1 失败: 回调为空").arg(operation);
    return false;
}

SubscriptionId SystemBus::registerSubscription(SubscriptionInfo info)
{
    SubscriptionId id = m_nextSubscriptionId.fetch_add(1);
    const bool hasSourceType = info.hasSourceType;
    const bool hasTarget = info.hasTarget;
    const bool isWildcard = info.isWildcard;

    m_subscriptions.emplace(id, std::move(info));

    if (hasSourceType) {
        addSourceIndex(m_subscriptions.at(id).sourceType, id);
    }
    if (hasTarget) {
        addTargetIndex(m_subscriptions.at(id).target, id);
    }
    if (isWildcard) {
        addWildcardIndex(id);
    }

    return id;
}

void SystemBus::addSourceIndex(uint16_t sourceType, SubscriptionId subscriptionId)
{
    m_sourceTypeSubscriptions[sourceType].push_back(subscriptionId);
}

void SystemBus::addTargetIndex(uint16_t target, SubscriptionId subscriptionId)
{
    m_targetSubscriptions[target].push_back(subscriptionId);
}

void SystemBus::addWildcardIndex(SubscriptionId subscriptionId)
{
    m_wildcardSubscriptions.push_back(subscriptionId);
}

void SystemBus::eraseSubscriptionId(std::vector<SubscriptionId>& subscriptions,
                                    SubscriptionId subscriptionId)
{
    subscriptions.erase(
        std::remove(subscriptions.begin(), subscriptions.end(), subscriptionId),
        subscriptions.end()
    );
}

void SystemBus::removeSourceIndex(uint16_t sourceType, SubscriptionId subscriptionId)
{
    auto it = m_sourceTypeSubscriptions.find(sourceType);
    if (it == m_sourceTypeSubscriptions.end()) {
        return;
    }

    eraseSubscriptionId(it->second, subscriptionId);
    if (it->second.empty()) {
        m_sourceTypeSubscriptions.erase(it);
    }
}

void SystemBus::removeTargetIndex(uint16_t target, SubscriptionId subscriptionId)
{
    auto it = m_targetSubscriptions.find(target);
    if (it == m_targetSubscriptions.end()) {
        return;
    }

    eraseSubscriptionId(it->second, subscriptionId);
    if (it->second.empty()) {
        m_targetSubscriptions.erase(it);
    }
}

void SystemBus::removeWildcardIndex(SubscriptionId subscriptionId)
{
    eraseSubscriptionId(m_wildcardSubscriptions, subscriptionId);
}

void SystemBus::appendUniqueSubscription(std::vector<SubscriptionId>& matchingSubscriptions,
                                         SubscriptionId subscriptionId)
{
    if (std::find(matchingSubscriptions.begin(), matchingSubscriptions.end(), subscriptionId)
        == matchingSubscriptions.end()) {
        matchingSubscriptions.push_back(subscriptionId);
    }
}

bool SystemBus::matchesTarget(const SubscriptionInfo& info, const DataPacket& dataPacket)
{
    if (!info.hasTarget) {
        return true;
    }

    return info.target == dataPacket.dispatchTarget;
}

void SystemBus::collectSourceMatches(const DataPacket& dataPacket,
                                     std::vector<SubscriptionId>& matchingSubscriptions) const
{
    auto sourceIt = m_sourceTypeSubscriptions.find(dataPacket.sourceType);
    if (sourceIt == m_sourceTypeSubscriptions.end()) {
        return;
    }

    for (SubscriptionId id : sourceIt->second) {
        const SubscriptionInfo& info = m_subscriptions.at(id);
        if (matchesTarget(info, dataPacket)) {
            appendUniqueSubscription(matchingSubscriptions, id);
        }
    }
}

void SystemBus::collectTargetMatches(const DataPacket& dataPacket,
                                     std::vector<SubscriptionId>& matchingSubscriptions) const
{
    auto targetIt = m_targetSubscriptions.find(dataPacket.dispatchTarget);
    if (targetIt == m_targetSubscriptions.end()) {
        return;
    }

    for (SubscriptionId id : targetIt->second) {
        const SubscriptionInfo& info = m_subscriptions.at(id);
        if (!info.hasSourceType) {
            appendUniqueSubscription(matchingSubscriptions, id);
        }
    }
}

void SystemBus::collectWildcardMatches(std::vector<SubscriptionId>& matchingSubscriptions) const
{
    for (SubscriptionId id : m_wildcardSubscriptions) {
        appendUniqueSubscription(matchingSubscriptions, id);
    }
}

std::vector<BusCallback> SystemBus::collectPacketCallbacks(
    const std::vector<SubscriptionId>& matchingSubscriptions) const
{
    std::vector<BusCallback> out;
    out.reserve(matchingSubscriptions.size());
    for (SubscriptionId id : matchingSubscriptions) {
        auto it = m_subscriptions.find(id);
        if (it != m_subscriptions.end() && it->second.packetCallback)
            out.push_back(it->second.packetCallback);
    }
    return out;
}

std::vector<BusEnvelopeCallback> SystemBus::collectEnvelopeCallbacks(
    const std::vector<SubscriptionId>& matchingSubscriptions) const
{
    std::vector<BusEnvelopeCallback> out;
    out.reserve(matchingSubscriptions.size());
    for (SubscriptionId id : matchingSubscriptions) {
        auto it = m_subscriptions.find(id);
        if (it != m_subscriptions.end() && it->second.envelopeCallback)
            out.push_back(it->second.envelopeCallback);
    }
    return out;
}

void SystemBus::invokePacketCallbacks(const std::vector<BusCallback>& callbacks,
                                      const DataPacket* dataPacket) const
{
    for (const auto& callback : callbacks) {
        try {
            callback(dataPacket);
        } catch (...) {
            qWarning() << QStringLiteral("[SystemBus] 回调执行异常，已忽略");
        }
    }
}

void SystemBus::invokeEnvelopeCallbacks(const std::vector<BusEnvelopeCallback>& callbacks,
                                        const BusEnvelope& envelope) const
{
    for (const auto& callback : callbacks) {
        try {
            callback(envelope);
        } catch (...) {
            qWarning() << QStringLiteral("[SystemBus] Envelope 回调执行异常，已忽略");
        }
    }
}

SubscriptionId SystemBus::subscribeBySource(uint16_t sourceType, BusCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateCallback(callback, QStringLiteral("按源类型订阅"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(SubscriptionInfo::BySource{}, sourceType, std::move(callback)));
}

SubscriptionId SystemBus::subscribeBySourceEnvelope(uint16_t sourceType,
                                                    BusEnvelopeCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateEnvelopeCallback(callback, QStringLiteral("按源类型订阅 Envelope"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(SubscriptionInfo::BySource{}, sourceType, std::move(callback)));
}

SubscriptionId SystemBus::subscribeByTargetEnvelope(uint16_t target,
                                                    BusEnvelopeCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateEnvelopeCallback(callback, QStringLiteral("按目标订阅 Envelope"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(SubscriptionInfo::ByTarget{}, target, std::move(callback)));
}

SubscriptionId SystemBus::subscribeEnvelope(uint16_t sourceType, uint16_t target,
                                            BusEnvelopeCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateEnvelopeCallback(callback, QStringLiteral("组合订阅 Envelope"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(sourceType, target, std::move(callback)));
}

SubscriptionId SystemBus::subscribeAllEnvelope(BusEnvelopeCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateEnvelopeCallback(callback, QStringLiteral("通配符订阅 Envelope"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(std::move(callback)));
}

SubscriptionId SystemBus::subscribeByTarget(uint16_t target, BusCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateCallback(callback, QStringLiteral("按目标订阅"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(SubscriptionInfo::ByTarget{}, target, std::move(callback)));
}

SubscriptionId SystemBus::subscribe(uint16_t sourceType, uint16_t target, BusCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateCallback(callback, QStringLiteral("组合订阅"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(sourceType, target, std::move(callback)));
}

SubscriptionId SystemBus::subscribeAll(BusCallback callback)
{
    if (isShutdown()) {
        return 0;
    }
    if (!validateCallback(callback, QStringLiteral("通配符订阅"))) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return registerSubscription(SubscriptionInfo(std::move(callback)));
}

bool SystemBus::unsubscribe(SubscriptionId subscriptionId)
{
    if (isShutdown()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);

    auto it = m_subscriptions.find(subscriptionId);

    if (it == m_subscriptions.end()) {
        return false;
    }

    const SubscriptionInfo& info = it->second;

    if (info.hasSourceType) {
        removeSourceIndex(info.sourceType, subscriptionId);
    }

    if (info.hasTarget) {
        removeTargetIndex(info.target, subscriptionId);
    }

    if (info.isWildcard) {
        removeWildcardIndex(subscriptionId);
    }

    m_subscriptions.erase(it);

    return true;
}

size_t SystemBus::getSubscriptionCount() const
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return m_subscriptions.size();
}

uint64_t SystemBus::getPublishedCount() const
{
    return m_publishedCount.load();
}

SystemBus::State SystemBus::state() const
{
    return m_state.load();
}

bool SystemBus::isRunning() const
{
    return m_state.load() == State::Running;
}

bool SystemBus::isShutdown() const
{
    return m_state.load() == State::ShutDown;
}

void SystemBus::clearAllSubscriptions()
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);

    m_subscriptions.clear();
    m_sourceTypeSubscriptions.clear();
    m_targetSubscriptions.clear();
    m_wildcardSubscriptions.clear();
}

void SystemBus::startProcessing()
{
    if (m_worker) {
        m_worker->start();
    }
}

void SystemBus::stopProcessing()
{
    if (m_worker) {
        m_messageQueue.shutdown();
        // BusWorker::run() 通过 dequeue 返回 false 自然退出，无需 quit()
        m_worker->wait();
        delete m_worker;
        m_worker = nullptr;
    }
}

// ========== 私有方法实现 ==========

void SystemBus::dispatchToSubscribers(const BusEnvelope& envelope)
{
    const DataPacket* dataPacket = &envelope.header;

    std::vector<BusCallback> packetCallbacks;
    std::vector<BusEnvelopeCallback> envelopeCallbacks;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        std::vector<SubscriptionId> matchingSubscriptions;

        collectSourceMatches(*dataPacket, matchingSubscriptions);
        collectTargetMatches(*dataPacket, matchingSubscriptions);
        collectWildcardMatches(matchingSubscriptions);
        packetCallbacks = collectPacketCallbacks(matchingSubscriptions);
        envelopeCallbacks = collectEnvelopeCallbacks(matchingSubscriptions);
    }

    invokePacketCallbacks(packetCallbacks, dataPacket);
    invokeEnvelopeCallbacks(envelopeCallbacks, envelope);
}

void SystemBus::dispatchToSubscribers(const DataPacket* dataPacket)
{
    if (!dataPacket) {
        qWarning() << QStringLiteral("[SystemBus] 分发失败: dataPacket 为空");
        return;
    }

    BusEnvelope env;
    env.header = *dataPacket;
    dispatchToSubscribers(env);
}

// ========== BusWorker 实现 ==========

void SystemBus::BusWorker::run()
{
    while (true) {
        std::unique_ptr<BusEnvelope> env;
        if (m_bus->m_messageQueue.dequeue(env)) {
            m_bus->dispatchToSubscribers(*env);
        } else {
            break;
        }
    }
}
