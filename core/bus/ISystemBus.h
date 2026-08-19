#pragma once
/**
 * @file ISystemBus.h
 * @brief 定义进程内唯一消息总线的发布、订阅及线程边界。
 * @details 业务模块只依赖本接口和 DataPacket，不直接依赖具体队列或工作线程。发布者把消息所有权交给
 *          总线，订阅者在总线工作线程中读取同一份只读消息；需要跨到 QObject 所在线程时必须先复制。
 *          具体的有界队列、索引和关闭策略由 SystemBus 实现。
 */


#include <functional>
#include <cstdint>
#include "GenericPackets.h"

// ========== 类型定义 ==========
using SubscriptionId = uint32_t; ///< 进程内订阅句柄；0 保留为订阅失败，非零值用于取消订阅。

// 系统总线回调函数类型
/**
 * @brief 系统总线回调函数类型
 * @warning 回调运行在 SystemBus 内部工作线程，不是主线程。
 *          不要在回调里直接操作 QObject、UI、SQLite 等线程敏感对象。
 *          推荐模式：拷贝 DataPacket，再用 QMetaObject::invokeMethod
 *          (target, "slot", Qt::QueuedConnection, Q_ARG(DataPacket, copy))
 *          转回 owner 线程处理。
 */
using BusCallback = std::function<void(const DataPacket*)>;

/**
 * @brief 系统总线接口
 * @details 基于DDS发布订阅模式的数据高速公路
 *          作为系统核心，承载所有数据流转
 *          提供高性能、线程安全的数据发布订阅服务
 *
 * 设计理念：
 * - 系统总线是"高速公路"，所有数据都在此流转
 * - 采用纯粹的发布订阅模式，支持多对多通信
 * - 提供基于数据源类型和分发目标的灵活订阅机制
 * - 保证数据传输的可靠性和实时性
 *
 * DataPacket 是唯一顶层消息；固定协议和复杂业务载荷均由其 variant 承载。
 */
class ISystemBus
{
public:
    virtual ~ISystemBus() = default;

    // ========== 数据发布接口 ==========
    /**
     * @brief 发布数据到系统总线
     * @param[in] dataPacket 发布期间有效的只读数据包；不得为空。
     * @return 数据包通过类型一致性检查并进入有界队列时返回 true；关闭、空指针或队列满时返回 false。
     * @note 返回 true 只表示成功入队，不表示任何订阅者已经完成处理。
     */
    virtual bool publish(const DataPacket* dataPacket) = 0;

    // ========== 数据订阅接口 ==========
    /**
     * @brief 订阅特定数据源类型的数据
     * @param[in] sourceType DataPacket::sourceType 的精确匹配值。
     * @param[in] callback 在总线工作线程执行的回调，不得为空。
     * @return 非零订阅 ID；总线已关闭或回调为空时返回 0。
     *
     * @note callback 在 SystemBus 内部工作线程执行，不可直接操作 QObject/UI/SQLite。
     *       见 BusCallback 文档的线程安全说明。
     */
    virtual SubscriptionId subscribeBySource(uint16_t sourceType, BusCallback callback) = 0;

    /**
     * @brief 订阅特定分发目标的数据
     * @param[in] target DataPacket::dispatchTarget 的精确匹配值。
     * @param[in] callback 在总线工作线程执行的回调，不得为空。
     * @return 非零订阅 ID；总线已关闭或回调为空时返回 0。
     *
     * @note callback 在 SystemBus 内部工作线程执行，不可直接操作 QObject/UI/SQLite。
     *       见 BusCallback 文档的线程安全说明。
     */
    virtual SubscriptionId subscribeByTarget(uint16_t target, BusCallback callback) = 0;

    /**
     * @brief 订阅同时匹配数据源类型和分发目标的数据
     * @param[in] sourceType 必须同时匹配的 DataPacket::sourceType。
     * @param[in] target 必须同时匹配的 DataPacket::dispatchTarget。
     * @param[in] callback 在总线工作线程执行的回调，不得为空。
     * @return 非零订阅 ID；总线已关闭或回调为空时返回 0。
     *
     * @note callback 在 SystemBus 内部工作线程执行，不可直接操作 QObject/UI/SQLite。
     *       见 BusCallback 文档的线程安全说明。
     */
    virtual SubscriptionId subscribe(uint16_t sourceType, uint16_t target, BusCallback callback) = 0;

    /**
     * @brief 订阅所有数据（通配符订阅）
     * @param[in] callback 接收全部 DataPacket 的工作线程回调。
     * @return 非零订阅 ID；总线已关闭或回调为空时返回 0。
     * @note 此订阅会接收总线上的所有数据，请谨慎使用
     */
    virtual SubscriptionId subscribeAll(BusCallback callback) = 0;

    /**
     * @brief 取消数据订阅
     * @param[in] subscriptionId 先前订阅返回的非零 ID。
     * @return 找到并从主表及全部索引移除时返回 true；未知 ID 或总线已关闭时返回 false。
     */
    virtual bool unsubscribe(SubscriptionId subscriptionId) = 0;

    // ========== 状态查询接口 ==========
    /**
     * @brief 总线是否已关闭
     * @return 是否已关闭
     * @note shutdown 是不可逆操作，关闭后所有发布/订阅操作返回失败
     */
    virtual bool isShutdown() const = 0;

    /**
     * @brief 获取当前订阅数量
     * @return 订阅数量
     */
    virtual size_t getSubscriptionCount() const = 0;

    /**
     * @brief 获取发布的数据包总数
     * @return 数据包总数
     */
    virtual uint64_t getPublishedCount() const = 0;

    /**
     * @brief 清除所有订阅
     */
    virtual void clearAllSubscriptions() = 0;
};
