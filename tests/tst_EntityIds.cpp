/**
 * @file tst_EntityIds.cpp
 * @brief 验证实时链路数值句柄的类型隔离、零值语义和零存储开销。
 * @details 本测试防止后续为了兼容字符串 ID 而扩大热结构、把不同实体 ID 隐式混用，或让默认零值意外引用
 * 有效实体。测试只验证值类型合同，不替代 Endpoint/Device/Task/Acquisition 注册表的存在性检查。
 */

#include "core/model/EntityIds.h"

#include <QHash>
#include <QtTest>

#include <limits>
#include <type_traits>

/** @brief 汇总紧凑实体句柄的编译期布局和运行时值语义验收。 */
class EntityIdsTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收默认零值不引用实体，TaskId 零值可表达“无活动任务”。 */
    void zeroMeansUnbound();

    /** @brief 验收四类句柄尺寸等于底层整数，且保持标准布局与平凡复制。 */
    void layoutHasNoWrapperOverhead();

    /** @brief 验收不同实体类型不能隐式构造或互换，同类值可比较和哈希。 */
    void entityKindsRemainTypeSafe();

    /** @brief 验收完整底层无符号范围可原样保存，不保留额外魔法数值。 */
    void preservesUnderlyingRange();
};

/**
 * @brief 确认统一零值不会被误判为已注册实体。
 * @details TaskId 的默认值在 TrackPacket 中表示接收瞬间没有活动任务；其余 ID 的默认值表示发布门禁必须
 * 拒绝的未绑定状态，二者都不能隐式转换为非零整数。
 */
void EntityIdsTest::zeroMeansUnbound()
{
    const core::model::EndpointId endpoint;
    const core::model::DeviceId device;
    const core::model::TaskId noTask;
    const core::model::AcquisitionId acquisition;

    QVERIFY(!endpoint.isSet());
    QVERIFY(!device.isSet());
    QVERIFY(!noTask.isSet());
    QVERIFY(!acquisition.isSet());
    QCOMPARE(noTask.value(), quint32(0));
}

/**
 * @brief 固化句柄对 TrackPacket 与 TrackKey 的确定性内存预算。
 * @details 标准布局和平凡复制保证句柄适合内嵌 RingBuffer 与固定 ScratchBuffer；这里只约束进程内布局，
 * 不授权直接 memcpy 到数据库、网络或录制文件。
 */
void EntityIdsTest::layoutHasNoWrapperOverhead()
{
    static_assert(std::is_standard_layout<core::model::EndpointId>::value, "EndpointId must be standard-layout");
    static_assert(std::is_trivially_copyable<core::model::EndpointId>::value, "EndpointId must be trivially copyable");
    static_assert(std::is_trivially_copyable<core::model::AcquisitionId>::value, "AcquisitionId must be trivially copyable");

    QCOMPARE(sizeof(core::model::EndpointId), sizeof(quint32));
    QCOMPARE(sizeof(core::model::DeviceId), sizeof(quint32));
    QCOMPARE(sizeof(core::model::TaskId), sizeof(quint32));
    QCOMPARE(sizeof(core::model::AcquisitionId), sizeof(quint64));
}

/**
 * @brief 防止端点、设备和任务编号因底层位宽相同而在接口间静默串线。
 * @details 编译期断言拒绝原始整数与异类 ID 的隐式转换；运行时 QHash 验证同类相等值定位同一键。
 */
void EntityIdsTest::entityKindsRemainTypeSafe()
{
    static_assert(!std::is_convertible<quint32, core::model::DeviceId>::value, "raw integers must not convert implicitly");
    static_assert(!std::is_convertible<core::model::EndpointId, core::model::DeviceId>::value, "entity IDs must not mix");
    static_assert(!std::is_convertible<core::model::DeviceId, quint32>::value, "IDs must not decay to integers");

    const auto first = core::model::DeviceId::fromRaw(17);
    const auto same = core::model::DeviceId::fromRaw(17);
    const auto other = core::model::DeviceId::fromRaw(18);

    QVERIFY(first.isSet());
    QVERIFY(first == same);
    QVERIFY(first != other);
    QVERIFY(first < other);

    QHash<core::model::DeviceId, int> devices;
    devices.insert(first, 42);
    QCOMPARE(devices.value(same), 42);
}

/**
 * @brief 确认句柄不偷用最大值等额外哨兵，避免缩减注册表可用编号空间。
 * @details 唯一保留值是零；各注册表可按自身容量策略使用底层类型的任意非零编号。
 */
void EntityIdsTest::preservesUnderlyingRange()
{
    const auto endpoint = core::model::EndpointId::fromRaw(std::numeric_limits<quint32>::max());
    const auto acquisition = core::model::AcquisitionId::fromRaw(std::numeric_limits<quint64>::max());

    QCOMPARE(endpoint.value(), std::numeric_limits<quint32>::max());
    QCOMPARE(acquisition.value(), std::numeric_limits<quint64>::max());
}

/**
 * @brief 由 QtTest 启动紧凑实体句柄合同测试，不创建 GUI 应用对象。
 * @details 测试只依赖 QCoreApplication；入口按声明顺序执行 EntityIdsTest 的私有槽。
 */
QTEST_APPLESS_MAIN(EntityIdsTest)

#include "tst_EntityIds.moc"
