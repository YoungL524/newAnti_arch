#pragma once

#include "ISystemBus.h"
#include "ThreadSafeQueue.h"
#include "GenericPackets.h"
#include <QThread>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

/**
 * @brief 系统总线实现类
 * @details 实现ISystemBus接口，作为数据高速公路的核心实现
 *          基于DDS发布订阅模式，提供高性能、线程安全的数据分发服务
 *          支持基于数据源类型、分发目标和组合条件的灵活订阅机制
 */
class SystemBus : public ISystemBus
{
public:
    /**
     * @brief 总线运行状态枚举
     * @details shutdown 是不可逆操作；一旦进入 ShutDown 状态，总线不可重启
     */
    enum class State : uint8_t {
        Running,    ///< 正常运行中，可发布/订阅
        ShutDown    ///< 已永久关闭，所有操作返回失败
    };

    static SystemBus& getInstance();

    void shutdown();

    // ========== ISystemBus接口实现 ==========
    bool publish(const DataPacket* dataPacket) override;
    bool publish(const DataPacket& dataPacket);
    bool publish(DataPacket&& dataPacket);
    SubscriptionId subscribeBySource(uint16_t sourceType, BusCallback callback) override;
    SubscriptionId subscribeByTarget(uint16_t target, BusCallback callback) override;
    SubscriptionId subscribe(uint16_t sourceType, uint16_t target, BusCallback callback) override;
    SubscriptionId subscribeAll(BusCallback callback) override;
    bool unsubscribe(SubscriptionId subscriptionId) override;
    size_t getSubscriptionCount() const override;
    uint64_t getPublishedCount() const override;
    bool isShutdown() const override;
    void clearAllSubscriptions() override;

    /**
     * @brief 查询总线当前状态
     * @return 总线状态枚举值
     */
    State state() const;

    /**
     * @brief 总线是否处于正常运行状态
     */
    bool isRunning() const;

private:
    /**
     * @brief 构造函数（私有，单例模式）
     */
    SystemBus();

    /**
     * @brief 析构函数
     */
    ~SystemBus() override;

private:
    /**
     * @brief 总线工作线程
     * @details 独立线程中从消息队列取出数据包并分发给订阅者
     */
    class BusWorker : public QThread
    {
    public:
        explicit BusWorker(SystemBus* bus) : m_bus(bus) {
        }

    protected:
        void run() override;

    private:
        SystemBus* m_bus;
    };

    /**
     * @brief 订阅信息结构体
     * @details 记录每个订阅的匹配条件和回调函数
     */
    struct SubscriptionInfo {
        // 标签类型用于区分构造函数
        struct BySource {};
        struct ByTarget {};
        
        uint16_t sourceType;
        uint16_t target;
        BusCallback callback;
        bool hasSourceType;
        bool hasTarget;
        bool isWildcard;

        // 按数据源类型订阅
        SubscriptionInfo(BySource, uint16_t src, BusCallback cb)
            : sourceType(src), callback(std::move(cb)), hasSourceType(true), hasTarget(false), isWildcard(false) {}

        // 按分发目标订阅
        SubscriptionInfo(ByTarget, uint16_t tgt, BusCallback cb)
            : target(tgt), callback(std::move(cb)), hasSourceType(false), hasTarget(true), isWildcard(false) {}

        // 组合条件订阅
        SubscriptionInfo(uint16_t src, uint16_t tgt, BusCallback cb)
            : sourceType(src), target(tgt), callback(std::move(cb)), hasSourceType(true), hasTarget(true), isWildcard(false) {}

        // 通配符订阅
        SubscriptionInfo(BusCallback cb)
            : callback(std::move(cb)), hasSourceType(false), hasTarget(false), isWildcard(true) {}
    };

    /**
     * @brief 分发数据包到所有匹配的订阅者
     * @param dataPacket 要分发的数据包
     */
    void dispatchToSubscribers(const DataPacket* dataPacket);

    /**
     * @brief 注册一个订阅并返回订阅 ID
     * @param info 订阅信息
     * @return 新生成的订阅 ID
     * @details 统一负责写入主订阅表以及各类索引，避免多个 subscribe 接口重复实现。
     */
    SubscriptionId registerSubscription(SubscriptionInfo info);

    /**
     * @brief 校验订阅回调是否合法
     * @param callback 订阅回调
     * @param operation 操作名，用于日志输出
     * @return 回调是否可用
     */
    bool validateCallback(const BusCallback& callback, const QString& operation) const;

    /**
     * @brief 向源类型索引追加订阅 ID
     * @param sourceType 源类型
     * @param subscriptionId 订阅 ID
     */
    void addSourceIndex(uint16_t sourceType, SubscriptionId subscriptionId);

    /**
     * @brief 向目标索引追加订阅 ID
     * @param target 分发目标
     * @param subscriptionId 订阅 ID
     */
    void addTargetIndex(uint16_t target, SubscriptionId subscriptionId);

    /**
     * @brief 向通配符索引追加订阅 ID
     * @param subscriptionId 订阅 ID
     */
    void addWildcardIndex(SubscriptionId subscriptionId);

    /**
     * @brief 从源类型索引移除订阅 ID
     * @param sourceType 源类型
     * @param subscriptionId 订阅 ID
     */
    void removeSourceIndex(uint16_t sourceType, SubscriptionId subscriptionId);

    /**
     * @brief 从目标索引移除订阅 ID
     * @param target 分发目标
     * @param subscriptionId 订阅 ID
     */
    void removeTargetIndex(uint16_t target, SubscriptionId subscriptionId);

    /**
     * @brief 从通配符索引移除订阅 ID
     * @param subscriptionId 订阅 ID
     */
    void removeWildcardIndex(SubscriptionId subscriptionId);

    /**
     * @brief 从订阅 ID 列表中移除指定元素
     * @param subscriptions 订阅 ID 列表
     * @param subscriptionId 订阅 ID
     */
    static void eraseSubscriptionId(std::vector<SubscriptionId>& subscriptions,
                                    SubscriptionId subscriptionId);

    /**
     * @brief 向匹配列表追加唯一订阅 ID
     * @param matchingSubscriptions 当前匹配结果
     * @param subscriptionId 待追加订阅 ID
     */
    static void appendUniqueSubscription(std::vector<SubscriptionId>& matchingSubscriptions,
                                         SubscriptionId subscriptionId);

    /**
     * @brief 判断订阅是否匹配当前数据包目标
     * @param info 订阅信息
     * @param dataPacket 数据包
     * @return 是否目标匹配
     */
    static bool matchesTarget(const SubscriptionInfo& info, const DataPacket& dataPacket);

    /**
     * @brief 收集按源类型命中的订阅
     * @param dataPacket 数据包
     * @param matchingSubscriptions 输出匹配结果
     */
    void collectSourceMatches(const DataPacket& dataPacket,
                              std::vector<SubscriptionId>& matchingSubscriptions) const;

    /**
     * @brief 收集按目标命中的订阅
     * @param dataPacket 数据包
     * @param matchingSubscriptions 输出匹配结果
     */
    void collectTargetMatches(const DataPacket& dataPacket,
                              std::vector<SubscriptionId>& matchingSubscriptions) const;

    /**
     * @brief 收集通配符订阅
     * @param matchingSubscriptions 输出匹配结果
     */
    void collectWildcardMatches(std::vector<SubscriptionId>& matchingSubscriptions) const;

    /**
     * @brief 将订阅 ID 列表解析为待执行回调列表
     * @param matchingSubscriptions 已匹配的订阅 ID 列表
     * @return 回调列表
     */
    std::vector<BusCallback> collectCallbacks(const std::vector<SubscriptionId>& matchingSubscriptions) const;

    /**
     * @brief 顺序执行回调列表
     * @param callbacksToInvoke 待执行回调列表
     * @param dataPacket 数据包
     */
    void invokeCallbacks(const std::vector<BusCallback>& callbacksToInvoke,
                         const DataPacket* dataPacket) const;

    void startProcessing();
    void stopProcessing();

private:
    // ========== 订阅管理 ==========
    std::unordered_map<SubscriptionId, SubscriptionInfo> m_subscriptions;          // 订阅信息映射
    std::unordered_map<uint16_t, std::vector<SubscriptionId>> m_sourceTypeSubscriptions; // 按源类型分组
    std::unordered_map<uint16_t, std::vector<SubscriptionId>> m_targetSubscriptions;     // 按分发目标分组
    std::vector<SubscriptionId> m_wildcardSubscriptions;                           // 通配符订阅列表
    mutable std::mutex m_subscriptionsMutex;                                       // 订阅信息互斥锁
    std::atomic<SubscriptionId> m_nextSubscriptionId;                              // 下一个订阅ID

    // ========== 统计信息 ==========
    std::atomic<uint64_t> m_publishedCount;                                        // 发布计数
    std::atomic<State> m_state{State::Running};                                    // 总线状态（不可逆转为 ShutDown）

    ThreadSafeQueue<DataPacket> m_messageQueue;                                    // 消息队列
    BusWorker* m_worker{nullptr};                                                  // 工作线程

    // 禁用拷贝构造和赋值
    SystemBus(const SystemBus&) = delete;
    SystemBus& operator=(const SystemBus&) = delete;
};
