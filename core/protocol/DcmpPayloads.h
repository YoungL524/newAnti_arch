/**
 * @file DcmpPayloads.h
 * @brief 定义 DCMP 控制、DP 状态航迹及 DP/SP 心跳的线协议载荷。
 * @details 上游为控制 owner 提供的字段或设备报文字节，下游为边界 Adapter；从四个顶层载荷阅读。
 * 子结构对应工作表真实信息区，仅在调用方拥有的短期报文对象中嵌套，不进入 Topic，不产生新数据链。
 * 本文件不实现编解码、设备状态机或航迹映射；所有数值保持线协议原始单位，自然对齐不能替代逐字段编码。
 * 默认零值仅用于确定性初始化，不保证整份控制指令可执行；发送前仍须按设备模式校验。
 */
#pragma once
#include "DcmpProtocolTypes.h"

namespace core::protocol {

/** @brief 0x0001：DCMP 发给 DP 的 896 字节控制载荷，含四面独立控制区。 */
struct DcmpToDpControlPayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::DcmpToDpControl; ///< 对应线消息类型。
    static constexpr quint32 wireSize = 896U; ///< 仅载荷字节数，不含公共头尾。
    /** @brief 通用阵面及 SP/DP 控制信息 */
    struct CommonControl
    {
        quint32 F_UI_Combi_Mode = {}; ///< 区内偏移 0：阵面组合应用模式；00：常规使用模式 01：合成使用模式 （保留这个接口，暂时可以不做任何响应）。
        quint32 F_UI_FrameWork_Mode = {}; ///< 区内偏移 4：帧工作模式；00：景幅连续模式 01：单帧连续模式（为DP自主调用的控制值，显控上不可选） 02：单帧测试模式。
        quint32 F_UI_SysWork_Mode = {}; ///< 区内偏移 8：系统工作主模式；00：Air 01：Groud 02：Sea 03：Weather 04：MultiFunc 保留这个接口，可暂时不用。
        quint32 F_UI_SysRange_Mode = {}; ///< 区内偏移 12：探测量程控制；00：2km 01：5km ……。
        quint32 F_UI_STC_Switch = {}; ///< 区内偏移 16：STC电路开关（开电置0）；00：关（默认关） 01：开。
        quint32 F_UI_STC_Mode = {}; ///< 区内偏移 20：STC方案；在STC开关打开时起效 00：方案一 01：方案二 10：方案三 11：方案四。
        quint32 F_UI_Exc_Switch = {}; ///< 区内偏移 24：激励信号关闭开关；00：激励信号开（默认开） 01：激励信号关。
        qint32 F_UI_CFAR_Mode_Ctrl = {}; ///< 区内偏移 28：CFAR方案控制；00：方案一 01：方案二。
        quint32 F_UI_STBY_Ctrl = {}; ///< 区内偏移 32：采集工作控制；00：停止采集，01：开始或保持采集。
        qint32 F_UI_CFAR_Ctrl_1 = {}; ///< 区内偏移 36：CFAR门限控制1。
        qint32 F_UI_CFAR_Ctrl_2 = {}; ///< 区内偏移 40：CFAR门限控制2。
        qint32 F_UI_Vel_Ctrl = {}; ///< 区内偏移 44：速度门限控制。
        quint32 F_UI_AD_Rec_Switch = {}; ///< 区内偏移 48：AD数据记录开关；00：记录关（默认关） 01：记录开。
        quint32 F_UI_Dot_Rec_Switch = {}; ///< 区内偏移 52：点迹数据记录开关；00：记录关（默认关） 01：记录开。
        quint32 F_UI_TracDot_Ctrl = {}; ///< 区内偏移 56：起批点数。
        quint32 F_UI_DotMerge_Rec_Switch = {}; ///< 区内偏移 60：凝聚数据记录开关；00：记录关（默认关） 01：记录开。
        quint32 F_UI_DotCorr_Rec_Switch = {}; ///< 区内偏移 64：关联数据记录开关；00：记录关（默认关） 01：记录开。
        quint32 uni_bak2[15] = {}; ///< 区内偏移 68：备份字；初始化为零。
    };

    /** @brief 连续模式及单帧模式的控制信息 */
    struct FrameControl
    {
        quint32 F_UI_ScreenWave_Mod_Ctrl = {}; ///< 区内偏移 0：景幅波形控制方案；00：波形方案一； 01：波形方案二； 02：波形方案三； ……。
        quint32 F_UI_ScreenTrack_Enable = {}; ///< 区内偏移 4：景幅跟踪使能；00：关（默认关） 01：开。
        quint32 F_DP_ScreenTrack_Azim_1 = {}; ///< 区内偏移 8：第1个跟踪点方位角；对最多四个目标实现景幅连续模式下的跟踪（此跟踪位置也为DP自主控制，不用在显控界面上设计人机接口）。
        quint32 F_DP_ScreenTrack_Elev_1 = {}; ///< 区内偏移 12：第1个跟踪点俯仰角。
        quint32 F_DP_ScreenTrack_Azim_2 = {}; ///< 区内偏移 16：第1个跟踪点方位角。
        quint32 F_DP_ScreenTrack_Elev_2 = {}; ///< 区内偏移 20：第1个跟踪点俯仰角。
        quint32 F_DP_ScreenTrack_Azim_3 = {}; ///< 区内偏移 24：第1个跟踪点方位角。
        quint32 F_DP_ScreenTrack_Elev_3 = {}; ///< 区内偏移 28：第1个跟踪点俯仰角。
        quint32 F_DP_ScreenTrack_Azim_4 = {}; ///< 区内偏移 32：第1个跟踪点方位角。
        quint32 F_DP_ScreenTrack_Elev_4 = {}; ///< 区内偏移 36：第1个跟踪点俯仰角。
        qint32 F_UIDP_Beam_Azim_Ctrl = {}; ///< 区内偏移 40：波束指向控制方位角；单帧模式（包含单独连续、单帧测试）下的波束指向控制，有符号数，补码表示，范围：[-45,45]度，比例尺：100。界面上人工输入值适用于单帧测试模式，在单帧连续模式下，这个值是DP直接控制的。
        qint32 F_UIDP_Beam_Elev_Ctrl = {}; ///< 区内偏移 44：波束指向控制俯仰角。
        quint32 F_UIDP_Band_Switch = {}; ///< 区内偏移 48：中频带宽切换控制字；00：带宽1 01：带宽2 02：带宽3 ……。
        quint32 F_UIDP_BIT_Switch_Ctrl = {}; ///< 区内偏移 52：BIT开关；00：关（默认关） 01：开。
        quint32 F_UIDP_FrameWave_Ctrl = {}; ///< 区内偏移 56：波形模式字选择；0x11：波形1 0x12：波形2 …… 0x21：波形m 0x22：波形n …… 0xaa：自定义波形（在自定义波形情况下，响应调制形式、调制带宽、脉宽等参数设置） 十六进制表示对应的各个具体波形，具体波形设定好后再分配。 另：显控上选择的波形模式字主要用于单帧模式的试验，正常工作时是DP根据情况自动选择的。
        quint32 F_UIDP_Modul_Ctrl = {}; ///< 区内偏移 60：调制形式；00：线性调频脉压 01：普通脉冲调制 ……。
        quint32 F_UIDP_ModulBw_Ctrl = {}; ///< 区内偏移 64：调频带宽；以MHz为单位，格式FPGA定义。
        quint32 F_UIDP_Pw_Ctrl = {}; ///< 区内偏移 68：脉宽；以us为单位，格式FPGA定义。
        quint32 F_UIDP_Tr_Ctrl = {}; ///< 区内偏移 72：重复周期；以us为单位，格式FPGA定义。
        quint32 F_UIDP_FrameWait_Ctrl = {}; ///< 区内偏移 76：帧间等待周期数；填零脉冲个数，格式FPGA定义。
        quint32 F_UIDP_EffTrN_Ctrl = {}; ///< 区内偏移 80：有效脉冲个数；采样对应的脉冲数。
        quint32 F_UIDP_SampR0_Ctrl = {}; ///< 区内偏移 84：Tr内采样值选取送出的起始位置；FPGA自定义（要考虑当采用开窗采样时，要能够适应此时数据包的长度变化）。
        quint32 F_UIDP_SampRn_Ctrl = {}; ///< 区内偏移 88：Tr内采样值选取送出的截止时刻 （或采样长度）。
        quint32 F_UIDP_FrameWaveWrite_Ctrl = {}; ///< 区内偏移 92：写入指令；00：不写入（默认） 01：写入。
        quint32 F_UIDP_WritePos_Ctrl = {}; ///< 区内偏移 96：写入位置；以模式字定义，可以写入的模式字范围为0x11~0x99。
        quint32 uni_bak3[7] = {}; ///< 区内偏移 100：备份字；初始化为零。
    };

    /** @brief 平台惯导原始信息 */
    struct Navigation
    {
        quint32 F_FL_Platform_GPS_Long = {}; ///< 区内偏移 0：载体平台经度，接收值除以1000000。
        quint32 F_FL_Platform_GPS_Lati = {}; ///< 区内偏移 4：载体平台纬度，接收值除以1000000。
        qint32 F_FL_Platform_Height = {}; ///< 区内偏移 8：载体平台高度，接收值除以100。
        qint32 F_FL_Platform_HeadingAngl = {}; ///< 区内偏移 12：载体平台航向角，接收值除以100。
        qint32 F_FL_Platform_ElAngl = {}; ///< 区内偏移 16：载体平台俯仰角，接收值除以100。
        qint32 F_FL_Platform_RolAngl = {}; ///< 区内偏移 20：载体平台横滚角，接收值除以100。
        qint32 F_FL_Platform_heading_AnglVel = {}; ///< 区内偏移 24：载体平台航向角速度，接收值除以100。
        qint32 F_FL_Platform_El_AnglVel = {}; ///< 区内偏移 28：载体平台俯仰角速度，接收值除以100。
        qint32 F_FL_Platform_Rol_AnglVel = {}; ///< 区内偏移 32：载体平台横滚角速度，接收值除以100。
        qint32 F_FL_Platform_Vel_East = {}; ///< 区内偏移 36：载体平台速度东向分量，接收值除以100。
        qint32 F_FL_Platform_Vel_North = {}; ///< 区内偏移 40：载体平台速度北向分量，接收值除以100。
        qint32 F_FL_Platform_Vel_Up = {}; ///< 区内偏移 44：载体平台速度天向分量，接收值除以100。
        qint32 F_FL_AntRef_X_Vel = {}; ///< 区内偏移 48：天线系速度X分量，接收值除以100。
        qint32 F_FL_AntRef_Y_Vel = {}; ///< 区内偏移 52：天线系速度Y分量，接收值除以100。
        qint32 F_FL_AntRef_Z_Vel = {}; ///< 区内偏移 56：天线系速度Z分量，接收值除以100。
        qint32 F_FL_Platform_Vel_Ground = {}; ///< 区内偏移 60：载体平台地速，接收值除以100。
        qint32 F_FL_Platform_Vel_Acc = {}; ///< 区内偏移 64：载体平台加速度，接收值除以100。
        qint32 F_FL_Platform_YawAngl = {}; ///< 区内偏移 68：载体平台偏航角，接收值除以100。
        quint32 uni_bak4[14] = {}; ///< 区内偏移 72：备份字；初始化为零。
    };

    /** @brief 单阵面控制信息；数组下标替代工作表字段名的 No1～No4 后缀 */
    struct FaceControl
    {
        quint32 F_UI_Ant_Idx = {}; ///< 区内偏移 0：天线控制索引号；00：1#阵面 01：2#阵面 10：3#阵面 11：4#阵面 在本包内，此处赋值为“00”，即1#阵面。
        quint32 F_UI_FreBand_Pick = {}; ///< 区内偏移 4：工作频段选择；00：X波段 01：Ku波段。
        quint32 F_UI_Freq_Ctrl = {}; ///< 区内偏移 8：频率控制字；频率控制字： 000000：频率0 000001：频率1 000010：频率2 …… 100111：频率39 有多少个频点，就使用多少号，每一个号码对应的频点值提供即可；另外，不同频段下如果频点数量不同，超出的号码不响应即可。
        quint32 F_UI_Recv_Gain_Ctrl = {}; ///< 区内偏移 12：接收增益控制；接收增益控制（在STC功能关闭时起作用） bit [9]:接收通道第二级衰减控制（IF）衰减16dB bit [8]:接收通道第二级衰减控制（IF）衰减08dB bit [7]:接收通道第二级衰减控制（IF）衰减04dB bit [6]:接收通道第二级衰减控制（IF）衰减02dB bit [5]:接收通道第二级衰减控制（IF）衰减01dB bit [4]:接收通道第一级衰减器控制（RF）衰减16dB bit [3]:接收通道第一级衰减器控制（RF）衰减08dB bit [2]:接收通道第一级衰减器控制（RF）衰减04dB bit [1]:接收通道第一级衰减器控制（RF）衰减02dB bit [0]:接收通道第一级衰减器控制（RF）衰减01dB。
        quint32 F_UI_Antenna_Mode = {}; ///< 区内偏移 16：天线模式（默认待机模式）；00：天线待机 01：只接收加电 10：只发射加电 11：收发均加电。
        quint32 F_UI_Power_Mode = {}; ///< 区内偏移 20：远程断电（默认均上电）；bit[2]=0: 风扇1电源打开 bit[2]=1: 风扇1电源关闭 bit[1]=0: 风扇2电源打开 bit[1]=1: 风扇2电源关闭 bit[0]=0: TR组件电源打开 bit[0]=1: TR组件电源关闭。
        quint32 F_UI_RF_Reset = {}; ///< 区内偏移 24：天线与射频系统复位；0x55：复位 0xaa：正常工作。
        qint32 F_DP_C1 = {}; ///< 区内偏移 28：C1；BIT反馈参数1。
        qint32 F_DP_C2 = {}; ///< 区内偏移 32：C2；BIT反馈参数2。
        qint32 F_DP_C3 = {}; ///< 区内偏移 36：C3；BIT反馈参数3。
        qint32 F_DP_C4 = {}; ///< 区内偏移 40：C4；BIT反馈参数4。
        qint32 F_DP_C5 = {}; ///< 区内偏移 44：C5；BIT反馈参数5。
        qint32 F_DP_C6 = {}; ///< 区内偏移 48：C6；BIT反馈参数6。
        qint32 F_UI_AntCourse_Angle = {}; ///< 区内偏移 52：该阵面安装航向角；有符号数，补码表示，范围：[-180,180]度，比例尺：100。。
        qint32 F_UI_AntPitch_Angle = {}; ///< 区内偏移 56：该阵面安装俯仰角；有符号数，补码表示，范围：[-90,90]度，比例尺：100。。
        qint32 F_UI_AntRoll_Angle = {}; ///< 区内偏移 60：该阵面安装横滚角；有符号数，补码表示，范围：[-90,90]度，比例尺：100。。
        quint32 F_UI_Antenna_bak[16] = {}; ///< 区内偏移 64：备份字；初始化为零。
    };

    CommonControl control {}; ///< 载荷偏移 0，128 字节。
    FrameControl frameControl {}; ///< 载荷偏移 128，128 字节。
    Navigation navigation {}; ///< 载荷偏移 256，128 字节。
    FaceControl faces[4] {{0U}, {1U}, {2U}, {3U}}; ///< 载荷偏移 384；每面 128 字节，索引为 0～3，非心跳位图。
};

/**
 * @brief 0x0100：DP 返回的 FPGA/SP/DP 状态及单个航迹点。
 * @details 状态前缀384字节，航迹点224字节；完整载荷608字节，公共包尾位于载荷之后。
 */
struct DpToDcmpTrackPayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::DpToDcmpTrack; ///< 对应线消息类型。
    static constexpr quint32 prefixWireSize = 384U; ///< 状态前缀字节数。
    static constexpr quint32 wireSize = 608U; ///< 完整载荷字节数，包含状态前缀和一个航迹点。
    static constexpr quint32 frameWireSize = DcmpFrameHeader::wireSize + wireSize + DcmpFrameTail::wireSize; ///< 公共头、载荷和包尾的完整线上字节数。
    static constexpr quint32 fpPacketCountOffset = 4U; ///< FPGA包计数相对载荷起点的线上偏移。
    static constexpr quint32 fpFaceIndexOffset = 8U; ///< 当前阵面索引相对载荷起点的线上偏移，合法值为0～3。
    static constexpr quint32 trackOffset = prefixWireSize; ///< 单个航迹点相对载荷起点的线上偏移。
    /** @brief 当前单阵面的 FPGA 状态；Executed_State 是 DP 向 FP 补发命令的提示，不等同 DCMP 命令 ACK */
    struct FpStatus
    {
        quint32 B_FP_Frame_Time = {}; ///< 区内偏移 0：FPGA帧时间原始字；表格同时出现秒和毫秒描述，暂不转换。
        quint32 B_FP_Packet_Count = {}; ///< 区内偏移 4：包计数；包个数自动累加。
        quint32 B_FP__Antenna_Idx = {}; ///< 区内偏移 8：当前阵面索引号，0～3对应1～4号阵面。
        quint32 B_FP_Primary_Confirm = {}; ///< 区内偏移 12：主定时确认；Y-2型FP暂时保留不用。
        quint32 B_FP_Executed_State = {}; ///< 区内偏移 16：已执行标志；有效时DP立即给FP发送新命令包。
        quint32 B_FP_InvalidComm_Count = {}; ///< 区内偏移 20：命令包异常累计数。
        quint32 F_FP_FrameWave = {}; ///< 区内偏移 24：控制后的波形模式字。
        quint32 B_FP_FrameWork_Attrib = {}; ///< 区内偏移 28：当前帧属性。
        quint32 B_FP_IFBw = {}; ///< 区内偏移 32：当前帧中频带宽。
        quint32 B_FP_BITSwitch = {}; ///< 区内偏移 36：当前帧BIT开关状态。
        quint32 B_FP_Pw = {}; ///< 区内偏移 40：当前帧脉冲宽度。
        quint32 B_FP_Tr = {}; ///< 区内偏移 44：当前帧Tr周期。
        quint32 B_FP_SampR0 = {}; ///< 区内偏移 48：当前帧采样起始时刻。
        quint32 B_FP_SampRn = {}; ///< 区内偏移 52：当前帧采样截至时刻或采样长度。
        quint32 B_FP_EffTrN = {}; ///< 区内偏移 56：当前帧有效脉冲个数。
        qint32 B_FP_Azim = {}; ///< 区内偏移 60：当前帧方位角，比例尺100。
        qint32 B_FP_Elev = {}; ///< 区内偏移 64：当前帧俯仰角。
        quint32 B_FP_FPGA_Version = {}; ///< 区内偏移 68：FPGA版本号。
        quint32 B_FP_FPGA_Time_DMY = {}; ///< 区内偏移 72：FPGA编译日期原始字。
        quint32 B_FP_FPGA_Time_SMH = {}; ///< 区内偏移 76：FPGA编译时间原始字。
        quint32 B_AN_Antenna_Mode = {}; ///< 区内偏移 80：天线工作状态。
        quint32 B_FS_Freq = {}; ///< 区内偏移 84：频率返回码。
        quint32 B_FP_Recv_Gain = {}; ///< 区内偏移 88：当前帧增益控制返回码。
        quint32 B_FP_Azim_Scurve = {}; ///< 区内偏移 92：当前天线方位S曲线系数。
        quint32 B_FP_Elev_Scurve = {}; ///< 区内偏移 96：当前天线俯仰S曲线系数。
        quint32 B_FP_ErrCode = {}; ///< 区内偏移 100：FPGA故障代码。
        quint32 B_AN_ErrCode = {}; ///< 区内偏移 104：天线故障代码。
        quint32 B_FP_FPGA_Temp = {}; ///< 区内偏移 108：采集板FPGA内核温度。
        qint32 B_AN_Array_Temp = {}; ///< 区内偏移 112：天线温度，范围-50～120℃，比例尺1。
        quint32 B_FS_LD1 = {}; ///< 区内偏移 116：信道模块时钟锁定信号。
        quint32 B_FS_LD2 = {}; ///< 区内偏移 120：信道模块本振锁定指示。
        quint32 B_FS_SF = {}; ///< 区内偏移 124：信道模块自检信号。
    };

    /** @brief SP 软件版本及编译信息原始字 */
    struct SpStatus
    {
        quint32 B_SP_Version = {}; ///< 区内偏移 0：SP版本号；SP端软件的版本号，具体格式待定义。
        quint32 B_SP_Time_DMY = {}; ///< 区内偏移 4：SP版本编译时间；SP端软件的编译日期(秒/分/时), Bit15~0: 秒(单位60/65536), Bit23~16: 分, Bit31~24: 时。
        quint32 B_SP_Time_SMH = {}; ///< 区内偏移 8：原始协议字，编码尚未明确。
        quint32 uni_bak6[29] = {}; ///< 区内偏移 12：备份字；初始化为零。
    };

    /** @brief DP 软件版本及编译信息原始字 */
    struct DpStatus
    {
        quint32 B_DP_Version = {}; ///< 区内偏移 0：DP版本号；DP端软件的版本号，具体格式待定义。
        quint32 B_DP_Time_DMY = {}; ///< 区内偏移 4：DP版本编译时间；DP端软件的编译日期(秒/分/时), Bit15~0: 秒(单位60/65536), Bit23~16: 分, Bit31~24: 时。
        quint32 B_DP_Time_SMH = {}; ///< 区内偏移 8：原始协议字，编码尚未明确。
        quint32 B_DP_bak[29] = {}; ///< 区内偏移 12：备份字；初始化为零。
    };

    /**
     * @brief 单个航迹点，DP 有数据即发送；所属阵面由外层 fpStatus 提供。
     * @details 字段采用 api-track.h 已确认版本；角度为十进制度，不能直接当内部 TrackPacket 的弧度。
     * 算法字段保持原值，消亡标志仅在协议边界表达，本类型不承担数据域发布或删除处理。
     */
    struct DpTrack
    {
        static constexpr quint32 wireSize = 224U; ///< 航迹点线上字节数，含九个 float 备份字。
    
        // =========================================================
        // 1. 目标及波位基本信息
        // =========================================================
        qint32 targetId = 0;              ///< 区内偏移 0： 目标识别号
        qint32 waveRow = 0;               ///< 区内偏移 4： 波位行
        qint32 waveCol = 0;               ///< 区内偏移 8： 波位列
        qint32 wave = 0;                  ///< 区内偏移 12： 波位
        qint32 rawWavePosition = 0;       ///< 区内偏移 16： 原始包头波位号
        // =========================================================
        // 2. 目标极坐标及基本运动信息
        // =========================================================
        float range = 0.0;               ///< 区内偏移 20： 雷达到目标的斜距，m
        float azimuth = 0.0;             ///< 区内偏移 24： 目标方位角，度.度
        float elevation = 0.0;           ///< 区内偏移 28： 目标俯仰角，度.度
        float radialSpeed = 0.0;         ///< 区内偏移 32： 目标径向速度，m/s，远离为负，抵近为正
        float amplitude = 0.0;           ///< 区内偏移 36： 幅度
        float altitude = 0.0;            ///< 区内偏移 40： 目标高度,相对雷达高度，m
        float courseAngle = 0.0;         ///< 区内偏移 44： 目标方位角速度,度.度/s
        float pitchAngle = 0.0;          ///< 区内偏移 48： 目标俯仰角速度,度.度/s
        float targetSNR = 0.0;           ///< 区内偏移 52： 信噪比
        // =========================================================
        // 3. 笛卡尔坐标及三维速度，雷达为坐标原点
        // =========================================================
        float x = 0.0;                   ///< 区内偏移 56： X 坐标
        float y = 0.0;                   ///< 区内偏移 60： Y 坐标
        float z = 0.0;                   ///< 区内偏移 64： Z 坐标
        float vx = 0.0;                  ///< 区内偏移 68： X 方向速度
        float vy = 0.0;                  ///< 区内偏移 72： Y 方向速度
        float vz = 0.0;                  ///< 区内偏移 76： Z 方向速度
        // =========================================================
        // 4. 聚类及波形相关信息
        // =========================================================
        qint32 clusterId = 0;             ///< 区内偏移 80： 聚类 ID
        qint32 count = 0;                 ///< 区内偏移 84： 原 count
        qint32 clutterDiagramMarkers = 0; ///< 区内偏移 88： 杂波图标志位
        qint32 waveformMark = 0;          ///< 区内偏移 92： A/B 波形标志
        // =========================================================
        // 5. 算法内部参数
        // =========================================================
        float a = 0.0F; ///< 区内偏移 96： DP 算法参数，按原值传输。
        float b = 0.0F; ///< 区内偏移 100： DP 算法参数，按原值传输。
        float c = 0.0F; ///< 区内偏移 104： DP 算法参数，按原值传输。
        qint32 tierLevel = 0;             ///< 区内偏移 108： 层级
        // =========================================================
        // 6. 景幅、生命周期及波束中心
        // =========================================================
        qint32 targetView = 0;            ///< 区内偏移 112： 原 TargetType::view
        quint32 packFlag = 0;             ///< 区内偏移116：航迹存在标志；1表示存在，0表示消亡，其他值非法。
        float savedAzimuthCenter = 0.0F;  ///< 区内偏移 120： 保存方位指向中心
        float savedElevationCenter = 0.0F;///< 区内偏移 124： 保存俯仰指向中心
        // =========================================================
        // 7. 运动学一致性验证
        // =========================================================
        float kinematicsMargin = 0.0F;    ///< 区内偏移 128： >0 在预测区间内，<0 超出
        float kinematicsDiff = 0.0F;      ///< 区内偏移 132： 运动学差值
        // =========================================================
        // 8. 数据来源及关联状态
        // =========================================================
        qint32 dataSource = 0;            ///< 区内偏移 136： 0: 探测数据，1: 跟踪数据
        qint32 assocState = 0;            ///< 区内偏移 140： 关联状态
        qint32 pointId = -1;              ///< 区内偏移 144： 原始点 ID
        qint32 multiplyRange = 0;         ///< 区内偏移 148： 广域距离扩展倍数
        qint32 multiplyDoppler = 0;       ///< 区内偏移 152： 广域速度扩展倍数
        // =========================================================
        // 9. host_computer 附加航迹信息
        // =========================================================
        quint32 accumulatedTime = 0;      ///< 区内偏移 156： 积累时间
        float trackRecognitionMark = 0.0;///< 区内偏移 160： 航迹识别标志
        float comingAndGoingSigns = 0.0; ///< 区内偏移 164： 出现/消失相关标记
        float pointPosition = 0.0;       ///< 区内偏移 168： 第几个点
        float pointType = 0.0;           ///< 区内偏移 172： 实际点/补充点：补点1，实际点0
        qint32 extrapolationCount = 0;    ///< 区内偏移 176： 未用实测点更新次数 / 外推次数
        quint32 updateFlag = 0; ///< 区内偏移 180： 0 = 未更新；1 = 已更新；
        qint32 hostView = 0;    ///< 区内偏移 184： 景幅
        // =========================================================
        // 10. 备份字
        // =========================================================
        float reserved[9] {};             ///< 区内偏移 188： 原 bak[9]
    
    };

    FpStatus fpStatus {}; ///< 载荷偏移 0，128 字节，阵面索引由 FPGA 返回。
    SpStatus spStatus {}; ///< 载荷偏移 128，128 字节。
    DpStatus dpStatus {}; ///< 载荷偏移 256，128 字节。
    DpTrack dpTrack {}; ///< 载荷偏移 384，224 字节，由 DP 提供的单个航迹点。
};

/** @brief Dp 向 DCMP 发送的 84 字节心跳载荷；在线超时由接收端 owner 判断。 */
struct DpHeartbeatPayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::DpHeartbeat; ///< 对应线消息类型。
    static constexpr quint32 wireSize = 84U; ///< 两个 uint16 及二十个 uint32 备份字。
    static constexpr quint32 frameWireSize = DcmpFrameHeader::wireSize + wireSize + DcmpFrameTail::wireSize; ///< 完整心跳报文线上字节数。
    static constexpr quint32 onlineOffset = 0U; ///< 在线值相对载荷起点的偏移。
    static constexpr quint32 faceMaskOffset = 2U; ///< 阵面位图相对载荷起点的偏移。
    static constexpr quint32 reservedOffset = 4U; ///< 二十个备份字相对载荷起点的偏移。
    static constexpr quint32 reservedWordCount = 20U; ///< 备份字数量；接收时原样保留。
    quint16 dpOnline = 0; ///< 偏移 0：1 在线，0 不在线，其他值非法。
    quint16 fpOnlineMask = 0; ///< 偏移 2：bit0～3 对应阵面 1～4，置位表示DP 能联系到对应 FP；bit4～15 必须为零。
    quint32 bak[20] {}; ///< 偏移 4：80 字节备份区，默认置零，无业务含义。
};

/** @brief Sp 向 DCMP 发送的 84 字节心跳载荷；在线超时由接收端 owner 判断。 */
struct SpHeartbeatPayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::SpHeartbeat; ///< 对应线消息类型。
    static constexpr quint32 wireSize = 84U; ///< 两个 uint16 及二十个 uint32 备份字。
    static constexpr quint32 frameWireSize = DcmpFrameHeader::wireSize + wireSize + DcmpFrameTail::wireSize; ///< 完整心跳报文线上字节数。
    static constexpr quint32 onlineOffset = 0U; ///< 在线值相对载荷起点的偏移。
    static constexpr quint32 faceMaskOffset = 2U; ///< 阵面位图相对载荷起点的偏移。
    static constexpr quint32 reservedOffset = 4U; ///< 二十个备份字相对载荷起点的偏移。
    static constexpr quint32 reservedWordCount = 20U; ///< 备份字数量；接收时原样保留。
    quint16 spOnline = 0; ///< 偏移 0：1 在线，0 不在线，其他值非法。
    quint16 fpDataMask = 0; ///< 偏移 2：bit0～3 对应阵面 1～4，置位表示对应 FP 正在回传数据；bit4～15 必须为零。
    quint32 bak[20] {}; ///< 偏移 4：80 字节备份区，默认置零，无业务含义。
};

static_assert(sizeof(DcmpToDpControlPayload) == 896U, "DcmpToDpControlPayload 尺寸不符");
static_assert(alignof(DcmpToDpControlPayload) == 4U && std::is_trivially_copyable<DcmpToDpControlPayload>::value
              && std::is_standard_layout<DcmpToDpControlPayload>::value, "DcmpToDpControlPayload 必须自然对齐且无动态资源");
static_assert(sizeof(DpToDcmpTrackPayload) == 608U, "DpToDcmpTrackPayload 尺寸不符");
static_assert(alignof(DpToDcmpTrackPayload) == 4U && std::is_trivially_copyable<DpToDcmpTrackPayload>::value
              && std::is_standard_layout<DpToDcmpTrackPayload>::value, "DpToDcmpTrackPayload 必须自然对齐且无动态资源");
static_assert(sizeof(DpHeartbeatPayload) == 84U, "DpHeartbeatPayload 尺寸不符");
static_assert(DpHeartbeatPayload::frameWireSize == 132U, "DP心跳完整报文必须保持132字节");
static_assert(alignof(DpHeartbeatPayload) == 4U && std::is_trivially_copyable<DpHeartbeatPayload>::value
              && std::is_standard_layout<DpHeartbeatPayload>::value, "DpHeartbeatPayload 必须自然对齐且无动态资源");
static_assert(sizeof(SpHeartbeatPayload) == 84U, "SpHeartbeatPayload 尺寸不符");
static_assert(SpHeartbeatPayload::frameWireSize == DpHeartbeatPayload::frameWireSize,
              "DP和SP心跳必须共用相同线布局，公共解析才可复用");
static_assert(SpHeartbeatPayload::onlineOffset == DpHeartbeatPayload::onlineOffset
              && SpHeartbeatPayload::faceMaskOffset == DpHeartbeatPayload::faceMaskOffset
              && SpHeartbeatPayload::reservedOffset == DpHeartbeatPayload::reservedOffset
              && SpHeartbeatPayload::reservedWordCount == DpHeartbeatPayload::reservedWordCount,
              "DP和SP心跳公共字段偏移必须保持一致");
static_assert(alignof(SpHeartbeatPayload) == 4U && std::is_trivially_copyable<SpHeartbeatPayload>::value
              && std::is_standard_layout<SpHeartbeatPayload>::value, "SpHeartbeatPayload 必须自然对齐且无动态资源");

} // namespace core::protocol
