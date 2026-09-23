/**
 * @file RadarControlSettings.h
 * @brief 定义任务模板、任务快照和后续控制Owner共用的雷达运行参数值类型。
 * @details 上游是模板与任务HTTP表单，下游仅允许TaskService持久化和后续DeviceController读取；本类型不含
 * 公共报文头、协议备份字、设备经纬高或阵面安装姿态，也不负责发送、定时器和运行状态。
 */

#pragma once

#include <QtGlobal>

#include <array>

namespace core::task {

/** @brief 保存不随阵面变化的通用控制参数，字段值均为已冻结的协议控制字语义。 */
struct RadarCommonSettings
{
    quint32 combinationMode = 0x00U; ///< 阵面组合模式：0x00常规、0x01合成。
    quint32 frameWorkMode = 0x00U; ///< 帧工作模式：0x00景幅连续、0x02单帧测试。
    quint32 systemWorkMode = 0x00U; ///< 系统主模式，合同允许0x00～0x04。
    quint32 rangeMode = 0x01U; ///< 探测量程：0x00为2 km、0x01为5 km。
    bool stcEnabled = false; ///< STC电路是否开启。
    quint32 stcMode = 0x00U; ///< STC方案：0x00、0x01、0x10或0x11。
    bool excitationDisabled = false; ///< true表示关闭激励信号。
    qint32 cfarMode = 0x00; ///< CFAR方案：0x00或0x01。
    qint32 cfarThreshold1 = 0; ///< CFAR门限1原始有符号控制字。
    qint32 cfarThreshold2 = 0; ///< CFAR门限2原始有符号控制字。
    qint32 velocityThreshold = 0; ///< 速度门限原始有符号控制字。
    bool adRecordEnabled = false; ///< 是否记录AD数据。
    bool pointRecordEnabled = false; ///< 是否记录DP内部点迹数据；不代表PointTopic。
    quint32 trackInitiationPointCount = 0U; ///< 起批点数。
    bool clusterRecordEnabled = false; ///< 是否记录凝聚数据。
    bool associationRecordEnabled = false; ///< 是否记录关联数据。
};

/** @brief 保存连续模式和单帧测试共用的帧控制参数。 */
struct RadarFrameSettings
{
    quint32 screenWaveMode = 0x00U; ///< 景幅波形方案原始控制字。
    bool screenTrackEnabled = false; ///< 景幅跟踪是否启用。
    double beamAzimuthDeg = 0.0; ///< 单帧测试波束方位角，单位度，范围[-45,45]。
    double beamElevationDeg = 0.0; ///< 单帧测试波束俯仰角，单位度，范围[-45,45]。
    quint32 bandwidthMode = 0x00U; ///< 中频带宽方案：0x00、0x01或0x02。
    bool bitEnabled = false; ///< FPGA BIT是否开启。
    quint32 waveformCode = 0x11U; ///< 波形模式字；0xaa表示自定义波形。
    quint32 modulationMode = 0x00U; ///< 调制形式原始控制字。
    quint32 modulationBandwidthWord = 0U; ///< FPGA定义的调制带宽原始字。
    quint32 pulseWidthWord = 0U; ///< FPGA定义的脉宽原始字。
    quint32 repetitionPeriodWord = 0U; ///< FPGA定义的重复周期原始字。
    quint32 frameWaitPulseCount = 0U; ///< 帧间等待脉冲数。
    quint32 effectivePulseCount = 0U; ///< 有效脉冲数。
    quint32 sampleStartWord = 0U; ///< 采样起点原始字。
    quint32 sampleEndOrLengthWord = 0U; ///< 采样终点或长度原始字。
    bool waveformWriteEnabled = false; ///< 是否把自定义波形写入指定模式位。
    quint32 waveformWritePosition = 0x11U; ///< 波形写入位置，合同范围0x11～0x99。
};

/** @brief 保存一个物理阵面的可编辑运行参数，不包含台账安装姿态。 */
struct RadarFaceSettings
{
    quint32 frequencyBand = 0x01U; ///< 工作频段：0x00为X、0x01为Ku。
    quint32 frequencyIndex = 0U; ///< 频点编号，范围0～39。
    quint32 rfAttenuationDb = 0U; ///< 射频衰减，范围0～31 dB。
    quint32 ifAttenuationDb = 0U; ///< 中频衰减，范围0～31 dB。
    quint32 antennaMode = 0x00U; ///< 天线模式：0x00、0x01、0x10或0x11。
    bool fan1Powered = true; ///< 风扇1是否上电；协议发送时转换为反逻辑位。
    bool fan2Powered = true; ///< 风扇2是否上电；协议发送时转换为反逻辑位。
    bool trModulePowered = true; ///< TR组件是否上电；协议发送时转换为反逻辑位。
};

/**
 * @brief 表示一份完整、可原子替换的雷达控制参数。
 * @note 四个阵面参数始终完整保存；任务faceMask只决定发送时哪些阵面生效，不删除未启用阵面的模板值。
 */
struct RadarControlSettings
{
    RadarCommonSettings common; ///< 通用控制参数。
    RadarFrameSettings frame; ///< 帧控制参数。
    std::array<RadarFaceSettings, 4> faces {}; ///< 1#～4#阵面的独立运行参数。
};

} // namespace core::task
