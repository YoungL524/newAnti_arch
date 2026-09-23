/**
 * @file tst_SystemEventPublisher.cpp
 * @brief 验证紧凑 SystemEvent、四域首批事件码、发布门禁和统一事件日志接入。
 * @details 测试防止动态错误载荷、四域拆 Topic、事件码与域不一致、无效字段未清零以及拒绝事件污染日志。
 */

#include "core/producer/SystemEventPublisher.h"

#include <QtTest>

#include <cstddef>
#include <type_traits>

namespace {

/**
 * @brief 构造具有合法公共时间和 trace 的设备非法帧事件。
 * @return 可直接通过门禁的 Warning 事件；测试按场景修改业务字段。
 */
core::model::Sample<core::model::SystemEvent> makeDeviceEvent()
{
    core::model::Sample<core::model::SystemEvent> sample;
    sample.meta.receiveTimeUtcMs = 1'777'000'000'000;
    sample.meta.receiveTimeSteadyNs = 1'000;
    sample.meta.traceId = 9;
    sample.meta.endpointId = core::model::EndpointId::fromRaw(1);
    sample.value.eventCode = core::model::EventCode::DeviceMalformedFrame;
    sample.value.domain = core::model::EventDomain::Device;
    sample.value.severity = core::model::EventSeverity::Warning;
    return sample;
}

/**
 * @brief 把基础事件改为航迹门禁拒绝，并按登记合同填写拒绝原因参数。
 * @return argument0 有效且为正的 Track 域事件。
 */
core::model::Sample<core::model::SystemEvent> makeTrackEvent()
{
    auto sample = makeDeviceEvent();
    sample.value.eventCode = core::model::EventCode::TrackPublishRejected;
    sample.value.domain = core::model::EventDomain::Track;
    sample.value.severity = core::model::EventSeverity::Error;
    sample.value.argument0 = 1;
    sample.value.flags = core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument0Valid);
    return sample;
}

/**
 * @brief 构造表示 Reader 丢失 sequence 10 至 12 的 System 域 gap 事件。
 * @return 两个诊断参数均有效、闭区间有序的事件。
 */
core::model::Sample<core::model::SystemEvent> makeGapEvent()
{
    auto sample = makeDeviceEvent();
    sample.value.eventCode = core::model::EventCode::SystemTopicSequenceGap;
    sample.value.domain = core::model::EventDomain::System;
    sample.value.severity = core::model::EventSeverity::Error;
    sample.value.argument0 = 10;
    sample.value.argument1 = 12;
    sample.value.flags =
        core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument0Valid)
        | core::model::eventSubjectBit(core::model::EventSubjectFlag::Argument1Valid);
    return sample;
}

/**
 * @brief 构造请求 88 的控制数据报未能交给本地 UDP Socket 的 Control 域事件。
 * @return 声明 requestId 有效的本地发送失败事件。
 */
core::model::Sample<core::model::SystemEvent> makeControlEvent()
{
    auto sample = makeDeviceEvent();
    sample.value.eventCode = core::model::EventCode::ControlDatagramSendFailed;
    sample.value.domain = core::model::EventDomain::Control;
    sample.value.severity = core::model::EventSeverity::Error;
    sample.value.requestId = 88;
    sample.value.flags =
        core::model::eventSubjectBit(core::model::EventSubjectFlag::RequestIdValid);
    return sample;
}

} // namespace

/** @brief 汇总 SystemEvent 模型与统一发布门禁的合同测试。 */
class SystemEventPublisherTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收事件事实定长、平凡复制且字段偏移稳定。 */
    void compactLayoutIsFrozen();

    /** @brief 验收四个错误域共享一个 EventTopic 并保持发布顺序。 */
    void firstRegisteredCodeOfEachDomainPublishes();

    /** @brief 验收事件码与 domain 不一致时拒绝且不改变 Topic 水位。 */
    void rejectsDomainMismatchWithoutPublishing();

    /** @brief 验收 flags、清零规则和具体事件所需上下文。 */
    void rejectsInvalidSubjectContracts();
};

/**
 * @brief 固化 56 字节布局及关键字段顺序，防止字符串、optional 或隐藏指针进入事件热结构。
 * @details 对象布局仅用于进程内资源预算，不允许直接作为数据库或网络序列化格式。
 */
void SystemEventPublisherTest::compactLayoutIsFrozen()
{
    static_assert(std::is_trivially_copyable<core::model::SystemEvent>::value,
                  "SystemEvent must be trivially copyable");
    QCOMPARE(sizeof(core::model::SystemEvent), std::size_t(56));
    QCOMPARE(alignof(core::model::SystemEvent), std::size_t(8));
    QCOMPARE(offsetof(core::model::SystemEvent, acquisitionId), std::size_t(0));
    QCOMPARE(offsetof(core::model::SystemEvent, requestId), std::size_t(8));
    QCOMPARE(offsetof(core::model::SystemEvent, argument0), std::size_t(16));
    QCOMPARE(offsetof(core::model::SystemEvent, argument1), std::size_t(24));
    QCOMPARE(offsetof(core::model::SystemEvent, deviceId), std::size_t(32));
    QCOMPARE(offsetof(core::model::SystemEvent, taskId), std::size_t(36));
    QCOMPARE(offsetof(core::model::SystemEvent, trackId), std::size_t(40));
    QCOMPARE(offsetof(core::model::SystemEvent, eventCode), std::size_t(44));
    QCOMPARE(offsetof(core::model::SystemEvent, messageTemplateId), std::size_t(48));
    QCOMPARE(offsetof(core::model::SystemEvent, flags), std::size_t(52));
    QCOMPARE(offsetof(core::model::SystemEvent, domain), std::size_t(54));
    QCOMPARE(offsetof(core::model::SystemEvent, severity), std::size_t(55));
}

/**
 * @brief 依次发布 Device、Track、System、Control 事件并由一个 cursor 顺序读回。
 * @details 该测试直接防止实现按错误域拆成多个 EventTopic，四条事实必须共享同一 sequence 空间。
 */
void SystemEventPublisherTest::firstRegisteredCodeOfEachDomainPublishes()
{
    core::domain::EventTopic<core::model::SystemEvent> topic(8);
    core::domain::TopicScratchBuffer<core::model::SystemEvent> scratch(8);
    core::producer::SystemEventPublisher publisher(topic);

    QVERIFY(publisher.publish(makeDeviceEvent()).accepted());
    QVERIFY(publisher.publish(makeTrackEvent()).accepted());
    QVERIFY(publisher.publish(makeGapEvent()).accepted());
    QVERIFY(publisher.publish(makeControlEvent()).accepted());
    QCOMPARE(publisher.acceptedCount(), quint64(4));
    QCOMPARE(publisher.rejectedCount(), quint64(0));

    const auto batch = topic.readAfter(0, scratch);
    QCOMPARE(batch.count, qsizetype(4));
    QCOMPARE(scratch[0].sample.value.eventCode, core::model::EventCode::DeviceMalformedFrame);
    QCOMPARE(scratch[1].sample.value.eventCode, core::model::EventCode::TrackPublishRejected);
    QCOMPARE(scratch[2].sample.value.eventCode, core::model::EventCode::SystemTopicSequenceGap);
    QCOMPARE(scratch[3].sample.value.eventCode, core::model::EventCode::ControlDatagramSendFailed);
}

/**
 * @brief 把设备事件伪装成 System 域并确认门禁返回稳定首因。
 * @details 拒绝后 stats 仍为空，证明错误门禁不会递归向同一事件日志写入另一条错误。
 */
void SystemEventPublisherTest::rejectsDomainMismatchWithoutPublishing()
{
    core::domain::EventTopic<core::model::SystemEvent> topic(4);
    core::producer::SystemEventPublisher publisher(topic);
    auto sample = makeDeviceEvent();
    sample.value.domain = core::model::EventDomain::System;

    const auto result = publisher.publish(sample);
    QVERIFY(!result.accepted());
    QVERIFY(result.rejection == core::producer::EventPublishRejection::DomainMismatch);
    QCOMPARE(result.sequence, core::domain::TopicSequence(0));
    QCOMPARE(topic.stats().size, qsizetype(0));
    QCOMPARE(publisher.rejectedCount(), quint64(1));
}

/**
 * @brief 分别制造无标志参数、孤立 trackId 和缺少 requestId 的控制发送失败。
 * @details 每次拒绝均不写入 Topic，确保消费者和归档永远只看到满足字段解释合同的事件。
 */
void SystemEventPublisherTest::rejectsInvalidSubjectContracts()
{
    core::domain::EventTopic<core::model::SystemEvent> topic(4);
    core::producer::SystemEventPublisher publisher(topic);

    auto unflaggedArgument = makeDeviceEvent();
    unflaggedArgument.value.argument0 = 7;
    QVERIFY(publisher.publish(unflaggedArgument).rejection
            == core::producer::EventPublishRejection::InvalidSubjectValue);

    auto isolatedTrack = makeDeviceEvent();
    isolatedTrack.value.flags =
        core::model::eventSubjectBit(core::model::EventSubjectFlag::TrackIdValid);
    QVERIFY(publisher.publish(isolatedTrack).rejection
            == core::producer::EventPublishRejection::InvalidSubjectValue);

    auto missingRequest = makeControlEvent();
    missingRequest.value.requestId = 0;
    missingRequest.value.flags &=
        ~core::model::eventSubjectBit(core::model::EventSubjectFlag::RequestIdValid);
    QVERIFY(publisher.publish(missingRequest).rejection
            == core::producer::EventPublishRejection::MissingRequiredContext);

    QCOMPARE(topic.stats().size, qsizetype(0));
    QCOMPARE(publisher.rejectedCount(), quint64(3));
}

/**
 * @brief 创建无 GUI 依赖的 QtTest 入口并执行事件合同用例。
 */
QTEST_APPLESS_MAIN(SystemEventPublisherTest)

#include "tst_SystemEventPublisher.moc"
