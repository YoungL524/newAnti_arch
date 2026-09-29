/**
 * @file taskTypes.ts
 * @brief 定义任务实体、生命周期状态和设备绑定的前端类型。
 * @details 状态值必须与核心 TaskService 状态机一致；界面不得自行构造核心不接受的状态迁移。
 */

export type TaskStatus = 'draft' | 'ready' | 'active' | 'faulted' | 'completed' | 'archived'

/** 任务直接使用的雷达整机形态，用于从设备台账筛选兼容设备。 */
export type TaskRadarArrayType = 'singleFace' | 'fourFace'

/** 后端冻结的通用控制参数；数值字段保存原始控制字。 */
export interface RadarCommonSettings {
  /** 组合工作模式控制字。 */ combinationMode: number
  /** 帧工作模式控制字。 */ frameWorkMode: number
  /** 系统工作模式控制字。 */ systemWorkMode: number
  /** 距离模式控制字。 */ rangeMode: number
  /** STC 功能是否开启。 */ stcEnabled: boolean
  /** STC 模式十六进制控制字。 */ stcMode: number
  /** 激励是否关闭。 */ excitationDisabled: boolean
  /** CFAR 模式十六进制控制字。 */ cfarMode: number
  /** CFAR 一级门限原始控制字。 */ cfarThreshold1: number
  /** CFAR 二级门限原始控制字。 */ cfarThreshold2: number
  /** 速度门限原始控制字。 */ velocityThreshold: number
  /** AD 数据记录开关。 */ adRecordEnabled: boolean
  /** 点数据记录开关。 */ pointRecordEnabled: boolean
  /** 航迹起始点数。 */ trackInitiationPointCount: number
  /** 聚类数据记录开关。 */ clusterRecordEnabled: boolean
  /** 关联数据记录开关。 */ associationRecordEnabled: boolean
}

/** 后端冻结的帧控制参数；角度单位为度，其余Word字段为原始控制字。 */
export interface RadarFrameSettings {
  /** 屏显波形模式控制字。 */ screenWaveMode: number
  /** 屏显航迹开关。 */ screenTrackEnabled: boolean
  /** 波束方位角，单位度。 */ beamAzimuthDeg: number
  /** 波束俯仰角，单位度。 */ beamElevationDeg: number
  /** 带宽模式控制字。 */ bandwidthMode: number
  /** BIT 自检开关。 */ bitEnabled: boolean
  /** 波形编号控制字。 */ waveformCode: number
  /** 调制模式控制字。 */ modulationMode: number
  /** 调制带宽原始控制字。 */ modulationBandwidthWord: number
  /** 脉宽原始控制字。 */ pulseWidthWord: number
  /** 重复周期原始控制字。 */ repetitionPeriodWord: number
  /** 帧等待脉冲数量。 */ frameWaitPulseCount: number
  /** 有效脉冲数量。 */ effectivePulseCount: number
  /** 采样起始位置原始控制字。 */ sampleStartWord: number
  /** 采样结束位置或长度原始控制字。 */ sampleEndOrLengthWord: number
  /** 自定义波形写入开关。 */ waveformWriteEnabled: boolean
  /** 自定义波形写入位置。 */ waveformWritePosition: number
}

/** 一个物理阵面的运行参数；安装姿态仍属于设备台账。 */
export interface RadarFaceSettings {
  /** 频段控制字。 */ frequencyBand: number
  /** 频点序号。 */ frequencyIndex: number
  /** 射频衰减，单位 dB。 */ rfAttenuationDb: number
  /** 中频衰减，单位 dB。 */ ifAttenuationDb: number
  /** 本阵面的天线模式控制字。 */ antennaMode: number
  /** 风扇一供电开关。 */ fan1Powered: boolean
  /** 风扇二供电开关。 */ fan2Powered: boolean
  /** TR 组件供电开关。 */ trModulePowered: boolean
}

/** 模板、任务快照和后续控制Owner共用的完整参数集合。 */
export interface RadarControlSettings {
  /** 四个阵面共用的系统控制参数。 */
  common: RadarCommonSettings
  /** 四个阵面共用的帧控制参数。 */
  frame: RadarFrameSettings
  /** 按物理1#～4#排列的阵面参数。 */
  faces: [RadarFaceSettings, RadarFaceSettings, RadarFaceSettings, RadarFaceSettings]
}

/** 任务创建时可选择的只读参数模板。 */
export interface TaskTemplate {
  /** 模板跨重启稳定UUID。 */
  templateId: string
  /** 用户可见模板名称。 */
  name: string
  /** 后端持久化并校验的权威参数值。 */
  controlSettings: RadarControlSettings
}

export interface Task {
  id: string
  name: string
  description: string
  operator: string
  status: TaskStatus
  /** 本次任务要求的雷达整机形态。 */
  radarArrayType: TaskRadarArrayType
  deviceIds: string[]
  /** 每台雷达本次任务启用的阵面位图；bit0～bit3对应1#～4#。 */
  deviceFaceMasks: Record<string, number>
  /** 创建任务时选用的模板，仅用于来源追溯。 */
  sourceTemplateId: string
  /** 创建时复制出的任务独立控制参数快照。 */
  controlSettings: RadarControlSettings
  geofence?: { lng: number; lat: number }[]
  alertRule?: string
  createdAt: number
  activatedAt?: number
  completedAt?: number
  archivedAt?: number
}
