#include "core/protocol/DcmpProtocolTypes.h"
#include "core/protocol/DcmpPayloads.h"
#include <QtTest>
#include <cstddef>
#include <limits>
#include "doc/protocol/toDP.h"

/** @brief 验收 Excel 公共封装的字段偏移、长度口径和命令字，防止旧版布局重新混入。 */
class DcmpProtocolTypesTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 对照工作表逐字段锁定 44 字节公共头及 4 字节包尾。 */
    void layout();
    /** @brief 验证声明长度排除两端标志，且命令字忠实保留工作表定义。 */
    void constantsAndLength();
    /** @brief 验收指定四种载荷的真实字段和四面控制隔离。 */
    void payloadLayouts();
};

/**
 * @brief 验证所有字段位置，避免总尺寸相同但字段顺序已变的隐蔽回归。
 */
void DcmpProtocolTypesTest::layout()
{
    using namespace core::protocol;
    QCOMPARE(sizeof(DcmpFrameHeader), std::size_t(44));
    QCOMPARE(alignof(DcmpFrameHeader), std::size_t(4));
    QCOMPARE(sizeof(DcmpMessageType), std::size_t(4));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_Package_Head), std::size_t(0));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_TotalLength), std::size_t(4));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_MessageType), std::size_t(8));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_Protocol_Version), std::size_t(12));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_GPS_Time_DMY), std::size_t(16));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_GPS_Time_SMH), std::size_t(20));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_SYS_Time_DMY), std::size_t(24));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_SYS_Time_SMH), std::size_t(28));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_DCMP_Version), std::size_t(32));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_DCMP_Time_DMY), std::size_t(36));
    QCOMPARE(offsetof(DcmpFrameHeader, F_UI_DCMP_Time_SMH), std::size_t(40));
    QCOMPARE(sizeof(DcmpFrameTail), std::size_t(4));
    QCOMPARE(offsetof(DcmpFrameTail, packetTail), std::size_t(0));
}

/**
 * @brief 用心跳的 84 字节载荷长度验收公式。
 * @details 实际整包 132 字节，长度字段为 120；差值排除了长度字段自身及两端标志。
 */
void DcmpProtocolTypesTest::constantsAndLength()
{
    using namespace core::protocol;
    DcmpFrameHeader header;
    const DcmpFrameTail tail;
    QCOMPARE(header.F_UI_Package_Head, quint32(0xA1A11A1A));
    QCOMPARE(header.F_UI_Protocol_Version, quint32(20260908));
    QCOMPARE(tail.packetTail, quint32(0xB1B2A1A2));
    header.F_UI_TotalLength = DcmpFrameHeader::declaredFixedSize + 84U;
    QCOMPARE(header.F_UI_TotalLength, quint32(120));
    QCOMPARE(DcmpFrameHeader::wireSize + 84U + DcmpFrameTail::wireSize, quint32(132));
    QCOMPARE(quint32(DcmpMessageType::DcmpToDpControl), quint32(0x0001));
    QCOMPARE(quint32(DcmpMessageType::DpToFpControl), quint32(0x0102));
    QCOMPARE(quint32(DcmpMessageType::FpgaToSpData), quint32(0x0203));
    QCOMPARE(quint32(DcmpMessageType::SpToDpPoint), quint32(0x0301));
    QCOMPARE(quint32(DcmpMessageType::DpToDcmpTrack), quint32(0x0100));
    QCOMPARE(quint32(DcmpMessageType::DpHeartbeat), quint32(0x0101));
    QCOMPARE(quint32(DcmpMessageType::SpHeartbeat), quint32(0x0303));
}

 
/**
 * @brief 对照工作表锁定每个信息区字段偏移，防止带符号字段或备份数组挤占后续区域。
 */
void DcmpProtocolTypesTest::payloadLayouts()
{
    using namespace core::protocol;
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_Combi_Mode), std::size_t(0));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_FrameWork_Mode), std::size_t(4));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_SysWork_Mode), std::size_t(8));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_SysRange_Mode), std::size_t(12));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_STC_Switch), std::size_t(16));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_STC_Mode), std::size_t(20));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_Exc_Switch), std::size_t(24));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_CFAR_Mode_Ctrl), std::size_t(28));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_STBY_Ctrl), std::size_t(32));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_CFAR_Ctrl_1), std::size_t(36));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_CFAR_Ctrl_2), std::size_t(40));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_Vel_Ctrl), std::size_t(44));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_AD_Rec_Switch), std::size_t(48));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_Dot_Rec_Switch), std::size_t(52));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_TracDot_Ctrl), std::size_t(56));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_DotMerge_Rec_Switch), std::size_t(60));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, F_UI_DotCorr_Rec_Switch), std::size_t(64));
    QCOMPARE(offsetof(DcmpToDpControlPayload::CommonControl, uni_bak2), std::size_t(68));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UI_ScreenWave_Mod_Ctrl), std::size_t(0));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UI_ScreenTrack_Enable), std::size_t(4));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Azim_1), std::size_t(8));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Elev_1), std::size_t(12));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Azim_2), std::size_t(16));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Elev_2), std::size_t(20));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Azim_3), std::size_t(24));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Elev_3), std::size_t(28));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Azim_4), std::size_t(32));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_DP_ScreenTrack_Elev_4), std::size_t(36));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_Beam_Azim_Ctrl), std::size_t(40));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_Beam_Elev_Ctrl), std::size_t(44));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_Band_Switch), std::size_t(48));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_BIT_Switch_Ctrl), std::size_t(52));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_FrameWave_Ctrl), std::size_t(56));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_Modul_Ctrl), std::size_t(60));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_ModulBw_Ctrl), std::size_t(64));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_Pw_Ctrl), std::size_t(68));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_Tr_Ctrl), std::size_t(72));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_FrameWait_Ctrl), std::size_t(76));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_EffTrN_Ctrl), std::size_t(80));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_SampR0_Ctrl), std::size_t(84));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_SampRn_Ctrl), std::size_t(88));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_FrameWaveWrite_Ctrl), std::size_t(92));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, F_UIDP_WritePos_Ctrl), std::size_t(96));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FrameControl, uni_bak3), std::size_t(100));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Height), std::size_t(0));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_GPS_Long), std::size_t(4));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_GPS_Lati), std::size_t(8));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_HeadingAngl), std::size_t(12));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_ElAngl), std::size_t(16));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_RolAngl), std::size_t(20));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_heading_AnglVel), std::size_t(24));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_El_AnglVel), std::size_t(28));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Rol_AnglVel), std::size_t(32));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Vel_East), std::size_t(36));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Vel_North), std::size_t(40));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Vel_Up), std::size_t(44));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_AntRef_X_Vel), std::size_t(48));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_AntRef_Y_Vel), std::size_t(52));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_AntRef_Z_Vel), std::size_t(56));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Vel_Ground), std::size_t(60));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_Vel_Acc), std::size_t(64));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, F_FL_Platform_YawAngl), std::size_t(68));
    QCOMPARE(offsetof(DcmpToDpControlPayload::Navigation, uni_bak4), std::size_t(72));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_Ant_Idx), std::size_t(0));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_FreBand_Pick), std::size_t(4));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_Freq_Ctrl), std::size_t(8));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_Recv_Gain_Ctrl), std::size_t(12));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_Antenna_Mode), std::size_t(16));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_Power_Mode), std::size_t(20));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_RF_Reset), std::size_t(24));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_DP_C1), std::size_t(28));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_DP_C2), std::size_t(32));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_DP_C3), std::size_t(36));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_DP_C4), std::size_t(40));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_DP_C5), std::size_t(44));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_DP_C6), std::size_t(48));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_AntCourse_Angle), std::size_t(52));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_AntPitch_Angle), std::size_t(56));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_AntRoll_Angle), std::size_t(60));
    QCOMPARE(offsetof(DcmpToDpControlPayload::FaceControl, F_UI_Antenna_bak), std::size_t(64));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Packet_Length), std::size_t(0));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Frame_Time), std::size_t(4));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Packet_Count), std::size_t(8));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP__Antenna_Idx), std::size_t(12));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Primary_Confirm), std::size_t(16));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Executed_State), std::size_t(20));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_InvalidComm_Count), std::size_t(24));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, F_FP_FrameWave), std::size_t(28));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_FrameWork_Attrib), std::size_t(32));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_IFBw), std::size_t(36));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_BITSwitch), std::size_t(40));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Pw), std::size_t(44));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Tr), std::size_t(48));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_SampR0), std::size_t(52));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_SampRn), std::size_t(56));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_EffTrN), std::size_t(60));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Azim), std::size_t(64));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Elev), std::size_t(68));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_FPGA_Version), std::size_t(72));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_FPGA_Time_DMY), std::size_t(76));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_FPGA_Time_SMH), std::size_t(80));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_AN_Antenna_Mode), std::size_t(84));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FS_Freq), std::size_t(88));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_Recv_Gain), std::size_t(92));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_ErrCode), std::size_t(96));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_AN_ErrCode), std::size_t(100));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_FPGA_Temp), std::size_t(104));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_AN_Array_Temp), std::size_t(108));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FS_LD1), std::size_t(112));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FS_LD2), std::size_t(116));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FS_SF), std::size_t(120));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::FpStatus, B_FP_bak), std::size_t(124));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::SpStatus, B_SP_Version), std::size_t(0));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::SpStatus, B_SP_Time_DMY), std::size_t(4));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::SpStatus, B_SP_Time_SMH), std::size_t(8));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::SpStatus, uni_bak6), std::size_t(12));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpStatus, B_DP_Version), std::size_t(0));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpStatus, B_DP_Time_DMY), std::size_t(4));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpStatus, B_DP_Time_SMH), std::size_t(8));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpStatus, B_DP_bak), std::size_t(12));
    QCOMPARE(sizeof(DcmpToDpControlPayload), std::size_t(896));
    QCOMPARE(offsetof(DcmpToDpControlPayload, frameControl), std::size_t(128));
    QCOMPARE(offsetof(DcmpToDpControlPayload, navigation), std::size_t(256));
    QCOMPARE(offsetof(DcmpToDpControlPayload, faces), std::size_t(384));
    QCOMPARE(sizeof(DcmpToDpControlPayload::FaceControl), std::size_t(128));
    DcmpToDpControlPayload control;
    for (quint32 i = 0; i < 4; ++i) {
        QCOMPARE(control.faces[i].F_UI_Ant_Idx, i);
        control.faces[i].F_UI_AntCourse_Angle = -18000 + qint32(i) * 9000;
    }
    QCOMPARE(control.faces[0].F_UI_AntCourse_Angle, qint32(-18000));
    QCOMPARE(control.faces[3].F_UI_AntCourse_Angle, qint32(9000));
    QCOMPARE(sizeof(DpToDcmpTrackPayload), std::size_t(608));
    QCOMPARE(offsetof(DpToDcmpTrackPayload, dpTrack), std::size_t(384));
    QCOMPARE(sizeof(DpToDcmpTrackPayload::DpTrack), std::size_t(224));
    QVERIFY(std::numeric_limits<float>::is_iec559);
    QCOMPARE(sizeof(float), std::size_t(4));
    QCOMPARE(DcmpFrameHeader::declaredFixedSize + DpToDcmpTrackPayload::wireSize, quint32(644));
    QCOMPARE(DcmpFrameHeader::wireSize + DpToDcmpTrackPayload::wireSize + DcmpFrameTail::wireSize, quint32(656));
    QCOMPARE(sizeof(dcmp::DpToDcmpTrackPayload), sizeof(DpToDcmpTrackPayload));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload, dpTrack), std::size_t(384));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, targetId), std::size_t(0));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, targetId), std::size_t(0));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, waveRow), std::size_t(4));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, waveRow), std::size_t(4));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, waveCol), std::size_t(8));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, waveCol), std::size_t(8));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, wave), std::size_t(12));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, wave), std::size_t(12));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, rawWavePosition), std::size_t(16));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, rawWavePosition), std::size_t(16));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, range), std::size_t(20));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, range), std::size_t(20));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, azimuth), std::size_t(24));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, azimuth), std::size_t(24));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, elevation), std::size_t(28));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, elevation), std::size_t(28));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, radialSpeed), std::size_t(32));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, radialSpeed), std::size_t(32));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, amplitude), std::size_t(36));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, amplitude), std::size_t(36));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, altitude), std::size_t(40));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, altitude), std::size_t(40));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, courseAngle), std::size_t(44));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, courseAngle), std::size_t(44));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, pitchAngle), std::size_t(48));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, pitchAngle), std::size_t(48));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, targetSNR), std::size_t(52));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, targetSNR), std::size_t(52));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, x), std::size_t(56));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, x), std::size_t(56));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, y), std::size_t(60));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, y), std::size_t(60));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, z), std::size_t(64));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, z), std::size_t(64));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, vx), std::size_t(68));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, vx), std::size_t(68));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, vy), std::size_t(72));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, vy), std::size_t(72));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, vz), std::size_t(76));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, vz), std::size_t(76));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, clusterId), std::size_t(80));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, clusterId), std::size_t(80));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, count), std::size_t(84));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, count), std::size_t(84));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, clutterDiagramMarkers), std::size_t(88));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, clutterDiagramMarkers), std::size_t(88));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, waveformMark), std::size_t(92));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, waveformMark), std::size_t(92));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, a), std::size_t(96));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, a), std::size_t(96));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, b), std::size_t(100));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, b), std::size_t(100));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, c), std::size_t(104));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, c), std::size_t(104));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, tierLevel), std::size_t(108));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, tierLevel), std::size_t(108));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, targetView), std::size_t(112));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, targetView), std::size_t(112));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, packFlag), std::size_t(116));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, packFlag), std::size_t(116));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, savedAzimuthCenter), std::size_t(120));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, savedAzimuthCenter), std::size_t(120));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, savedElevationCenter), std::size_t(124));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, savedElevationCenter), std::size_t(124));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, kinematicsMargin), std::size_t(128));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, kinematicsMargin), std::size_t(128));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, kinematicsDiff), std::size_t(132));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, kinematicsDiff), std::size_t(132));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, dataSource), std::size_t(136));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, dataSource), std::size_t(136));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, assocState), std::size_t(140));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, assocState), std::size_t(140));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, pointId), std::size_t(144));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, pointId), std::size_t(144));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, multiplyRange), std::size_t(148));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, multiplyRange), std::size_t(148));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, multiplyDoppler), std::size_t(152));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, multiplyDoppler), std::size_t(152));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, accumulatedTime), std::size_t(156));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, accumulatedTime), std::size_t(156));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, trackRecognitionMark), std::size_t(160));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, trackRecognitionMark), std::size_t(160));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, comingAndGoingSigns), std::size_t(164));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, comingAndGoingSigns), std::size_t(164));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, pointPosition), std::size_t(168));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, pointPosition), std::size_t(168));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, pointType), std::size_t(172));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, pointType), std::size_t(172));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, extrapolationCount), std::size_t(176));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, extrapolationCount), std::size_t(176));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, updateFlag), std::size_t(180));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, updateFlag), std::size_t(180));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, hostView), std::size_t(184));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, hostView), std::size_t(184));
    QCOMPARE(offsetof(DpToDcmpTrackPayload::DpTrack, reserved), std::size_t(188));
    QCOMPARE(offsetof(dcmp::DpToDcmpTrackPayload::DpTrack, reserved), std::size_t(188));
    QCOMPARE(offsetof(DpToDcmpTrackPayload, spStatus), std::size_t(128));
    QCOMPARE(offsetof(DpToDcmpTrackPayload, dpStatus), std::size_t(256));
    QCOMPARE(sizeof(DpHeartbeatPayload), std::size_t(84));
    QCOMPARE(sizeof(SpHeartbeatPayload), std::size_t(84));
    QCOMPARE(offsetof(DpHeartbeatPayload, fpOnlineMask), std::size_t(2));
    QCOMPARE(offsetof(DpHeartbeatPayload, bak), std::size_t(4));
    QCOMPARE(offsetof(SpHeartbeatPayload, fpDataMask), std::size_t(2));
    QCOMPARE(offsetof(SpHeartbeatPayload, bak), std::size_t(4));
    const DpHeartbeatPayload dp;
    const SpHeartbeatPayload sp;
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(dp.bak[i], quint32(0));
        QCOMPARE(sp.bak[i], quint32(0));
    }
    QCOMPARE(quint32(DcmpToDpControlPayload::messageType), quint32(0x0001));
    QCOMPARE(quint32(DpToDcmpTrackPayload::messageType), quint32(0x0100));
    QCOMPARE(quint32(DpHeartbeatPayload::messageType), quint32(0x0101));
    QCOMPARE(quint32(SpHeartbeatPayload::messageType), quint32(0x0303));
}


/**
 * @brief 运行公共封装合同测试，不启动通信端点或数据域。
 */
QTEST_APPLESS_MAIN(DcmpProtocolTypesTest)
#include "tst_DcmpProtocolTypes.moc"
