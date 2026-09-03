/**
 * @file EntityIds.h
 * @brief 定义实时数据链使用的紧凑、强类型进程内实体句柄。
 * @details 本文件位于配置注册表与 SampleMeta/TrackPacket 之间：上游注册表把 endpoint、设备、任务、阵面和
 * 采集会话的外部稳定标识映射为定宽数值句柄，下游热链路只复制这些句柄。句柄值不携带名称、不拥有注册表，
 * 也不是数据库、网络或录制文件中的稳定主键；新人应先阅读各别名的零值与作用域，再阅读标识符合同。
 */

#pragma once

#include <QHashFunctions>
#include <QtGlobal>

#include <type_traits>

namespace core::model {

/**
 * @brief 用零额外存储成本把无符号整数包装成不可混用的实体句柄。
 * @tparam Tag 区分业务实体种类的空标签；不同 Tag 之间不能隐式转换或比较。
 * @tparam Storage 保存注册表编号的无符号整数类型。
 * @details 默认值和底层零值统一表示“未绑定”。非零值只能由拥有相应注册表或会话分配器的模块创建；该类型
 * 不验证编号是否仍存在于注册表中，也不允许隐式退化为整数，从而避免把 DeviceId 误传为 EndpointId。
 */
template<typename Tag, typename Storage>
class CompactId final
{
    static_assert(std::is_integral<Storage>::value, "CompactId storage must be integral");
    static_assert(std::is_unsigned<Storage>::value, "CompactId storage must be unsigned");

public:
    /** @brief 暴露底层定宽整数类型，供布局测试和边界适配器显式转换。 */
    using StorageType = Storage;

    /** @brief 构造未绑定句柄；底层值为零，不代表任何已注册实体。 */
    constexpr CompactId() noexcept = default;

    /**
     * @brief 从注册表或会话分配器给出的原始编号显式构造句柄。
     * @param value 当前进程注册域中的编号；零值产生未绑定句柄。
     * @return 与原始编号位模式相同的强类型句柄。
     * @warning 本函数不检查编号是否存在；协议适配器不得直接用厂家字段绕过注册表调用它。
     */
    static constexpr CompactId fromRaw(Storage value) noexcept
    {
        return CompactId(value);
    }

    /**
     * @brief 返回边界适配或哈希计算所需的底层编号。
     * @return 当前进程注册域中的原始数值；零表示未绑定。
     */
    constexpr Storage value() const noexcept
    {
        return m_value;
    }

    /**
     * @brief 判断句柄是否引用了一个非零注册编号。
     * @return 底层值非零时返回 true；存在性仍需相应注册表确认。
     */
    constexpr bool isSet() const noexcept
    {
        return m_value != 0;
    }

    /**
     * @brief 提供显式布尔检查，避免句柄隐式参与整数运算。
     * @return 与 isSet() 相同。
     */
    explicit constexpr operator bool() const noexcept
    {
        return isSet();
    }

    /** @brief 比较两个同类句柄是否引用同一注册编号。 */
    friend constexpr bool operator==(CompactId left, CompactId right) noexcept
    {
        return left.m_value == right.m_value;
    }

    /** @brief 比较两个同类句柄是否引用不同注册编号。 */
    friend constexpr bool operator!=(CompactId left, CompactId right) noexcept
    {
        return !(left == right);
    }

    /** @brief 按无符号编号排序，供确定性序列化和有序容器使用。 */
    friend constexpr bool operator<(CompactId left, CompactId right) noexcept
    {
        return left.m_value < right.m_value;
    }

private:
    /** @brief 只允许 fromRaw() 显式进入原始编号，阻断整数隐式构造。 */
    explicit constexpr CompactId(Storage value) noexcept
        : m_value(value)
    {
    }

    Storage m_value = 0; ///< 当前进程注册域编号；零统一表示未绑定。
};

namespace detail {

/** @brief 为 EndpointId 建立独立编译期类型身份，不产生运行时存储。 */
struct EndpointIdTag final {};

/** @brief 为 DeviceId 建立独立编译期类型身份，不产生运行时存储。 */
struct DeviceIdTag final {};

/** @brief 为 TaskId 建立独立编译期类型身份，不产生运行时存储。 */
struct TaskIdTag final {};

/** @brief 为 AcquisitionId 建立独立编译期类型身份，不产生运行时存储。 */
struct AcquisitionIdTag final {};

} // namespace detail

/**
 * @brief 通信端点注册表的 32 位进程内句柄。
 * @note 零表示来源端点未绑定；不得把数值跨进程持久化后当作 endpoint 的稳定业务 ID。
 */
using EndpointId = CompactId<detail::EndpointIdTag, quint32>;

/**
 * @brief 设备台账的 32 位进程内句柄。
 * @note 零表示设备未绑定；真实设备名称和外部稳定 ID 只在 DeviceRegistry 中保存一次。
 */
using DeviceId = CompactId<detail::DeviceIdTag, quint32>;

/**
 * @brief 任务注册表的 32 位进程内句柄。
 * @note 零明确表示采集瞬间没有活动任务，因此 TrackPacket 可以合法携带默认 TaskId。
 */
using TaskId = CompactId<detail::TaskIdTag, quint32>;

/**
 * @brief 采集会话注册表的 64 位进程内句柄。
 * @note 零表示没有活动采集会话；跨重启持久身份由会话注册表映射的外部 UUID/数据库键承担。
 */
using AcquisitionId = CompactId<detail::AcquisitionIdTag, quint64>;

/**
 * @brief 为任意 CompactId 提供 Qt 哈希容器所需的稳定进程内哈希。
 * @tparam Tag 实体种类标签。
 * @tparam Storage 句柄底层无符号整数类型。
 * @param id 待哈希的强类型句柄。
 * @param seed Qt 容器提供的随机种子。
 * @return 对底层编号应用 Qt qHash 后的结果。
 */
template<typename Tag, typename Storage>
inline uint qHash(CompactId<Tag, Storage> id, uint seed = 0) noexcept
{
    return ::qHash(id.value(), seed);
}

static_assert(sizeof(EndpointId) == sizeof(quint32), "EndpointId must have zero storage overhead");
static_assert(sizeof(DeviceId) == sizeof(quint32), "DeviceId must have zero storage overhead");
static_assert(sizeof(TaskId) == sizeof(quint32), "TaskId must have zero storage overhead");
static_assert(sizeof(AcquisitionId) == sizeof(quint64), "AcquisitionId must have zero storage overhead");

} // namespace core::model
