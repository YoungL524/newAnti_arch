/**
 * @file toDP.h
 * @brief 交付 DP 的独立 C++14 协议头：公共封装及 0x0001、0xFFFF、0x0100、0x0101 载荷。
 * @details 从 dcmp::DcmpFrameHeader 阅读；线上为 44 字节头 + N 字节载荷 + 4 字节尾。
 * DP 心跳的 POS2 逐面实时位姿扩展见 doc/contracts/DP心跳实时位姿契约.md。
 * 声明长度为 36+N，实际长度为 48+N。控制 N=896；工作/待机 N=20；心跳 N=84，声明长度120，整包132字节。
 * 0x0100 含384字节状态及224字节航迹点，声明长度644，实际报文656字节。
 * 本文件可单独复制给 DP，无 Qt 或仓库头文件依赖；字段以应用版（1.0）_20260923.xlsx 为准，
 * 与 core/protocol 保持一致。
 * 调用方拥有报文对象；不含通信、线程或编解码。自然布局不保证线字节序，发送时应逐字段编码。
 * 日期字编码仍未明确；惯导字段按表格约定保留缩放前整数，默认控制值不能代替发送前的业务校验。
 */
#ifndef DCMP_DELIVERY_TO_DP_H
#define DCMP_DELIVERY_TO_DP_H
#include <cstdint>
#include <cstddef>
#include <type_traits>

/** @brief DCMP 与 DP 之间的外部协议类型，避免污染 DP 工程的全局类型名。 */
namespace dcmp {
/**
 * @brief 工作表指定的 32 位线协议消息类型，不作为进程内分发标识。
 * DCMP-00;DP-01;FPGA-02;SP-03;
 */
enum class DcmpMessageType : std::uint32_t
{
    DcmpToDpControl = 0x0001U, ///< DCMP 向 DP 发送控制指令。
    DcmpToDpWorkMode = 0xFFFFU, ///< DCMP 向 DP 发送工作/待机指令。
    DpToDcmpTrack = 0x0100U,  ///< DP 向 DCMP 返回状态及航迹。
    DpHeartbeat = 0x0101U,    ///< DP 向 DCMP 发送心跳。
};

/**
 * @brief 所有消息共用的 44 字节公共头，包含起始标志及十个系统信息字。
 * @details F_UI_TotalLength 不计入自身，不含起始标志和包尾，因此值为 36+N；实际报文为 48+N。
 * N 必须是 4 的倍数，载荷补齐字节置零。默认对象尚未指定消息类型和有效时间，不代表可发送业务报文。
 * 日期、软件版本及编译时间编码未完全明确，暂保留原始字，不推测转换为 UTC。
 */
struct DcmpFrameHeader
{
    static constexpr std::uint32_t wireSize = 44U; ///< 公共头线上字节数，包含起始标志。
    static constexpr std::uint32_t declaredFixedSize = 36U; ///< 长度字段计入的固定字节数。
    static constexpr std::uint32_t expectedMagic = 0xA1A11A1AU; ///< 工作表定义的起始标志。
    static constexpr std::uint32_t expectedVersion = 20260915U; ///< 工作表定义的协议版本数值。

    std::uint32_t F_UI_Package_Head = expectedMagic; ///< 偏移 0：起始标志。
    std::uint32_t F_UI_TotalLength = declaredFixedSize; ///< 偏移 4：36+N 字节，不含F_UI_TotalLength、起始标志和包尾。
    DcmpMessageType F_UI_MessageType {}; ///< 偏移 8：消息类型；默认零未指定。
    std::uint32_t F_UI_Protocol_Version = expectedVersion; ///< 偏移 12：协议版本，非软件版本。
    std::uint32_t F_UI_GPS_Time_DMY = 0; ///< 偏移 16：外部 GPS 日期原始字，年月日编码待明确。
    std::uint32_t F_UI_GPS_Time_SMH = 0; ///< 偏移 20：低 16 位秒（60/65536 s），随后 8 位分、8 位时。
    std::uint32_t F_UI_SYS_Time_DMY = 0; ///< 偏移 24：电脑日期原始字，年月日编码待明确。
    std::uint32_t F_UI_SYS_Time_SMH = 0; ///< 偏移 28：北京时间原始字；不是 UTC 或单调时钟。
    std::uint32_t F_UI_DCMP_Version = 0; ///< 偏移 32：显控软件版本原始字，编码待明确。
    std::uint32_t F_UI_DCMP_Time_DMY = 0; ///< 偏移 36：编译日期原始字；表格名称与时分秒说明有冲突，暂不转换。
    std::uint32_t F_UI_DCMP_Time_SMH = 0; ///< 偏移 40：编译时间原始字，表格尚未明确编码。
};

/**
 * @brief 紧随可变长载荷之后的公共包尾。
 * @details 线上偏移为 44+N，不能把它直接紧贴公共头存放后当作有载荷整包发送。
 */
struct DcmpFrameTail
{
    static constexpr std::uint32_t wireSize = 4U; ///< 包尾线上字节数，不计入 F_UI_TotalLength。
    static constexpr std::uint32_t expectedMagic = 0xB1B2A1A2U; ///< 工作表指定的结束标志。
    std::uint32_t packetTail = expectedMagic; ///< 偏移 44+N：结束标志，无备份字。
};

static_assert(sizeof(DcmpFrameHeader) == 44U, "公共头必须保持 44 字节");
static_assert(alignof(DcmpFrameHeader) == alignof(std::uint32_t), "公共头必须自然对齐");
static_assert(sizeof(DcmpFrameTail) == 4U, "包尾必须保持 4 字节");
static_assert(std::is_standard_layout<DcmpFrameHeader>::value
              && std::is_trivially_copyable<DcmpFrameHeader>::value,
              "公共头必须是无动态资源的标准布局值对象");

/**
 * @brief DCMP 发给 DP 的 20 字节工作/待机载荷。
 * @details 使用统一公共头尾，消息类型为 0xFFFF；本地采集启停成功后由 DCMP 各发送一包。
 */
struct DcmpToDpWorkModePayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::DcmpToDpWorkMode; ///< 公共头中的消息类型。
    static constexpr std::uint32_t wireSize = 20U; ///< 仅载荷字节数，不含公共头尾。
    std::uint32_t workStandbyMode = 1U; ///< 偏移 0：0 待机，1 工作；2、3 保留，当前禁止发送。
    std::int32_t reserved[4] {}; ///< 偏移 4：16 字节备份区，无业务含义，发送时置零。
};

/** @brief 0x0001：DCMP 发给 DP 的 896 字节控制载荷，含四面独立控制区。 */
struct DcmpToDpControlPayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::DcmpToDpControl; ///< 对应线消息类型。
    static constexpr std::uint32_t wireSize = 896U; ///< 仅载荷字节数，不含公共头尾。
    /** @brief 通用阵面及 SP/DP 控制信息 */
    struct CommonControl
    {
        std::uint32_t F_UI_Combi_Mode = {}; ///< 区内偏移 0：阵面组合应用模式；00：常规使用模式 01：合成使用模式 （保留这个接口，暂时可以不做任何响应）。
        std::uint32_t F_UI_FrameWork_Mode = {}; ///< 区内偏移 4：帧工作模式；00：景幅连续模式 01：单帧连续模式（为DP自主调用的控制值，显控上不可选） 02：单帧测试模式。
        std::uint32_t F_UI_SysWork_Mode = {}; ///< 区内偏移 8：系统工作主模式；00：Air 01：Groud 02：Sea 03：Weather 04：MultiFunc 保留这个接口，可暂时不用。
        std::uint32_t F_UI_SysRange_Mode = {}; ///< 区内偏移 12：探测量程控制；00：2km 01：5km ……。
        std::uint32_t F_UI_STC_Switch = {}; ///< 区内偏移 16：STC电路开关（开电置0）；00：关（默认关） 01：开。
        std::uint32_t F_UI_STC_Mode = {}; ///< 区内偏移 20：STC方案；在STC开关打开时起效 00：方案一 01：方案二 10：方案三 11：方案四。
        std::uint32_t F_UI_Exc_Switch = {}; ///< 区内偏移 24：激励信号关闭开关；00：激励信号开（默认开） 01：激励信号关。
        std::int32_t F_UI_CFAR_Mode_Ctrl = {}; ///< 区内偏移 28：CFAR方案控制；00：方案一 01：方案二。
        std::uint32_t F_UI_STBY_Ctrl = {}; ///< 区内偏移 32：采集工作控制；00：停止采集，01：开始或保持采集。
        std::int32_t F_UI_CFAR_Ctrl_1 = {}; ///< 区内偏移 36：CFAR门限控制1。
        std::int32_t F_UI_CFAR_Ctrl_2 = {}; ///< 区内偏移 40：CFAR门限控制2。
        std::int32_t F_UI_Vel_Ctrl = {}; ///< 区内偏移 44：速度门限控制。
        std::uint32_t F_UI_AD_Rec_Switch = {}; ///< 区内偏移 48：AD数据记录开关；00：记录关（默认关） 01：记录开。
        std::uint32_t F_UI_Dot_Rec_Switch = {}; ///< 区内偏移 52：点迹数据记录开关；00：记录关（默认关） 01：记录开。
        std::uint32_t F_UI_TracDot_Ctrl = {}; ///< 区内偏移 56：起批点数。
        std::uint32_t F_UI_DotMerge_Rec_Switch = {}; ///< 区内偏移 60：凝聚数据记录开关；00：记录关（默认关） 01：记录开。
        std::uint32_t F_UI_DotCorr_Rec_Switch = {}; ///< 区内偏移 64：关联数据记录开关；00：记录关（默认关） 01：记录开。
        std::uint32_t uni_bak2[15] = {}; ///< 区内偏移 68：备份字；初始化为零。
    };

    /** @brief 连续模式及单帧模式的控制信息 */
    struct FrameControl
    {
        std::uint32_t F_UI_ScreenWave_Mod_Ctrl = {}; ///< 区内偏移 0：景幅波形控制方案；00：波形方案一； 01：波形方案二； 02：波形方案三； ……。
        std::uint32_t F_UI_ScreenTrack_Enable = {}; ///< 区内偏移 4：景幅跟踪使能；00：关（默认关） 01：开。
        std::uint32_t F_DP_ScreenTrack_Azim_1 = {}; ///< 区内偏移 8：第1个跟踪点方位角；对最多四个目标实现景幅连续模式下的跟踪（此跟踪位置也为DP自主控制，不用在显控界面上设计人机接口）。
        std::uint32_t F_DP_ScreenTrack_Elev_1 = {}; ///< 区内偏移 12：第1个跟踪点俯仰角。
        std::uint32_t F_DP_ScreenTrack_Azim_2 = {}; ///< 区内偏移 16：第1个跟踪点方位角。
        std::uint32_t F_DP_ScreenTrack_Elev_2 = {}; ///< 区内偏移 20：第1个跟踪点俯仰角。
        std::uint32_t F_DP_ScreenTrack_Azim_3 = {}; ///< 区内偏移 24：第1个跟踪点方位角。
        std::uint32_t F_DP_ScreenTrack_Elev_3 = {}; ///< 区内偏移 28：第1个跟踪点俯仰角。
        std::uint32_t F_DP_ScreenTrack_Azim_4 = {}; ///< 区内偏移 32：第1个跟踪点方位角。
        std::uint32_t F_DP_ScreenTrack_Elev_4 = {}; ///< 区内偏移 36：第1个跟踪点俯仰角。
        std::int32_t F_UIDP_Beam_Azim_Ctrl = {}; ///< 区内偏移 40：波束指向控制方位角；单帧模式（包含单独连续、单帧测试）下的波束指向控制，有符号数，补码表示，范围：[-45,45]度，比例尺：100。界面上人工输入值适用于单帧测试模式，在单帧连续模式下，这个值是DP直接控制的。
        std::int32_t F_UIDP_Beam_Elev_Ctrl = {}; ///< 区内偏移 44：波束指向控制俯仰角。
        std::uint32_t F_UIDP_Band_Switch = {}; ///< 区内偏移 48：中频带宽切换控制字；00：带宽1 01：带宽2 02：带宽3 ……。
        std::uint32_t F_UIDP_BIT_Switch_Ctrl = {}; ///< 区内偏移 52：BIT开关；00：关（默认关） 01：开。
        std::uint32_t F_UIDP_FrameWave_Ctrl = {}; ///< 区内偏移 56：波形模式字选择；0x11：波形1 0x12：波形2 …… 0x21：波形m 0x22：波形n …… 0xaa：自定义波形（在自定义波形情况下，响应调制形式、调制带宽、脉宽等参数设置） 十六进制表示对应的各个具体波形，具体波形设定好后再分配。 另：显控上选择的波形模式字主要用于单帧模式的试验，正常工作时是DP根据情况自动选择的。
        std::uint32_t F_UIDP_Modul_Ctrl = {}; ///< 区内偏移 60：调制形式；00：线性调频脉压 01：普通脉冲调制 ……。
        std::uint32_t F_UIDP_ModulBw_Ctrl = {}; ///< 区内偏移 64：调频带宽；以MHz为单位，格式FPGA定义。
        std::uint32_t F_UIDP_Pw_Ctrl = {}; ///< 区内偏移 68：脉宽；以us为单位，格式FPGA定义。
        std::uint32_t F_UIDP_Tr_Ctrl = {}; ///< 区内偏移 72：重复周期；以us为单位，格式FPGA定义。
        std::uint32_t F_UIDP_FrameWait_Ctrl = {}; ///< 区内偏移 76：帧间等待周期数；填零脉冲个数，格式FPGA定义。
        std::uint32_t F_UIDP_EffTrN_Ctrl = {}; ///< 区内偏移 80：有效脉冲个数；采样对应的脉冲数。
        std::uint32_t F_UIDP_SampR0_Ctrl = {}; ///< 区内偏移 84：Tr内采样值选取送出的起始位置；FPGA自定义（要考虑当采用开窗采样时，要能够适应此时数据包的长度变化）。
        std::uint32_t F_UIDP_SampRn_Ctrl = {}; ///< 区内偏移 88：Tr内采样值选取送出的截止时刻 （或采样长度）。
        std::uint32_t F_UIDP_FrameWaveWrite_Ctrl = {}; ///< 区内偏移 92：写入指令；00：不写入（默认） 01：写入。
        std::uint32_t F_UIDP_WritePos_Ctrl = {}; ///< 区内偏移 96：写入位置；以模式字定义，可以写入的模式字范围为0x11~0x99。
        std::uint32_t uni_bak3[7] = {}; ///< 区内偏移 100：备份字；初始化为零。
    };

    /** @brief 平台惯导原始信息 */
    struct Navigation
    {
        std::uint32_t F_FL_Platform_GPS_Long = {}; ///< 区内偏移 0：载体平台经度，接收值除以1000000。
        std::uint32_t F_FL_Platform_GPS_Lati = {}; ///< 区内偏移 4：载体平台纬度，接收值除以1000000。
        std::int32_t F_FL_Platform_Height = {}; ///< 区内偏移 8：载体平台高度，接收值除以100。
        std::int32_t F_FL_Platform_HeadingAngl = {}; ///< 区内偏移 12：载体平台航向角，接收值除以100。
        std::int32_t F_FL_Platform_ElAngl = {}; ///< 区内偏移 16：载体平台俯仰角，接收值除以100。
        std::int32_t F_FL_Platform_RolAngl = {}; ///< 区内偏移 20：载体平台横滚角，接收值除以100。
        std::int32_t F_FL_Platform_heading_AnglVel = {}; ///< 区内偏移 24：载体平台航向角速度，接收值除以100。
        std::int32_t F_FL_Platform_El_AnglVel = {}; ///< 区内偏移 28：载体平台俯仰角速度，接收值除以100。
        std::int32_t F_FL_Platform_Rol_AnglVel = {}; ///< 区内偏移 32：载体平台横滚角速度，接收值除以100。
        std::int32_t F_FL_Platform_Vel_East = {}; ///< 区内偏移 36：载体平台速度东向分量，接收值除以100。
        std::int32_t F_FL_Platform_Vel_North = {}; ///< 区内偏移 40：载体平台速度北向分量，接收值除以100。
        std::int32_t F_FL_Platform_Vel_Up = {}; ///< 区内偏移 44：载体平台速度天向分量，接收值除以100。
        std::int32_t F_FL_AntRef_X_Vel = {}; ///< 区内偏移 48：天线系速度X分量，接收值除以100。
        std::int32_t F_FL_AntRef_Y_Vel = {}; ///< 区内偏移 52：天线系速度Y分量，接收值除以100。
        std::int32_t F_FL_AntRef_Z_Vel = {}; ///< 区内偏移 56：天线系速度Z分量，接收值除以100。
        std::int32_t F_FL_Platform_Vel_Ground = {}; ///< 区内偏移 60：载体平台地速，接收值除以100。
        std::int32_t F_FL_Platform_Vel_Acc = {}; ///< 区内偏移 64：载体平台加速度，接收值除以100。
        std::int32_t F_FL_Platform_YawAngl = {}; ///< 区内偏移 68：载体平台偏航角，接收值除以100。
        std::uint32_t uni_bak4[14] = {}; ///< 区内偏移 72：备份字；初始化为零。
    };

    /** @brief 单阵面控制信息；数组下标替代工作表字段名的 No1～No4 后缀 */
    struct FaceControl
    {
        std::uint32_t F_UI_Ant_Idx = {}; ///< 区内偏移 0：天线控制索引号；00：1#阵面 01：2#阵面 10：3#阵面 11：4#阵面 在本包内，此处赋值为“00”，即1#阵面。
        std::uint32_t F_UI_FreBand_Pick = {}; ///< 区内偏移 4：工作频段选择；00：X波段 01：Ku波段。
        std::uint32_t F_UI_Freq_Ctrl = {}; ///< 区内偏移 8：频率控制字；频率控制字： 000000：频率0 000001：频率1 000010：频率2 …… 100111：频率39 有多少个频点，就使用多少号，每一个号码对应的频点值提供即可；另外，不同频段下如果频点数量不同，超出的号码不响应即可。
        std::uint32_t F_UI_Recv_Gain_Ctrl = {}; ///< 区内偏移 12：接收增益控制；接收增益控制（在STC功能关闭时起作用） bit [9]:接收通道第二级衰减控制（IF）衰减16dB bit [8]:接收通道第二级衰减控制（IF）衰减08dB bit [7]:接收通道第二级衰减控制（IF）衰减04dB bit [6]:接收通道第二级衰减控制（IF）衰减02dB bit [5]:接收通道第二级衰减控制（IF）衰减01dB bit [4]:接收通道第一级衰减器控制（RF）衰减16dB bit [3]:接收通道第一级衰减器控制（RF）衰减08dB bit [2]:接收通道第一级衰减器控制（RF）衰减04dB bit [1]:接收通道第一级衰减器控制（RF）衰减02dB bit [0]:接收通道第一级衰减器控制（RF）衰减01dB。
        std::uint32_t F_UI_Antenna_Mode = {}; ///< 区内偏移 16：天线模式（默认待机模式）；00：天线待机 01：只接收加电 10：只发射加电 11：收发均加电。
        std::uint32_t F_UI_Power_Mode = {}; ///< 区内偏移 20：远程断电（默认均上电）；bit[2]=0: 风扇1电源打开 bit[2]=1: 风扇1电源关闭 bit[1]=0: 风扇2电源打开 bit[1]=1: 风扇2电源关闭 bit[0]=0: TR组件电源打开 bit[0]=1: TR组件电源关闭。
        std::uint32_t F_UI_RF_Reset = {}; ///< 区内偏移 24：天线与射频系统复位；0x55：复位 0xaa：正常工作。
        std::int32_t F_DP_C1 = {}; ///< 区内偏移 28：C1；BIT反馈参数1。
        std::int32_t F_DP_C2 = {}; ///< 区内偏移 32：C2；BIT反馈参数2。
        std::int32_t F_DP_C3 = {}; ///< 区内偏移 36：C3；BIT反馈参数3。
        std::int32_t F_DP_C4 = {}; ///< 区内偏移 40：C4；BIT反馈参数4。
        std::int32_t F_DP_C5 = {}; ///< 区内偏移 44：C5；BIT反馈参数5。
        std::int32_t F_DP_C6 = {}; ///< 区内偏移 48：C6；BIT反馈参数6。
        std::int32_t F_UI_AntCourse_Angle = {}; ///< 区内偏移 52：该阵面安装航向角；有符号数，补码表示，范围：[-180,180]度，比例尺：100。。
        std::int32_t F_UI_AntPitch_Angle = {}; ///< 区内偏移 56：该阵面安装俯仰角；有符号数，补码表示，范围：[-90,90]度，比例尺：100。。
        std::int32_t F_UI_AntRoll_Angle = {}; ///< 区内偏移 60：该阵面安装横滚角；有符号数，补码表示，范围：[-90,90]度，比例尺：100。。
        std::uint32_t F_UI_Antenna_bak[16] = {}; ///< 区内偏移 64：备份字；初始化为零。
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
    static constexpr std::uint32_t prefixWireSize = 384U; ///< 状态前缀字节数。
    static constexpr std::uint32_t wireSize = 608U; ///< 状态前缀加单个航迹点的完整载荷长度。
    /** @brief 当前单阵面的 FPGA 状态；Executed_State 是 DP 向 FP 补发命令的提示，不等同 DCMP 命令 ACK */
    struct FpStatus
    {
        std::uint32_t B_FP_Frame_Time = {}; ///< 区内偏移 0：FPGA帧时间原始字；表格同时出现秒和毫秒描述，暂不转换。
        std::uint32_t B_FP_Packet_Count = {}; ///< 区内偏移 4：包计数；包个数自动累加。
        std::uint32_t B_FP__Antenna_Idx = {}; ///< 区内偏移 8：当前阵面索引号，0～3对应1～4号阵面。
        std::uint32_t B_FP_Screen_Count = {}; ///< 区内偏移 12：FPGA 景幅计数，从 1 开始累计；不再表示主定时确认。
        std::uint32_t B_FP_Executed_State = {}; ///< 区内偏移 16：已执行标志；有效时DP立即给FP发送新命令包。
        std::uint32_t B_FP_InvalidComm_Count = {}; ///< 区内偏移 20：命令包异常累计数。
        std::uint32_t F_FP_FrameWave = {}; ///< 区内偏移 24：控制后的波形模式字。
        std::uint32_t B_FP_FrameWork_Attrib = {}; ///< 区内偏移 28：当前帧属性。
        std::uint32_t B_FP_IFBw = {}; ///< 区内偏移 32：当前帧中频带宽。
        std::uint32_t B_FP_BITSwitch = {}; ///< 区内偏移 36：当前帧BIT开关状态。
        std::uint32_t B_FP_Pw = {}; ///< 区内偏移 40：当前帧脉冲宽度。
        std::uint32_t B_FP_Tr = {}; ///< 区内偏移 44：当前帧Tr周期。
        std::uint32_t B_FP_SampR0 = {}; ///< 区内偏移 48：当前帧采样起始时刻。
        std::uint32_t B_FP_SampRn = {}; ///< 区内偏移 52：当前帧采样截至时刻或采样长度。
        std::uint32_t B_FP_EffTrN = {}; ///< 区内偏移 56：当前帧有效脉冲个数。
        std::int32_t B_FP_Azim = {}; ///< 区内偏移 60：当前帧方位角，比例尺100。
        std::int32_t B_FP_Elev = {}; ///< 区内偏移 64：当前帧俯仰角。
        std::uint32_t B_FP_FPGA_Version = {}; ///< 区内偏移 68：FPGA版本号。
        std::uint32_t B_FP_FPGA_Time_DMY = {}; ///< 区内偏移 72：FPGA编译日期原始字。
        std::uint32_t B_FP_FPGA_Time_SMH = {}; ///< 区内偏移 76：FPGA编译时间原始字。
        std::uint32_t B_AN_Antenna_Mode = {}; ///< 区内偏移 80：天线工作状态。
        std::uint32_t B_FS_Freq = {}; ///< 区内偏移 84：频率返回码。
        std::uint32_t B_FP_Recv_Gain = {}; ///< 区内偏移 88：当前帧增益控制返回码。
        std::uint32_t B_FP_Azim_Scurve = {}; ///< 区内偏移 92：当前天线方位S曲线系数。
        std::uint32_t B_FP_Elev_Scurve = {}; ///< 区内偏移 96：当前天线俯仰S曲线系数。
        std::uint32_t B_FP_ErrCode = {}; ///< 区内偏移 100：FPGA故障代码。
        std::uint32_t B_AN_ErrCode = {}; ///< 区内偏移 104：天线故障代码。
        std::uint32_t B_FP_FPGA_Temp = {}; ///< 区内偏移 108：采集板FPGA内核温度。
        std::int32_t B_AN_Array_Temp = {}; ///< 区内偏移 112：天线温度，范围-50～120℃，比例尺1。
        std::uint32_t B_FS_LD1 = {}; ///< 区内偏移 116：信道模块时钟锁定信号。
        std::uint32_t B_FS_LD2 = {}; ///< 区内偏移 120：信道模块本振锁定指示。
        std::uint32_t B_FS_SF = {}; ///< 区内偏移 124：信道模块自检信号。
    };

    /** @brief SP 软件版本及编译信息原始字 */
    struct SpStatus
    {
        std::uint32_t B_SP_Version = {}; ///< 区内偏移 0：SP版本号；SP端软件的版本号，具体格式待定义。
        std::uint32_t B_SP_Time_DMY = {}; ///< 区内偏移 4：SP版本编译时间；SP端软件的编译日期(秒/分/时), Bit15~0: 秒(单位60/65536), Bit23~16: 分, Bit31~24: 时。
        std::uint32_t B_SP_Time_SMH = {}; ///< 区内偏移 8：原始协议字，编码尚未明确。
        std::uint32_t uni_bak6[29] = {}; ///< 区内偏移 12：备份字；初始化为零。
    };

    /** @brief DP 软件版本及编译信息原始字 */
    struct DpStatus
    {
        std::uint32_t B_DP_Version = {}; ///< 区内偏移 0：DP版本号；DP端软件的版本号，具体格式待定义。
        std::uint32_t B_DP_Time_DMY = {}; ///< 区内偏移 4：DP版本编译时间；DP端软件的编译日期(秒/分/时), Bit15~0: 秒(单位60/65536), Bit23~16: 分, Bit31~24: 时。
        std::uint32_t B_DP_Time_SMH = {}; ///< 区内偏移 8：原始协议字，编码尚未明确。
        std::uint32_t B_DP_bak[29] = {}; ///< 区内偏移 12：备份字；初始化为零。
    };

    /**
     * @brief 单个航迹点，DP 有数据即发送；所属阵面由外层 fpStatus 提供。
     * @details 字段采用 api-track.h 已确认版本；角度为十进制度，不能直接当内部 TrackPacket 的弧度。
     * 算法字段保持原值，消亡标志仅在协议边界表达，本类型不承担数据域发布或删除处理。
     */
    struct DpTrack
    {
        static constexpr std::uint32_t wireSize = 224U; ///< 航迹点线上字节数，含九个 float 备份字。
    
        // =========================================================
        // 1. 目标及波位基本信息
        // =========================================================
        std::int32_t targetId = 0;              ///< 区内偏移 0： 目标识别号
        std::int32_t waveRow = 0;               ///< 区内偏移 4： 波位行
        std::int32_t waveCol = 0;               ///< 区内偏移 8： 波位列
        std::int32_t wave = 0;                  ///< 区内偏移 12： 波位
        std::int32_t rawWavePosition = 0;       ///< 区内偏移 16： 原始包头波位号
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
        std::int32_t clusterId = 0;             ///< 区内偏移 80： 聚类 ID
        std::int32_t count = 0;                 ///< 区内偏移 84： 原 count
        std::int32_t clutterDiagramMarkers = 0; ///< 区内偏移 88： 杂波图标志位
        std::int32_t waveformMark = 0;          ///< 区内偏移 92： A/B 波形标志
        // =========================================================
        // 5. 算法内部参数
        // =========================================================
        float a = 0.0F; ///< 区内偏移 96： DP 算法参数，按原值传输。
        float b = 0.0F; ///< 区内偏移 100： DP 算法参数，按原值传输。
        float c = 0.0F; ///< 区内偏移 104： DP 算法参数，按原值传输。
        std::int32_t tierLevel = 0;             ///< 区内偏移 108： 层级
        // =========================================================
        // 6. 景幅、生命周期及波束中心
        // =========================================================
        std::int32_t targetView = 0;            ///< 区内偏移 112： 原 TargetType::view
        std::uint32_t packFlag = 0;             ///< 区内偏移116：航迹存在标志；1表示存在，0表示消亡，其他值非法。
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
        std::int32_t dataSource = 0;            ///< 区内偏移 136： 0: 探测数据，1: 跟踪数据
        std::int32_t assocState = 0;            ///< 区内偏移 140： 关联状态
        std::int32_t pointId = -1;              ///< 区内偏移 144： 原始点 ID
        std::int32_t multiplyRange = 0;         ///< 区内偏移 148： 广域距离扩展倍数
        std::int32_t multiplyDoppler = 0;       ///< 区内偏移 152： 广域速度扩展倍数
        // =========================================================
        // 9. host_computer 附加航迹信息
        // =========================================================
        std::uint32_t accumulatedTime = 0;      ///< 区内偏移 156： 积累时间
        float trackRecognitionMark = 0.0;///< 区内偏移 160： 航迹识别标志
        float comingAndGoingSigns = 0.0; ///< 区内偏移 164： 出现/消失相关标记
        float pointPosition = 0.0;       ///< 区内偏移 168： 第几个点
        float pointType = 0.0;           ///< 区内偏移 172： 实际点/补充点：补点1，实际点0
        std::int32_t extrapolationCount = 0;    ///< 区内偏移 176： 未用实测点更新次数 / 外推次数
        std::uint32_t updateFlag = 0; ///< 区内偏移 180： 0 = 未更新；1 = 已更新；
        std::int32_t hostView = 0;    ///< 区内偏移 184： 景幅
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

/** @brief DP 向 DCMP 发送的 104 字节心跳载荷；四面分别提供位置和姿态。 */
struct DpHeartbeatPayload
{
    static constexpr DcmpMessageType messageType = DcmpMessageType::DpHeartbeat; ///< 对应线消息类型。
    static constexpr std::uint32_t wireSize = 104U; ///< 两个 uint16、四面各六个 int32 和格式标记。
    static constexpr std::uint32_t expectedPoseFormatTag = 0x32534F50U; ///< 小端字节为 POS2，区分旧版心跳。
    std::uint16_t dpOnline = 0; ///< 偏移 0：1 在线，0 不在线，其他值非法。
    std::uint16_t fpOnlineMask = 0; ///< 偏移 2：bit0～3 对应阵面 1～4，置位表示DP 能联系到对应 FP；bit4～15 必须为零。
    std::int32_t facePose[4][6] {}; ///< 偏移 4：每面依次为经度 1e-6°、纬度 1e-6°、MSL 海拔 cm、航向/俯仰/横滚 0.01°；仅在线面有效。
    std::uint32_t poseFormatTag = expectedPoseFormatTag; ///< 偏移 100：POS2 表示本包提供逐面实时位姿。
};

static_assert(sizeof(DcmpToDpWorkModePayload) == DcmpToDpWorkModePayload::wireSize,
              "DcmpToDpWorkModePayload 尺寸不符");
static_assert(alignof(DcmpToDpWorkModePayload) == 4U
                  && std::is_trivially_copyable<DcmpToDpWorkModePayload>::value
                  && std::is_standard_layout<DcmpToDpWorkModePayload>::value,
              "DcmpToDpWorkModePayload 必须自然对齐且无动态资源");
static_assert(sizeof(DcmpToDpControlPayload) == 896U, "DcmpToDpControlPayload 尺寸不符");
static_assert(sizeof(DpToDcmpTrackPayload) == 608U, "DpToDcmpTrackPayload 尺寸不符");
static_assert(sizeof(DpHeartbeatPayload) == 104U, "DpHeartbeatPayload 尺寸不符");

} // namespace dcmp
#endif
