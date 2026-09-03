/**
 * @file SystemEventPublisher.h
 * @brief 定义 SystemEvent 进入统一有界事件日志前的发布门禁。
 * @details 上游四类事件 Producer 只构造一个 Sample<SystemEvent>，本类校验元数据、稳定事件码、域、级别、
 * flags 与字段清零规则后写入 EventTopic。它不格式化中文文本、不做错误风暴抑制、不更新 LatestState、
 * 不返回 CommandResult；新人应从 EventPublishRejection 和 publish() 的拒绝不写入保证开始阅读。
 */

#pragma once

#include "core/datadomain/EventTopic.h"
#include "core/model/SystemEvent.h"

#include <QtGlobal>

#include <atomic>

namespace core::producer {

/** @brief 表示 SystemEventPublisher 可从候选 Sample 独立确定的拒绝原因。 */
enum class EventPublishRejection : quint8
{
    None = 0, ///< 事件通过门禁并成功进入 EventTopic。
    InvalidReceiveUtcTime, ///< receiveTimeUtcMs 不是正 Unix epoch 毫秒。
    InvalidReceiveSteadyTime, ///< receiveTimeSteadyNs 不是正单调纳秒值。
    InvalidMetaValidity, ///< SampleMeta 含未知位，或未置位的源字段没有清零。
    InvalidSourceTime, ///< 已声明有效的 sourceTimeUtcMs 不是正 Unix epoch 毫秒。
    MissingTraceId, ///< traceId 为零，事件不能关联到产生它的处理链。
    InvalidQuality, ///< SampleQuality 超出 Good 与 Suspect。
    UnregisteredEventCode, ///< eventCode 不是当前登记表中的稳定事件码。
    DomainMismatch, ///< domain 与 eventCode 高 8 位不一致。
    InvalidSeverity, ///< severity 超出 Info 至 Alarm。
    UnknownSubjectFlag, ///< flags 含尚未冻结语义的位。
    InvalidSubjectValue, ///< 未置位字段未清零、置位 ID 未绑定，或 TrackId 缺少 DeviceId。
    UnregisteredMessageTemplate, ///< 第一版尚无模板注册表，但 messageTemplateId 非零。
    MissingRequiredContext ///< 具体事件码要求的 requestId 或诊断参数没有声明有效。
};

/** @brief 表示一次事件发布尝试的无动态分配结果。 */
struct EventPublishResult
{
    EventPublishRejection rejection = EventPublishRejection::None; ///< 成功为 None，失败为首个稳定拒绝原因。
    core::domain::TopicSequence sequence = 0; ///< 成功发布的事件 Topic sequence；拒绝时固定为零。

    /**
     * @brief 判断候选事件是否真正写入统一事件日志。
     * @return rejection 为 None 且 sequence 非零时返回 true。
     */
    bool accepted() const noexcept;
};

/**
 * @brief 校验定长事件合同并向唯一 EventTopic<SystemEvent> 发布。
 * @details 类不拥有 EventTopic、不创建线程或队列；多个 Producer 可共享本入口，统计只用于运行观测。
 */
class SystemEventPublisher final
{
public:
    /**
     * @brief 绑定 CoreDataDomain 拥有的统一事件日志。
     * @param eventTopic 生命周期必须覆盖本对象与所有并发 publish 调用的 EventTopic。
     */
    explicit SystemEventPublisher(
        core::domain::EventTopic<core::model::SystemEvent> &eventTopic) noexcept;

    /** @brief 禁止复制，避免误认为 Publisher 拥有或复制 EventTopic。 */
    SystemEventPublisher(const SystemEventPublisher&) = delete;

    /** @brief 禁止复制赋值，保持构造时绑定的统一事件日志不可替换。 */
    SystemEventPublisher& operator=(const SystemEventPublisher&) = delete;

    /**
     * @brief 校验一条已发生事实，并仅在合同全部成立时追加到事件日志。
     * @param sample 已填充发生/接收时间、trace 和紧凑事件字段的候选事实；函数取得其所有权。
     * @return 成功时返回非零 Topic sequence；拒绝时返回稳定原因且 sequence 为零。
     * @post 拒绝不会改变 EventTopic 水位，也不会递归产生另一条错误事件。
     */
    EventPublishResult publish(core::model::Sample<core::model::SystemEvent> sample);

    /** @brief 返回进程内累计成功发布事件数，不因 Topic 覆盖而减少。 */
    quint64 acceptedCount() const noexcept;

    /** @brief 返回进程内累计门禁拒绝数，不包含 EventTopic 抛出的基础设施异常。 */
    quint64 rejectedCount() const noexcept;

private:
    /**
     * @brief 按固定顺序检查公共元数据、事件登记和可选字段一致性。
     * @param sample 不会被修改的候选事件。
     * @return 全部通过为 None，否则返回首个确定拒绝原因。
     */
    static EventPublishRejection validate(
        const core::model::Sample<core::model::SystemEvent> &sample) noexcept;

    core::domain::EventTopic<core::model::SystemEvent> &m_eventTopic; ///< 借用的唯一有界事件日志。
    std::atomic<quint64> m_acceptedCount {0}; ///< 多 Producer 并发成功累计值，仅用于指标。
    std::atomic<quint64> m_rejectedCount {0}; ///< 多 Producer 并发拒绝累计值，不参与业务状态。
};

} // namespace core::producer
