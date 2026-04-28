#pragma once
/**
 * @file StructureSend.h
 * @brief 发送数据结构体定义文件（上位机 → CPU/雷达）
 * @details 纯数据结构体，不包含任何业务逻辑。
 *          保持现有的雷达协议结构不变，与硬件端协议一一对应。
 *          本文件为协议定义的唯一正式版本。
 */
#include <iostream>
#include <vector>
#include <QMetaType>
#pragma pack(push, 1) // 强制单字节对齐

/**
 * @brief 大包头 -- 整个雷达系统的通用协议
 * @details 与上位机内部无关，上位机向外发送
 */
struct DataPackageHeader
{
    unsigned int F_UI_Package_Head; // 包头起始握手标志
    unsigned int F_UI_Time_Code; // 时间码
    unsigned int F_UI_Work_Mode; // 工作模式（帧性质）
    unsigned int F_UI_Scene_Num = 1; // 景幅序号
    unsigned int F_UI_GPS_Time_DMY;  // GPS时间(日月年)
    unsigned int F_UI_GPS_Time_SMH;  // GPS时间(秒分时)
    unsigned int F_UI_SYS_Time_DMY;  // 系统时间(日月年)
    unsigned int F_UI_SYS_Time_SMH;  // 系统时间(秒分时)
    unsigned int F_UI_CPU_Version;   // CPU版本号
    unsigned int F_UI_CPU_Time_DMY;  // CPU版本时间(日月年)
    unsigned int F_UI_CPU_Time_SMH;  // CPU版本时间(秒分时)
    unsigned int F_UI_GPU_Version;   // GPU版本号
    unsigned int F_UI_GPU_Time_DMY;  // GPU版本时间(日月年)
    unsigned int F_UI_GPU_Time_SMH;  // GPU版本时间(秒分时)
    unsigned int B_UI_Packet_Length; // 包长度,0-65535
    unsigned int B_UI_Frame_Start_Time; // 帧时间
    unsigned int B_UI_Test_Mode; // 测试信息收集有效字
    unsigned int B_UI_Packet_Count; // 包计数
    unsigned int B_UI_FPGA_Version; // FPGA版本号
    unsigned int B_UI_FPGA_Time_DMY; // FPGA版本时间(日月年)
    unsigned int B_UI_FPGA_Time_SMH; // FPGA版本时间(秒分时)
    unsigned int bak1[11]; // 备份字
    unsigned int F_UI_Antenna_Idx; // 天线控制索引号
    unsigned int F_FL_Track_PosNum; // 跟踪波位数量
    unsigned int F_FL_Track_Pos_1; // 第1个跟踪位置
    unsigned int F_FL_Track_Pos_2; // 第2个跟踪位置
    unsigned int F_FL_Track_Pos_3; // 第3个跟踪位置
    unsigned int F_FL_Track_Pos_4; // 第4个跟踪位置
    unsigned int F_FL_Track_Pos_5; // 第5个跟踪位置
    unsigned int F_FL_Track_Pos_6; // 第6个跟踪位置
    unsigned int F_FL_Track_Pos_7; // 第7个跟踪位置
    unsigned int F_FL_Track_Pos_8; // 第8个跟踪位置
    unsigned int F_FL_Track_Pos_9; // 第9个跟踪位置
    unsigned int F_FL_Track_Pos_10; // 第10个跟踪位置
    unsigned int F_FL_Track_Pos_11; // 第11个跟踪位置
    unsigned int F_FL_Track_Pos_12; // 第12个跟踪位置
    unsigned int F_FL_Track_Pos_13; // 第13个跟踪位置
    unsigned int F_FL_Track_Pos_14; // 第14个跟踪位置
    unsigned int F_UI_IF_Loss_Ctrl; // 中频衰减控制
    unsigned int F_UI_Freq_Ctrl = 00; // 频率控制字
    unsigned int F_UI_Wave_Ctrl; // 波形控制字
    unsigned int F_UI_Power_Mode; // 远程断电（默认均上电）
    unsigned int F_UI_Antenna_Mode; // 天线模式（默认待机模式）
    unsigned int F_UI_Exc_Switch; // 激励信号开关
    int   F_FL_Beam_Azim_Ctrl; // 波束指向控制方位角
    int   F_FL_Beam_Elev_Ctrl; // 波束指向控制俯仰角
    unsigned int F_UI_STC_Switch; // STC电路开关（开电置0）
    unsigned int F_UI_STC_Mode; // STC方案
    int F_IN_A_Cali_Coef; // A通道校准系数
    int F_IN_B_Cali_Coef; // B通道校准系数
    int F_IN_C_Cali_Coef; // C通道校准系数
    unsigned int F_UI_Clear_Flag; // 计数器清零
    unsigned int F_UI_RF_Reset; // 天线与射频系统复位
    unsigned int Wave_Num; // 波形选择
    unsigned int F_FL_T1; // CFAR门限1
    unsigned int F_FL_T2; // CFAR门限2
    unsigned int F_FL_Fc; // 杂波频率
    unsigned int F_FL_FcR; // 杂波距离
    unsigned int F_FL_FcW; // 杂波宽度
    unsigned int F_FL_Frame_WT; // 帧间等待时间
    unsigned int F_UI_Noise_Flag; // 噪声处理标志
    unsigned int F_UI_Target_Type; // 目标类型
    unsigned int F_UI_Save_Flag = 0; // 数据保存标志
    char         F_CR_Save_Path[64]; // 数据保存路径
    unsigned int bak3[7]; // 备份字
    unsigned int F_FL_Platform_Height; // 平台高度
    unsigned int F_FL_Platform_GPS_Long; // 平台GPS位置_经度
    unsigned int F_FL_Platform_GPS_Lati; // 平台GPS位置_纬度
    unsigned int F_FL_Antenna_Normal_1_Azim; // 1号天线法线指向方位角
    unsigned int F_FL_Antenna_Normal_1_Elev; // 1号天线法线指向俯仰角
    unsigned int F_FL_Antenna_Normal_2_Azim; // 2号天线法线指向方位角
    unsigned int F_FL_Antenna_Normal_2_Elev; // 2号天线法线指向俯仰角
    unsigned int F_FL_Antenna_Normal_3_Azim; // 3号天线法线指向方位角
    unsigned int F_FL_Antenna_Normal_3_Elev; // 3号天线法线指向俯仰角
    unsigned int F_FL_Antenna_Normal_4_Azim; // 4号天线法线指向方位角
    unsigned int F_FL_Antenna_Normal_4_Elev; // 4号天线法线指向俯仰角
    unsigned int bak4[21]; // 备份字
    unsigned int B_UI_Antenna_Data_Idx; // 天线数据索引号
    unsigned int B_UI_Wave_Position; // 当前帧波位号
    unsigned int B_UI_Wave_Num; // 本帧波形码
    unsigned int B_UI_Antenna_Mode; // 天线工作状态（默认待机模式）
    unsigned int B_FL_F0; // 当前帧频率码
    unsigned int B_FL_Bw; // 当前帧带宽
    unsigned int B_FL_Kr; // 当前帧调频斜率
    unsigned int B_FL_Fs; // 当前帧采样率
    unsigned int B_FL_Tr; // 当前帧Tr周期
    unsigned int B_FL_R0; // 当前帧采样起始时间
    unsigned int B_FL_Pw; // 当前帧脉冲宽度
    unsigned int B_FL_Rw; // 当前帧距离门宽度
    unsigned int B_UI_TrN; // 当前帧脉冲个数
    unsigned int B_UI_RN; // 当前帧距离门个数
    unsigned int B_UI_ChN; // 当前帧通道个数
    signed int  B_FL_Azim; // 当前帧方位角
    signed int  B_FL_Elev; // 当前帧俯仰角
    unsigned int bak5[15]; // 备份字
    unsigned int B_FL_Gather_FPGA_Temp; // 采集板FPGA内核温度
    signed int B_FL_Array_Temp; // 天线温度
    unsigned int B_FL_Work_Mode; // 工作模式
    unsigned int B_FL_Freq_Code; // 频率码
    signed int B_FL_Azi_H; // 方位角高字节
    signed int B_FL_Azi_L; // 方位角低字节
    signed int B_FL_Ele_H; // 俯仰角高字节
    signed int B_FL_Ele_L; // 俯仰角低字节
    unsigned int B_FL_LD1=0; // 信道模块时钟锁定信号
    unsigned int B_FL_LD2=0; // 信道模块本振锁定指示
    unsigned int B_UI_SF=0; // 信道模块自检信号
    unsigned int bak6[21]; // 备份字
};

/**
 * @brief 工作/待机模式结构体 -- 发给CPU
 */
struct workStandby
{
    unsigned int Work_Standby_Mode = 1; // 工作模式--00-待机模式 01工作
    double Lon_Coordinates; // 经度坐标
    double Lat_Coordinates; // 纬度坐标
    double Altitude;        // 高度
    double Course_Angle;    // 航向角
    double Pitch_Angle;     // 俯仰角
    double Roll_Angle;      // 横滚角
    double Antenna_azi1;    // 天线法线指向方位角
    double Antenna_ele1;    // 天线法线指向俯仰角
};

/**
 * @brief 跟踪目标信息结构体
 */
struct Track_information {
    unsigned int   trackTargetID;    // 目标识别号
    int            trackwave_row;    // 波位行
    int            trackwave_col;    // 波位列
    int            trackwave;        // 波位
    double         trackAzimuth;     // 目标方位角（单位：弧度）
    double         trackElevation;   // 目标俯仰角（单位：弧度）
    unsigned int   track_flag;       // 追踪标志位：0-未追踪 1-正在追踪 2-追踪完成
};

/**
 * @brief 控制命令状态结构体 -- 记录界面上控制控件的状态
 */
struct Control_Command_Status {
    unsigned int Ant_mode; // 工作模式当前状态
    unsigned int Exc_signal; // 激励信号当前状态
    unsigned int freq_ctrl; // 频率切换当前状态
    unsigned int wave_ctrl; // 波形控制当前状态
    unsigned int rece_gain_ctrl; // 接收增益控制当前状态
    unsigned int power_mode; // 远程断电当前状态
    unsigned int STC_mode; // STC模式当前状态
    unsigned int STC_switch; // STC电路开关当前状态
    unsigned int Ant_Idx; // 天线索引号
    unsigned int RF_reset; // 天线与射频系统复位当前状态
    unsigned int track_num; // 跟踪波位数量当前状态
    unsigned int Beam_Azim_Ctrl; // 波束指向控制方位角当前状态
    unsigned int Beam_Elev_Ctrl; // 波束指向控制俯仰角当前状态
    bool initialized = false;
};

#pragma pack(pop) // 恢复之前的对齐
