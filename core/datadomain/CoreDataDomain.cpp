#include "core/datadomain/CoreDataDomain.h"

namespace core::domain {

/**
 * @brief 在组合根分别给定容量下构造航迹历史与统一事件日志。
 * @details 构造过程只建立共享存储，不预注册 Reader、不启动线程，也不隐式选择部署容量；任一非法容量
 * 直接沿用对应 Topic 的异常失败，避免应用带着退化数据域继续运行。
 */
CoreDataDomain::CoreDataDomain(qsizetype trackHistoryCapacity, qsizetype eventHistoryCapacity)
    : m_trackTopic(trackHistoryCapacity)
    , m_eventTopic(eventHistoryCapacity)
{
}

/**
 * @brief 向已通过 Producer 门禁的写入路径提供航迹 Topic。
 * @details 返回借用引用而非所有权；数据域生命周期由组合根控制，调用者不得跨越数据域析构继续使用。
 */
HistoryTopic<core::model::TrackPacket>& CoreDataDomain::trackTopic() noexcept
{
    return m_trackTopic;
}

/**
 * @brief 向 WebReader、ArchiveReader 等主动读取方提供只读航迹 Topic。
 * @details const 访问仍允许调用线程安全的 latest/readAfter/stats，但从类型层面禁止 Consumer 发布数据。
 */
const HistoryTopic<core::model::TrackPacket>& CoreDataDomain::trackTopic() const noexcept
{
    return m_trackTopic;
}

/**
 * @brief 向通过门禁的 Device、Track、System 和 Control 事件 Producer 提供统一事件日志。
 * @details 返回引用不代表中央路由；生产者仍只发布强类型事实，事件日志不调用任何消费者。
 */
EventTopic<core::model::SystemEvent>& CoreDataDomain::eventTopic() noexcept
{
    return m_eventTopic;
}

/**
 * @brief 向归档、监控、Web 和日志 Reader 提供只读事件历史。
 * @details 每个 Reader 独立维护 cursor；读取和 retention 不影响其他 Reader 或 Producer。
 */
const EventTopic<core::model::SystemEvent>& CoreDataDomain::eventTopic() const noexcept
{
    return m_eventTopic;
}

} // namespace core::domain
