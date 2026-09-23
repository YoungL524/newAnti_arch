#include "core/producer/SystemEventPublisher.h"

namespace core::producer {
namespace {

/**
 * @brief 判断事件码是否属于当前已冻结的首批登记表。
 * @details 使用穷举 switch，确保新增枚举值必须同时经过门禁评审，不能因数值落在某个范围内而自动获准持久化。
 */
bool isRegisteredEventCode(core::model::EventCode code) noexcept
{
    switch (code) {
    case core::model::EventCode::DeviceMalformedFrame:
    case core::model::EventCode::TrackPublishRejected:
    case core::model::EventCode::SystemTopicSequenceGap:
    case core::model::EventCode::ControlDatagramSendFailed:
    case core::model::EventCode::ControlTaskLifecycleChanged:
        return true;
    }
    return false;
}

/**
 * @brief 检查具体事件码必须携带的关联上下文。
 * @details Topic gap 必须给出丢失闭区间，控制数据报发送失败必须关联请求；其余首批事件允许按
 * 事实发生阶段选择设备、航迹或拒绝原因上下文。
 */
bool hasRequiredContext(const core::model::SystemEvent &event) noexcept
{
    using core::model::EventCode;
    using core::model::EventSubjectFlag;
    if (event.eventCode == EventCode::SystemTopicSequenceGap) {
        return core::model::hasEventSubject(event.flags, EventSubjectFlag::Argument0Valid)
            && core::model::hasEventSubject(event.flags, EventSubjectFlag::Argument1Valid)
            && event.argument0 > 0
            && event.argument1 >= event.argument0;
    }
    if (event.eventCode == EventCode::ControlDatagramSendFailed) {
        return core::model::hasEventSubject(event.flags, EventSubjectFlag::RequestIdValid)
            && event.requestId != 0;
    }
    if (event.eventCode == EventCode::ControlTaskLifecycleChanged) {
        return core::model::hasEventSubject(event.flags, EventSubjectFlag::TaskIdValid)
            && core::model::hasEventSubject(event.flags, EventSubjectFlag::Argument0Valid)
            && event.argument0 >= 1 && event.argument0 <= 4;
    }
    if (event.eventCode == EventCode::TrackPublishRejected) {
        return core::model::hasEventSubject(event.flags, EventSubjectFlag::Argument0Valid)
            && event.argument0 > 0;
    }
    return true;
}

} // namespace

/**
 * @brief 同时检查拒绝原因与非零 Topic sequence，防止构造出自相矛盾的成功结果。
 */
bool EventPublishResult::accepted() const noexcept
{
    return rejection == EventPublishRejection::None && sequence != 0;
}

/**
 * @brief 保存统一 EventTopic 的借用引用，不登记回调、不创建错误域路由或后台线程。
 */
SystemEventPublisher::SystemEventPublisher(
    core::domain::EventTopic<core::model::SystemEvent> &eventTopic) noexcept
    : m_eventTopic(eventTopic)
{
}

/**
 * @brief 在移动 Sample 前完成确定性门禁，成功后才追加一次事件事实。
 * @details 门禁拒绝只累计指标，不尝试向同一 EventTopic 发布“发布失败”事件，从结构上避免递归错误风暴。
 */
EventPublishResult SystemEventPublisher::publish(
    core::model::Sample<core::model::SystemEvent> sample)
{
    const EventPublishRejection rejection = validate(sample);
    if (rejection != EventPublishRejection::None) {
        m_rejectedCount.fetch_add(1, std::memory_order_relaxed);
        return {rejection, 0};
    }

    const core::domain::TopicSequence sequence = m_eventTopic.publish(std::move(sample));
    m_acceptedCount.fetch_add(1, std::memory_order_relaxed);
    return {EventPublishRejection::None, sequence};
}

/**
 * @brief 以 relaxed 顺序读取观测计数，不把指标读取引入事件发布同步路径。
 */
quint64 SystemEventPublisher::acceptedCount() const noexcept
{
    return m_acceptedCount.load(std::memory_order_relaxed);
}

/**
 * @brief 返回门禁拒绝累计快照；并发发布时不承诺与 Topic stats 属于同一时刻。
 */
quint64 SystemEventPublisher::rejectedCount() const noexcept
{
    return m_rejectedCount.load(std::memory_order_relaxed);
}

/**
 * @brief 先验证公共 SampleMeta，再验证事件码登记、域和定长可选字段合同。
 * @details 稳定顺序让同一非法事件总得到相同首因。endpointId 对纯内部 System/Control 事件允许为零；
 * 设备注册真实性和事件限频属于上游 owner，不能由本门禁反向依赖注册表或消费者。
 */
EventPublishRejection SystemEventPublisher::validate(
    const core::model::Sample<core::model::SystemEvent> &sample) noexcept
{
    using namespace core::model;
    const SampleMeta &meta = sample.meta;
    const SystemEvent &event = sample.value;
    constexpr SampleFieldFlags knownMetaFlags =
        sampleFieldBit(SampleFieldFlag::HasSourceSequence)
        | sampleFieldBit(SampleFieldFlag::HasSourceTimeUtcMs);
    constexpr EventSubjectFlags knownSubjectFlags =
        eventSubjectBit(EventSubjectFlag::DeviceIdValid)
        | eventSubjectBit(EventSubjectFlag::TaskIdValid)
        | eventSubjectBit(EventSubjectFlag::AcquisitionIdValid)
        | eventSubjectBit(EventSubjectFlag::TrackIdValid)
        | eventSubjectBit(EventSubjectFlag::RequestIdValid)
        | eventSubjectBit(EventSubjectFlag::Argument0Valid)
        | eventSubjectBit(EventSubjectFlag::Argument1Valid);

    if (meta.receiveTimeUtcMs <= 0) return EventPublishRejection::InvalidReceiveUtcTime;
    if (meta.receiveTimeSteadyNs <= 0) return EventPublishRejection::InvalidReceiveSteadyTime;
    if ((meta.validity & ~knownMetaFlags) != 0
        || (!hasSampleField(meta.validity, SampleFieldFlag::HasSourceSequence) && meta.sourceSequence != 0)
        || (!hasSampleField(meta.validity, SampleFieldFlag::HasSourceTimeUtcMs) && meta.sourceTimeUtcMs != 0)) {
        return EventPublishRejection::InvalidMetaValidity;
    }
    if (hasSampleField(meta.validity, SampleFieldFlag::HasSourceTimeUtcMs)
        && meta.sourceTimeUtcMs <= 0) {
        return EventPublishRejection::InvalidSourceTime;
    }
    if (meta.traceId == 0) return EventPublishRejection::MissingTraceId;
    if (meta.quality != SampleQuality::Good && meta.quality != SampleQuality::Suspect) {
        return EventPublishRejection::InvalidQuality;
    }
    if (!isRegisteredEventCode(event.eventCode)) return EventPublishRejection::UnregisteredEventCode;
    if (event.domain != eventDomainOf(event.eventCode)) return EventPublishRejection::DomainMismatch;
    if (event.severity < EventSeverity::Info || event.severity > EventSeverity::Alarm) {
        return EventPublishRejection::InvalidSeverity;
    }
    if ((event.flags & ~knownSubjectFlags) != 0) return EventPublishRejection::UnknownSubjectFlag;

    const bool deviceValid = hasEventSubject(event.flags, EventSubjectFlag::DeviceIdValid);
    const bool taskValid = hasEventSubject(event.flags, EventSubjectFlag::TaskIdValid);
    const bool acquisitionValid = hasEventSubject(event.flags, EventSubjectFlag::AcquisitionIdValid);
    const bool trackValid = hasEventSubject(event.flags, EventSubjectFlag::TrackIdValid);
    const bool requestValid = hasEventSubject(event.flags, EventSubjectFlag::RequestIdValid);
    const bool argument0Valid = hasEventSubject(event.flags, EventSubjectFlag::Argument0Valid);
    const bool argument1Valid = hasEventSubject(event.flags, EventSubjectFlag::Argument1Valid);
    if (deviceValid != event.deviceId.isSet()
        || taskValid != event.taskId.isSet()
        || acquisitionValid != event.acquisitionId.isSet()
        || (!trackValid && event.trackId != 0)
        || (trackValid && !deviceValid)
        || requestValid != (event.requestId != 0)
        || (!argument0Valid && event.argument0 != 0)
        || (!argument1Valid && event.argument1 != 0)) {
        return EventPublishRejection::InvalidSubjectValue;
    }
    if (event.messageTemplateId != 0) return EventPublishRejection::UnregisteredMessageTemplate;
    if (!hasRequiredContext(event)) return EventPublishRejection::MissingRequiredContext;
    return EventPublishRejection::None;
}

} // namespace core::producer
