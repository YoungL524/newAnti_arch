#pragma once

#include <functional>
#include <cstdint>
#include "GenericPackets.h"

// ========== 类型定义 ==========
using SubscriptionId = uint32_t;

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

struct BusEnvelope;
using BusEnvelopeCallback = std::function<void(const BusEnvelope&)>;

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
 */
class ISystemBus
{
public:
    virtual ~ISystemBus() = default;

    // ========== 数据发布接口 ==========
    /**
     * @brief 发布数据到系统总线
     * @param dataPacket 要发布的数据包
     * @return 是否成功发布
     * @note 发布的数据会被分发给所有匹配的订阅者
     */
    virtual bool publish(const DataPacket* dataPacket) = 0;

    // ========== 数据订阅接口 ==========
    /**
     * @brief 订阅特定数据源类型的数据
     * @param sourceType 数据源类型
     * @param callback 接收数据的回调函数
     * @return 订阅ID，用于取消订阅
     *
     * @note callback 在 SystemBus 内部工作线程执行，不可直接操作 QObject/UI/SQLite。
     *       见 BusCallback 文档的线程安全说明。
     */
    virtual SubscriptionId subscribeBySource(uint16_t sourceType, BusCallback callback) = 0;

    /**
     * @brief 按源类型订阅，回调接收完整 BusEnvelope（含大对象载荷）
     */
    virtual SubscriptionId subscribeBySourceEnvelope(uint16_t sourceType,
                                                     BusEnvelopeCallback callback) = 0;

    /**
     * @brief 订阅特定分发目标的数据
     * @param target 分发目标
     * @param callback 接收数据的回调函数
     * @return 订阅ID，用于取消订阅
     *
     * @note callback 在 SystemBus 内部工作线程执行，不可直接操作 QObject/UI/SQLite。
     *       见 BusCallback 文档的线程安全说明。
     */
    virtual SubscriptionId subscribeByTarget(uint16_t target, BusCallback callback) = 0;

    /**
     * @brief 订阅同时匹配数据源类型和分发目标的数据
     * @param sourceType 数据源类型
     * @param target 分发目标
     * @param callback 接收数据的回调函数
     * @return 订阅ID，用于取消订阅
     *
     * @note callback 在 SystemBus 内部工作线程执行，不可直接操作 QObject/UI/SQLite。
     *       见 BusCallback 文档的线程安全说明。
     */
    virtual SubscriptionId subscribe(uint16_t sourceType, uint16_t target, BusCallback callback) = 0;

    /**
     * @brief 订阅所有数据（通配符订阅）
     * @param callback 接收数据的回调函数
     * @return 订阅ID，用于取消订阅
     * @note 此订阅会接收总线上的所有数据，请谨慎使用
     */
    virtual SubscriptionId subscribeAll(BusCallback callback) = 0;

    /**
     * @brief 取消数据订阅
     * @param subscriptionId 订阅ID
     * @return 是否成功取消订阅
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
