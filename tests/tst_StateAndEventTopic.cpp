/**
 * @file tst_StateAndEventTopic.cpp
 * @brief 验收 LatestState 的 KeepLast(1) 与 EventTopic 的有界事件日志语义。
 * @details 测试只定义局部 TestState/TestEvent，不提前创建 DeviceState 或 SystemEvent 空壳。所有 revision、
 * sequence 和 gap 判断都由数据域内部水位驱动，不依赖墙上时钟或外部设备。
 */

#include "core/datadomain/EventTopic.h"
#include "core/datadomain/LatestState.h"

#include <QSignalSpy>
#include <QtTest>

#include <type_traits>
#include <utility>

namespace {

/** @brief 用于验证完整状态替换的测试载荷，不代表生产 DeviceState 合同。 */
struct TestState
{
    int mode = 0; ///< 仅用于区分两份完整状态的测试值，无设备协议含义。
};

/** @brief 用于验证不可合并事件顺序的测试载荷，不代表生产 SystemEvent 合同。 */
struct TestEvent
{
    int code = 0; ///< 仅用于断言事件先后顺序的测试值，无告警等级含义。
};

/**
 * @brief 构造指定业务值和来源序号的局部测试 Sample。
 * @tparam T TestState 或 TestEvent。
 * @param value 写入局部测试模型整数成员的值。
 * @return sourceSequence 与业务值一致的 Sample。
 */
template<typename T>
core::model::Sample<T> makeSample(int value);

/**
 * @brief 为完整状态构造测试 Sample，避免测试依赖尚未冻结的生产字段。
 */
template<>
core::model::Sample<TestState> makeSample<TestState>(int value)
{
    core::model::Sample<TestState> sample;
    sample.meta.sourceSequence = static_cast<quint64>(value);
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence);
    sample.value.mode = value;
    return sample;
}

/**
 * @brief 为不可合并事件构造测试 Sample，保留来源序号与 Topic sequence 的独立字段。
 */
template<>
core::model::Sample<TestEvent> makeSample<TestEvent>(int value)
{
    core::model::Sample<TestEvent> sample;
    sample.meta.sourceSequence = static_cast<quint64>(value);
    sample.meta.validity = core::model::sampleFieldBit(core::model::SampleFieldFlag::HasSourceSequence);
    sample.value.code = value;
    return sample;
}

/** @brief 在编译期探测某个公开类型是否暴露 latest()。 */
template<typename T, typename = void>
struct HasLatest : std::false_type
{
};

/** @brief 当表达式 T::latest() 有效时标记为 true，用于守住 Event 与 State 的接口边界。 */
template<typename T>
struct HasLatest<T, std::void_t<decltype(std::declval<const T&>().latest())>> : std::true_type
{
};

static_assert(!HasLatest<core::domain::EventTopic<TestEvent>>::value,
              "EventTopic 不得暴露会误导状态语义的 latest()");
static_assert(HasLatest<core::domain::LatestState<TestState>>::value,
              "LatestState 必须暴露完整状态 latest()");

/** @brief 汇总 LatestState 与 EventTopic 第一阶段必须阻止的语义回归。 */
class StateAndEventTopicTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /** @brief 验证未设置状态时 revision 为零且 latest 为空。 */
    void reportsUnsetLatestState();

    /** @brief 验证连续 set 只保留最后完整状态、revision 单调且旧句柄仍安全。 */
    void replacesLatestStateAtomically();

    /** @brief 验证 LatestState Doorbell 最终提示最新 revision 而不传状态本体。 */
    void ringsStateRevisionDoorbell();

    /** @brief 验证 EventTopic 按序读取并准确报告容量覆盖 gap。 */
    void readsBoundedEventLogWithGap();
};

/**
 * @brief 在任何 owner 写入前检查 LatestState 的哨兵 revision 和空句柄合同。
 */
void StateAndEventTopicTest::reportsUnsetLatestState()
{
    core::domain::LatestState<TestState> state;
    QCOMPARE(state.revision(), quint64(0));
    QVERIFY(!state.latest().has_value());
}

/**
 * @brief 保存第一版状态句柄后执行第二次完整替换，确认最新值一致且旧对象生命周期由 Reader 句柄延长。
 */
void StateAndEventTopicTest::replacesLatestStateAtomically()
{
    core::domain::LatestState<TestState> state;
    QCOMPARE(state.set(makeSample<TestState>(1)), quint64(1));

    const auto first = state.latest();
    QVERIFY(first.has_value());
    QCOMPARE(first->revision, quint64(1));
    QCOMPARE(first->sample->value.mode, 1);

    QCOMPARE(state.set(makeSample<TestState>(2)), quint64(2));
    const auto second = state.latest();
    QVERIFY(second.has_value());
    QCOMPARE(second->revision, quint64(2));
    QCOMPARE(second->sample->value.mode, 2);
    QCOMPARE(state.revision(), quint64(2));

    QCOMPARE(first->sample->value.mode, 1);
    QVERIFY(first->sample.get() != second->sample.get());
}

/**
 * @brief 在处理事件循环前连续替换状态，允许合并通知但最终水位必须等于最新 revision。
 */
void StateAndEventTopicTest::ringsStateRevisionDoorbell()
{
    core::domain::LatestState<TestState> state;
    QSignalSpy spy(state.doorbell(), &core::domain::TopicDoorbell::advanced);
    QVERIFY(spy.isValid());

    state.set(makeSample<TestState>(1));
    state.set(makeSample<TestState>(2));
    state.set(makeSample<TestState>(3));

    QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 1000);
    QCOMPARE(spy.back().at(0).toULongLong(), quint64(3));
}

/**
 * @brief 在容量二的日志中发布四个事件，要求从旧 cursor 读取时报告 [1,2] 缺口并返回事件三、四。
 */
void StateAndEventTopicTest::readsBoundedEventLogWithGap()
{
    core::domain::EventTopic<TestEvent> events(2);
    core::domain::TopicScratchBuffer<TestEvent> scratch(10);
    for (int code = 1; code <= 4; ++code) {
        QCOMPARE(events.publish(makeSample<TestEvent>(code)), static_cast<quint64>(code));
    }

    const auto result = events.readAfter(0, scratch);
    QVERIFY(result.relation == core::domain::CursorRelation::Gap);
    QVERIFY(result.gap.has_value());
    QCOMPARE(result.gap->firstLost, quint64(1));
    QCOMPARE(result.gap->lastLost, quint64(2));
    QCOMPARE(result.count, qsizetype(2));
    QCOMPARE(scratch[0].sample.value.code, 3);
    QCOMPARE(scratch[1].sample.value.code, 4);
    QCOMPARE(events.stats().overwrittenRecords, quint64(2));
}

} // namespace

/**
 * @brief 创建无 GUI 的 QtTest 入口，为 LatestState/EventTopic Doorbell 提供主线程事件循环。
 */
QTEST_GUILESS_MAIN(StateAndEventTopicTest)

#include "tst_StateAndEventTopic.moc"
