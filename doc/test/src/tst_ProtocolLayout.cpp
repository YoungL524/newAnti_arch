/**
 * @file tst_ProtocolLayout.cpp
 * @brief 协议结构体布局的编译期 + 运行期验证
 * @details 所有 static_assert 在编译期锁定结构体尺寸和偏移，
 *          防止无意修改破坏与硬件的线缆协议契约。
 *
 *          修改结构体时必须同步更新本文件的期望值，
 *          "期望值"的来源是《ZL-反无雷达指控协议-260205.docx》。
 *
 * 运行方式：
 *   ctest --test-dir build --output-on-failure
 *   或直接 build/bin/tst_ProtocolLayout(.exe)
 */

#include <QtTest>
#include <cstddef>  // offsetof

#include "StructureSend.h"
#include "StructureReceive.h"
#include "GenericPackets.h"

// ============================================================
// 编译期契约：结构体总大小
//
// 当 static_assert 触发时，说明结构体布局发生了非预期的变化。
// 确认是协议版本升级导致的 → 同步更新期望值即可。
// 非预期变化 → 必须回退，否则 UDP 报文将与硬件错位。
// ============================================================

// workStandby: uint32 Work_Standby_Mode + 8×double = 4 + 64 = 68
// （注意有 8 个 double：Lon/Lat/Altitude/Course/Pitch/Roll/Antenna_azi1/Antenna_ele1）
static_assert(sizeof(workStandby) == 68,
    "sizeof(workStandby) != 68 — protocol spec §workStandby");

// TargetType: 1×uint32 + 3×int + 15×double + 1×int + 1×uint32 = 4+12+120+4+4 = 144
static_assert(sizeof(TargetType) == 144,
    "sizeof(TargetType) != 144 — protocol spec §TargetType");

// host_computer: TargetType(144) + uint32(4) + 4×double(32) + int(4) + bool(1) + int(4) = 189
static_assert(sizeof(host_computer) == 189,
    "sizeof(host_computer) != 189 — protocol spec §host_computer");

// Track_information: 4×uint32/int(16) + 2×double(16) + uint32(4) = 36
static_assert(sizeof(Track_information) == 36,
    "sizeof(Track_information) != 36 — protocol spec §Track_information");

// Control_Command_Status: 13×uint32(52) + bool(1) = 53
static_assert(sizeof(Control_Command_Status) == 53,
    "sizeof(Control_Command_Status) != 53 — protocol spec §Control_Command_Status");

// DataPackageHeader: 编译期守卫，防止意外修改结构体布局。
// 实际值 768 来自 MinGW 7.3.0 / Qt 5.14.2 实测。确认是协议升级 → 更新此值。
static_assert(sizeof(DataPackageHeader) == 768,
    "sizeof(DataPackageHeader) changed — review layout and update guard");

// GeneralPackage 应恰好等于子结构体之和（pack(1) 保证无 padding）
static_assert(sizeof(GeneralPackage) == sizeof(DataPackageHeader) + sizeof(workStandby) + sizeof(host_computer),
    "GeneralPackage 子结构体之间存在 padding — pack(1) 未生效");

// ============================================================
// 编译期契约：关键字段偏移
// ============================================================

// workStandby：第 1 个字段必须位于偏移 0
static_assert(offsetof(workStandby, Work_Standby_Mode) == 0,
    "workStandby.Work_Standby_Mode 偏移不为 0");

// workStandby：Lon_Coordinates 紧跟在 Work_Standby_Mode(4B) 之后
static_assert(offsetof(workStandby, Lon_Coordinates) == 4,
    "workStandby.Lon_Coordinates 偏移不为 4 — 对齐或布局异常");

// workStandby：Altitude 在第 3 个 double，偏移 = 4 + 2×8 = 20
static_assert(offsetof(workStandby, Altitude) == 20,
    "workStandby.Altitude 偏移不为 20");

// DataPackageHeader：前 3 个 uint32
static_assert(offsetof(DataPackageHeader, F_UI_Package_Head) == 0,
    "DPH.F_UI_Package_Head 偏移不为 0");
static_assert(offsetof(DataPackageHeader, F_UI_Time_Code) == 4,
    "DPH.F_UI_Time_Code 偏移不为 4");
static_assert(offsetof(DataPackageHeader, F_UI_Work_Mode) == 8,
    "DPH.F_UI_Work_Mode 偏移不为 8");

// GeneralPackage 组成验证
static_assert(offsetof(GeneralPackage, datapackageheader) == 0,
    "GeneralPackage.datapackageheader 偏移不为 0");
static_assert(offsetof(GeneralPackage, workstandby) == sizeof(DataPackageHeader),
    "GeneralPackage.workstandby 应紧跟在 DataPackageHeader 之后");
static_assert(offsetof(GeneralPackage, h_c) == sizeof(DataPackageHeader) + sizeof(workStandby),
    "GeneralPackage.h_c 应紧跟在 workStandby 之后");

// ============================================================
// 业务相关编译期约束
// ============================================================

// 迁移期 wire payload 上限仍需覆盖现有厂家协议包；DataPacket 本身不再内嵌该缓冲。
static_assert(MAX_ERROR_DESC_SIZE >= 64,
    "MAX_ERROR_DESC_SIZE < 64 — 无法容纳典型错误消息");

// 所有协议结构体必须为 trivially copyable（memcpy 前提）
static_assert(std::is_trivially_copyable<DataPackageHeader>::value, "");
static_assert(std::is_trivially_copyable<workStandby>::value, "");
static_assert(std::is_trivially_copyable<TargetType>::value, "");
static_assert(std::is_trivially_copyable<host_computer>::value, "");
static_assert(std::is_trivially_copyable<GeneralPackage>::value, "");
static_assert(std::is_trivially_copyable<Track_information>::value, "");
static_assert(std::is_trivially_copyable<Control_Command_Status>::value, "");

// ============================================================
// 运行时测试（辅助人工核对 + 边界验证）
// ============================================================

class tst_ProtocolLayout : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qDebug() << "=== 协议结构体布局 (pack(1)) ===";
        qDebug() << "sizeof(DataPackageHeader):"       << sizeof(DataPackageHeader);
        qDebug() << "sizeof(workStandby):"             << sizeof(workStandby);
        qDebug() << "sizeof(TargetType):"              << sizeof(TargetType);
        qDebug() << "sizeof(host_computer):"           << sizeof(host_computer);
        qDebug() << "sizeof(GeneralPackage):"          << sizeof(GeneralPackage);
        qDebug() << "sizeof(Track_information):"       << sizeof(Track_information);
        qDebug() << "sizeof(Control_Command_Status):"  << sizeof(Control_Command_Status);
    }

    void workStandby_field_offsets()
    {
        QCOMPARE(offsetof(workStandby, Work_Standby_Mode), 0u);
        QCOMPARE(offsetof(workStandby, Lon_Coordinates),   4u);
        QCOMPARE(offsetof(workStandby, Lat_Coordinates),  12u);
        QCOMPARE(offsetof(workStandby, Altitude),         20u);
        QCOMPARE(offsetof(workStandby, Course_Angle),     28u);
        QCOMPARE(offsetof(workStandby, Pitch_Angle),      36u);
        QCOMPARE(offsetof(workStandby, Roll_Angle),       44u);
        QCOMPARE(offsetof(workStandby, Antenna_azi1),     52u);
    }

    void DPH_critical_offsets()
    {
        // DPH 头部关键字段（上位机与雷达握手）
        QCOMPARE(offsetof(DataPackageHeader, F_UI_Package_Head), 0u);  // 握手标志
        QCOMPARE(offsetof(DataPackageHeader, F_UI_Work_Mode),    8u);  // 帧性质
        QCOMPARE(offsetof(DataPackageHeader, F_UI_Scene_Num),   12u);  // 景幅序号
    }

    void payload_capacity()
    {
        QVERIFY(MAX_ERROR_DESC_SIZE >= 64);
    }

    void dataPacket_command_and_payload_are_consistent()
    {
        workStandby input{};
        input.Work_Standby_Mode = 1;

        DataPacket packet;
        QVERIFY(packDataToPayload(packet, input));
        QCOMPARE(packet.command, DataCommand::WorkStandby);
        QVERIFY(std::holds_alternative<workStandby>(packet.payload));

        workStandby output{};
        QVERIFY(unpackDataFromPayload(packet, output));
        QCOMPARE(output.Work_Standby_Mode, 1u);

        packet.command = DataCommand::Target;
        QVERIFY(!unpackDataFromPayload(packet, output));
    }

    void dataPacket_error_is_a_typed_payload()
    {
        const DataPacket packet = createErrorPacket(
            ERR_LEVEL_ERROR, ERR_NETWORK, SOURCE_COMMUNICATION,
            QStringLiteral("network failed"), __FILE__, __LINE__);

        QCOMPARE(packet.command, DataCommand::Error);
        QVERIFY(isErrorPacket(packet));
        const auto error = std::get_if<ErrorPayload>(&packet.payload);
        QVERIFY(error != nullptr);
        QCOMPARE(error->type, ERR_NETWORK);
        QCOMPARE(error->description, QStringLiteral("network failed"));
    }

    void general_package_composition()
    {
        // GeneralPackage 的 3 个子结构体应连续排列，无间隔
        QCOMPARE(sizeof(GeneralPackage),
                 sizeof(DataPackageHeader) + sizeof(workStandby) + sizeof(host_computer));
    }
};

/**
 * @brief 验证 QTEST_MAIN 场景的验收条件并防止对应功能回归。
 * @details 测试通过明确的输入、可观察结果和边界断言锁定既定行为。
 */
QTEST_MAIN(tst_ProtocolLayout)
#include "tst_ProtocolLayout.moc"
