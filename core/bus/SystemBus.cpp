#include "SystemBus.h"
#include <algorithm>
#include <QDebug>
#include "GenericPackets.h"

// ========== 单例实现 ==========
/**
 * @brief 延迟构造进程级总线单例。
 * @details C++ 局部静态对象保证并发首次访问时只构造一次；构造过程会同步启动 BusWorker。
 */
SystemBus& SystemBus::getInstance() {
    static SystemBus instance;
    return instance;
}

// ========== 构造函数和析构函数 ==========

/**
 * @brief 初始化订阅 ID 和发布计数，并创建常驻的消息分发线程。
 * @details ID 从 1 开始以保留 0 作为订阅失败值。工作线程在所有成员完成构造后才启动。
 */
SystemBus::SystemBus()
    : m_nextSubscriptionId(1)
    , m_publishedCount(0)
{
    m_worker = new BusWorker(this);
    startProcessing();
}

/**
 * @brief 确保工作线程已经退出，并清空仍登记的订阅回调。
 * @note 正常进程退出应先显式调用 shutdown()；这里提供最后一道资源回收保障。
 */
SystemBus::~SystemBus()
{
    stopProcessing();
    clearAllSubscriptions();
}

/**
 * @brief 将总线原子地切换为永久关闭状态并等待分发线程退出。
 * @details compare_exchange 保证只有第一个调用者执行关闭；状态先置为 ShutDown，使并发发布和订阅立即失败，
 *          随后关闭队列唤醒 BusWorker，最后清除回调，避免进程退出阶段访问已销毁的订阅者。
 */
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

/**
 * @brief 为接口层的可空指针发布提供防御性入口。
 * @details 拒绝关闭状态和空指针；合法对象转交 const 引用重载统一执行载荷一致性与队列容量检查。
 */
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

/**
 * @brief 复制一个类型一致的 DataPacket，并把副本所有权移交给有界队列。
 * @details command 与 variant 类型不一致时拒绝发布；队列满时不阻塞接收线程并返回失败。
 *          m_publishedCount 只在成功入队后增加，因此可用于判断总线实际接纳的消息量。
 */
bool SystemBus::publish(const DataPacket& dataPacket)
{
    if (isShutdown()) {
        return false;
    }
    if (!dataPacket.hasConsistentPayload()) {
        qWarning() << QStringLiteral("[SystemBus] 发布失败: command 与 payload 类型不匹配, command=%1")
                      .arg(dataPacket.commandValue());
        return false;
    }

    auto packet = std::make_unique<DataPacket>(dataPacket);
    if (!m_messageQueue.enqueue(std::move(packet))) {
        qWarning() << QStringLiteral("[SystemBus] 发布失败: 队列已满（容量=%1）")
                      .arg(m_messageQueue.maxSize());
        return false;
    }
    m_publishedCount.fetch_add(1);
    return true;
}

/**
 * @brief 通过移动语义发布 DataPacket，避免复制 TrackPacket 等业务载荷。
 * @details 校验与背压行为和 const 引用重载完全一致；失败时调用方传入对象可能已被移动，不能继续依赖其内容。
 */
bool SystemBus::publish(DataPacket&& dataPacket)
{
    if (isShutdown()) {
        return false;
    }
    if (!dataPacket.hasConsistentPayload()) {
        qWarning() << QStringLiteral("[SystemBus] 发布失败: command 与 payload 类型不匹配, command=%1")
                      .arg(dataPacket.commandValue());
        return false;
    }

    auto packet = std::make_unique<DataPacket>(std::move(dataPacket));
    if (!m_messageQueue.enqueue(std::move(packet))) {
        qWarning() << QStringLiteral("[SystemBus] 发布失败: 队列已满（容量=%1）")
                      .arg(m_messageQueue.maxSize());
        return false;
    }

    m_publishedCount.fetch_add(1);
    return true;
}

/**
 * @brief 在修改订阅表前拒绝空 std::function，并用操作名生成可定位日志。
 */
bool SystemBus::validateCallback(const BusCallback& callback, const QString& operation) const
{
    if (callback) {
        return true;
    }

    qWarning() << QStringLiteral("[SystemBus] %1 失败: 回调为空").arg(operation);
    return false;
}

/**
 * @brief 把订阅写入权威表，并同步维护其参与的倒排索引。
 * @details 调用者必须持有 m_subscriptionsMutex。先保存匹配标志再移动 info，避免从已移动对象读取索引条件。
 */
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

/**
 * @brief 将订阅 ID 追加到 sourceType 倒排索引；调用者负责持有订阅锁。
 */
void SystemBus::addSourceIndex(uint16_t sourceType, SubscriptionId subscriptionId)
{
    m_sourceTypeSubscriptions[sourceType].push_back(subscriptionId);
}

/**
 * @brief 将订阅 ID 追加到 dispatchTarget 倒排索引；调用者负责持有订阅锁。
 */
void SystemBus::addTargetIndex(uint16_t target, SubscriptionId subscriptionId)
{
    m_targetSubscriptions[target].push_back(subscriptionId);
}

/**
 * @brief 将无匹配条件的订阅加入通配符索引；调用者负责持有订阅锁。
 */
void SystemBus::addWildcardIndex(SubscriptionId subscriptionId)
{
    m_wildcardSubscriptions.push_back(subscriptionId);
}

/**
 * @brief 使用 erase-remove 惯用法删除索引列表中的全部指定 ID。
 * @param[in,out] subscriptions 待原地修改的某一倒排索引桶。
 * @param[in] subscriptionId 要删除的订阅 ID；不存在时保持原列表不变。
 */
void SystemBus::eraseSubscriptionId(std::vector<SubscriptionId>& subscriptions,
                                    SubscriptionId subscriptionId)
{
    subscriptions.erase(
        std::remove(subscriptions.begin(), subscriptions.end(), subscriptionId),
        subscriptions.end()
    );
}

/**
 * @brief 从 sourceType 倒排索引移除订阅，并回收已经为空的索引桶。
 */
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

/**
 * @brief 从 dispatchTarget 倒排索引移除订阅，并回收已经为空的索引桶。
 */
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

/**
 * @brief 从通配符列表移除订阅 ID；未知 ID 不改变列表。
 */
void SystemBus::removeWildcardIndex(SubscriptionId subscriptionId)
{
    eraseSubscriptionId(m_wildcardSubscriptions, subscriptionId);
}

/**
 * @brief 把订阅 ID 去重后加入单个数据包的匹配集合。
 * @details 组合订阅同时存在于源索引和目标索引；这里保证一个数据包只触发该订阅一次。
 */
void SystemBus::appendUniqueSubscription(std::vector<SubscriptionId>& matchingSubscriptions,
                                         SubscriptionId subscriptionId)
{
    if (std::find(matchingSubscriptions.begin(), matchingSubscriptions.end(), subscriptionId)
        == matchingSubscriptions.end()) {
        matchingSubscriptions.push_back(subscriptionId);
    }
}

/**
 * @brief 判断源类型索引命中的订阅是否还满足可选的目标条件。
 * @details 纯源订阅没有目标约束，直接匹配；组合订阅必须同时匹配 dispatchTarget。
 */
bool SystemBus::matchesTarget(const SubscriptionInfo& info, const DataPacket& dataPacket)
{
    if (!info.hasTarget) {
        return true;
    }

    return info.target == dataPacket.dispatchTarget;
}

/**
 * @brief 从 sourceType 索引收集纯源订阅和同时满足目标条件的组合订阅。
 * @note 调用者必须持有 m_subscriptionsMutex，确保索引 ID 在权威表中仍然有效。
 */
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

/**
 * @brief 从 dispatchTarget 索引收集纯目标订阅。
 * @details 组合订阅已经由 collectSourceMatches() 同时校验两个条件，此处跳过 hasSourceType 的记录。
 */
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

/**
 * @brief 把所有通配符订阅并入本次匹配结果，同时避免与其他索引产生重复回调。
 */
void SystemBus::collectWildcardMatches(std::vector<SubscriptionId>& matchingSubscriptions) const
{
    for (SubscriptionId id : m_wildcardSubscriptions) {
        appendUniqueSubscription(matchingSubscriptions, id);
    }
}

/**
 * @brief 把匹配 ID 转换为可在锁外安全执行的回调快照。
 * @details 复制 std::function 后，即使后续回调取消订阅，本轮分发仍使用进入分发阶段时的订阅集合。
 */
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

/**
 * @brief 按收集顺序同步执行回调，并隔离第三方回调抛出的异常。
 * @warning 所有回调运行在同一个 BusWorker 线程；耗时操作会增加整个总线的排队延时。
 */
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

/**
 * @brief 登记只匹配 sourceType 的订阅。
 * @details 关闭检查和回调校验在加锁前完成，缩短订阅表临界区；登记时由统一入口同时更新权威表和源索引。
 */
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

/**
 * @brief 登记只匹配 dispatchTarget 的订阅，并写入目标倒排索引。
 */
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

/**
 * @brief 登记必须同时匹配 sourceType 与 dispatchTarget 的组合订阅。
 * @details 同一订阅会进入两个索引，分发阶段通过 appendUniqueSubscription 保证最多执行一次。
 */
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

/**
 * @brief 登记不带任何匹配条件的通配符订阅。
 */
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

/**
 * @brief 在同一临界区内从所有相关索引及权威表删除订阅。
 * @details 先读取 SubscriptionInfo 决定需要清理的索引，最后删除权威记录，避免索引留下悬空 ID。
 */
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

/**
 * @brief 在订阅锁保护下读取权威订阅表大小，保证诊断快照不发生数据竞争。
 */
size_t SystemBus::getSubscriptionCount() const
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    return m_subscriptions.size();
}

/**
 * @brief 返回成功入队的累计消息数；原子读取不会阻塞发布线程。
 */
uint64_t SystemBus::getPublishedCount() const
{
    return m_publishedCount.load();
}

/**
 * @brief 原子读取不可逆的总线生命周期状态。
 */
SystemBus::State SystemBus::state() const
{
    return m_state.load();
}

/**
 * @brief 判断总线是否仍允许发布和新增订阅。
 */
bool SystemBus::isRunning() const
{
    return m_state.load() == State::Running;
}

/**
 * @brief 判断 shutdown() 是否已开始执行；一旦为 true 将永久保持。
 */
bool SystemBus::isShutdown() const
{
    return m_state.load() == State::ShutDown;
}

/**
 * @brief 在同一临界区清空权威表和全部倒排索引，使它们重新保持一致。
 */
void SystemBus::clearAllSubscriptions()
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);

    m_subscriptions.clear();
    m_sourceTypeSubscriptions.clear();
    m_targetSubscriptions.clear();
    m_wildcardSubscriptions.clear();
}

/**
 * @brief 启动构造阶段创建的 BusWorker；进程生命周期内只调用一次。
 */
void SystemBus::startProcessing()
{
    if (m_worker) {
        m_worker->start();
    }
}

/**
 * @brief 关闭消息队列、唤醒阻塞的 BusWorker，并等待线程自然退出后释放对象。
 * @details ThreadSafeQueue::shutdown() 使 dequeue() 返回 false，因此不依赖 QThread 事件循环或 quit()。
 */
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

/**
 * @brief 从三个索引收集匹配订阅，并在释放订阅锁后执行回调快照。
 * @details 锁内只读取索引并复制 std::function；锁外调用用户代码，避免回调中的取消订阅导致死锁。
 */
void SystemBus::dispatchToSubscribers(const DataPacket* dataPacket)
{
    if (!dataPacket) {
        qWarning() << QStringLiteral("[SystemBus] 分发失败: dataPacket 为空");
        return;
    }

    std::vector<BusCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        std::vector<SubscriptionId> matchingSubscriptions;
        collectSourceMatches(*dataPacket, matchingSubscriptions);
        collectTargetMatches(*dataPacket, matchingSubscriptions);
        collectWildcardMatches(matchingSubscriptions);
        callbacks = collectPacketCallbacks(matchingSubscriptions);
    }
    invokePacketCallbacks(callbacks, dataPacket);
}

// ========== BusWorker 实现 ==========

/**
 * @brief 持续从有界队列取得消息，并在本线程同步完成全部订阅回调。
 * @details DataPacket 的 unique_ptr 在单次循环内保持有效；所有回调返回后才销毁。队列关闭且无数据时
 *          dequeue() 返回 false，线程退出。慢回调会阻塞后续消息，因此订阅者应快速复制并转交自己的线程。
 */
void SystemBus::BusWorker::run()
{
    while (true) {
        std::unique_ptr<DataPacket> packet;
        if (m_bus->m_messageQueue.dequeue(packet)) {
            m_bus->dispatchToSubscribers(packet.get());
        } else {
            break;
        }
    }
}
