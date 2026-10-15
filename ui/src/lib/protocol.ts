/**
 * @file protocol.ts
 * @brief 定义浏览器与核心 HTTP/WebSocket 接口共享的数据契约。
 * @details 字段名称、可选性和单位必须与 C++ JSON 序列化保持一致；本文件不承担网络请求或状态管理。
 */

/** @brief 以下类型与核心服务公开的 HTTP/WebSocket JSON 字段逐项对应。 */

export interface LogEntry {
  /** EventTopic 的进程内序号，用于 WebSocket 重连补偿时去重。 */
  sequence?: string
  level: string
  message: string
  timestamp: number
  taskId?: string
  deviceId?: string
}

export interface DeviceStatus {
  id: string
  state: string       // Standby | Starting | Working | Stopping | Fault
  acquisition: boolean
}

export interface LogsResponse {
  ok: boolean
  logs: LogEntry[]
}

/** @brief 当前有界回放页的最终绘制点；地理位置按采集时设备 revision 计算，多个视图共用，不进入实时缓存。 */
export interface TrackHistoryPoint {
  taskId: string ///< 稳定任务 UUID。
  deviceId: string ///< 稳定设备 UUID。
  acquisitionId: string ///< 稳定采集会话 UUID，厂家航迹号不得跨会话连线。
  observationId: string ///< 数据库观测主键十进制字符串，不是 Topic sequence。
  faceIndex: number ///< 0～3 物理阵面，不猜测缺失姿态。
  tsMs: number ///< 接收 Unix epoch UTC 毫秒。
  targetId: number ///< uint32 厂家航迹号。
  range: number ///< 斜距，米。
  azimuth: number ///< RFU 局部方位，弧度。
  elevation: number ///< RFU 局部俯仰，弧度。
  radialSpeed: number ///< 径向速度，米每秒。
  amplitude: number ///< 厂家幅值原值。
  packFlag: 0 | 1 ///< 0 消亡、1 存在。
  scanSequence?: number ///< 仅真实归档扫描号存在时提供，不制造 sceneNum。
  longitude: number ///< 按归档安装配置重建的经度，度。
  latitude: number ///< 按归档安装配置重建的纬度，度。
  altitude: number ///< 安装原点 MSL 海拔加旋转后目标高差，米。
}

/** @brief 历史视图使用的只读系统事件；归档 HTTP 落地前不会由网络填充。 */
export interface SystemEvent {
  tsMs: number
  category: string
  source: string
  detail: string
  taskId?: string
  deviceId?: string
}

export interface StatusResponse {
  ok: boolean
  acquisition: boolean
  state: string
  connected: boolean
  uptime: number
  endpoints: number
  dbConnected: boolean
  devices: DeviceStatus[]
}

export interface WsStatusMessage {
  type: 'status'
  acquisition: boolean
  state: string
  connected: boolean
  upTime: number
  devices: DeviceStatus[]
}

export interface WsLogMessage extends LogEntry {
  type: 'log'
}

export type WsMessage = WsStatusMessage | WsTracksMessage | WsLogMessage | WsDeviceStateMessage | WsSystemStateMessage | WsTaskStateMessage

/** @brief 唯一活动任务的完整实时快照；稳定身份关联台账，不包含设备数组或第二份任务配置。 */
export interface WsTaskStateMessage {
  type: 'taskState' ///< 独立实时流，不能与设备或系统 revision 比较。
  sentUtcMs: number ///< Web 序列化的 Unix epoch UTC 毫秒。
  revision: string ///< 非零 uint64 十进制修订号，重连后重新接受。
  payload: { phase: 'none'; taskId?: never }
    | { phase: 'ready' | 'running' | 'completed' | 'faulted'; taskId: string } ///< None 无身份，其他阶段关联非零稳定 UUID。
}

/** @brief Core 系统状态完整网络快照；页面只保留最后一份，断线清除，独立于设备/任务状态。 */
export interface WsSystemStateMessage {
  type: 'systemState' ///< 独立消息类型，不作为通用状态路由。
  sentUtcMs: number ///< 序列化时的 Unix epoch UTC 毫秒。
  revision: string ///< 本运行实例 SystemState 的非零 uint64 十进制修订号。
  payload: {
    processStartedUtcMs: number ///< Runtime 首次形成状态的 UTC 毫秒，服务重启不修改。
    phase: 0 | 1 | 2 | 3 | 4 ///< Starting、Ready、Stopping、Stopped、Faulted。
    health: 0 | 1 | 2 ///< Healthy、Degraded、Faulted；由 Core 唯一 Owner 判定。
    readinessFlags: number ///< 数据库、配置、数据域、归档、Core Web、控制平面的低六位就绪标记。
    degradationFlags: number ///< 低六位降级原因；当前接入归档不可用、航迹缺口、事件缺口，后两者不会因写入恢复而清除。
  }
}

/** @brief 表示后端心跳 owner 已判定且V1网络合同允许的三态枚举。 */
export type HeartbeatStatus = 'unknownOrTimedOut' | 'online' | 'offlineOrLate'

/** @brief 表示DeviceState消息中与当前心跳联试有关的完整载荷。 */
export interface WsDeviceStatePayload {
  dpHeartbeat: HeartbeatStatus ///< DP心跳状态，由后端超时门禁判定。
  spHeartbeat: HeartbeatStatus ///< SP心跳状态，由后端超时门禁判定。
  fpOnlineMask: number ///< DP报告的FP在线位图，bit0～bit3对应1～4号阵面。
  fpDataMask: number ///< SP报告的FP数据位图，bit0～bit3对应1～4号阵面。
  radarPose?: RadarLivePose ///< 当前任务雷达的实时位姿；DP/阵面离线时缺省并回退台账。
  devices: DeviceStatus[] ///< 当前本地采集设备完整快照，稳定UUID与设备台账一致。
}

/** @brief DP 心跳在线阵面位置的平均值和各面独立姿态，仅用于实时显示。 */
export interface RadarLivePose {
  deviceId: string ///< 当前准备任务雷达的稳定 UUID，不凭页面当前选择猜测归属。
  longitudeDeg: number ///< 在线阵面经度平均值，东经为正，单位 °。
  latitudeDeg: number ///< 在线阵面纬度平均值，北纬为正，单位 °。
  altitudeM: number ///< 在线阵面 MSL 海拔平均值，单位 m。
  validFaceMask: number ///< 低四位有效阵面位；只覆盖对应位的姿态。
  faces: Array<{ faceIndex: number; headingDeg: number; pitchDeg: number; rollDeg: number }> ///< 当前有效面姿态，单位 °。
}

/**
 * @brief 表示 StateWebSocketService 输出的一次 DeviceState 完整心跳视图。
 * @details revision 使用十进制字符串避免 JavaScript 丢失 64 位整数精度；两个 mask 仅使用低四位。
 */
export interface WsDeviceStateMessage {
  type: 'deviceState' ///< 状态消息鉴别字段。
  sentUtcMs: number ///< WebReader序列化本快照的Unix epoch UTC毫秒。
  revision: string ///< LatestState 修订号的十进制字符串。
  payload: WsDeviceStatePayload ///< 本revision对应的完整DeviceState网络载荷。
}

// ── Device types ──────────────────────────────────────────

export enum DeviceType {
  UNKNOWN = 0,
  RADAR = 1,
  LEVEL_SENSOR = 100,
  OPTOELECTRONIC = 200,
  GPS = 300,
}

export const DEVICE_TYPE_LABELS: Record<DeviceType, string> = {
  [DeviceType.UNKNOWN]: '未知设备',
  [DeviceType.RADAR]: '雷达',
  [DeviceType.LEVEL_SENSOR]: '水平仪',
  [DeviceType.OPTOELECTRONIC]: '光电',
  [DeviceType.GPS]: 'GPS',
}

export const DEVICE_TYPE_OPTIONS: { value: string; label: string; deviceType: DeviceType }[] = [
  { value: 'type_radar', label: '雷达', deviceType: DeviceType.RADAR },
  { value: 'type_level', label: '水平仪', deviceType: DeviceType.LEVEL_SENSOR },
  { value: 'type_gps', label: 'GPS', deviceType: DeviceType.GPS },
  { value: 'type_opto', label: '光电', deviceType: DeviceType.OPTOELECTRONIC },
]

// ── Data row types ────────────────────────────────────────

export interface RadarTrack {
  batchId: number          // 批号
  acquisitionId?: string  ///< 当前采集会话稳定标识；实时尾迹不得跨会话合并。
  deviceId?: string
  trackId?: number
  faceIndex?: number
  slantRange: number       // 斜距 (m)
  azimuth: number          // 方位角 (°)
  elevation: number        // 俯仰角 (°)
  altitude?: number        ///< MSL 海拔米；实时未提供时省略，历史按安装配置重建。
  radialSpeed: number      // 径向速度 (m/s)
  amplitude: number        ///< 厂家未标定幅值原值，不标注为 dB。
  snr?: number             ///< 信噪比 dB；没有真实归档值时省略。
  extrapolateFlag?: number ///< 外推标志；消亡不等同外推，缺失时省略。
  targetType: number       ///< 识别类别；0 为 Unknown，不使用厂家航迹号冒充类别。
  signalId?: number        ///< 真实信号 ID，历史未归档时省略。
  sceneNum?: number        ///< 真实景幅；扫描序号不能冒充景幅。
  timestamp: number
  pointIndex?: number
  compacted?: boolean     ///< true 表示旧路径形状点，只绘制线路，不绘制近期观测点样式。
  style?: {
    color: string
    size: number
    opacity: number
  }
  interaction?: {
    selected: boolean
    trackingEnabled: boolean
  }
}

export interface LevelSensorData {
  pitch: number     // 俯仰角 (°)
  roll: number      // 横滚角 (°)
  heading: number   // 航向角 (°)
  timestamp: number
}

export interface OptoelectronicData {
  turntableAzimuth: number    // 转台方位 (°)
  turntableElevation: number  // 转台俯仰 (°)
  targetAzimuth: number       // 目标方位 (°)
  targetElevation: number     // 目标俯仰 (°)
  workStatus: 'idle' | 'searching' | 'tracking'  // 工作状态
  timestamp: number
}

export interface GpsData {
  longitude: number     // 经度
  latitude: number      // 纬度
  altitude: number      // 海拔高度 (m)
  coordinateSystem: string  // 坐标系, e.g. 'WGS-84'
  timestamp: number
}

export type DeviceDataRow = RadarTrack | LevelSensorData | OptoelectronicData | GpsData

// ── WebSocket extension ───────────────────────────────────

export interface WsTrackDisplayPoint {
  acquisitionId: string
  trackId: number
  deviceId: string
  faceIndex: number
  receiveUtcMs: number
  rangeM: number
  azimuthRad: number
  elevationRad: number
  radialVelocityMps: number
  amplitude: number
  pointIndex: number
  style?: {
    color: string
    size: number
    opacity: number
  }
  interaction?: {
    selected: boolean
    trackingEnabled: boolean
  }
}

export interface WsTrackDisplayPointKey {
  acquisitionId: string
  deviceId: string
  faceIndex: number
  trackId: number
  pointIndex: number
}

export interface WsTracksSnapshotMessage {
  type: 'trackSnapshot'
  sentUtcMs: number
  revision: string
  payload: {
    tracks: WsTrackDisplayPoint[]
  }
}

export interface WsTracksDeltaMessage {
  type: 'trackDelta'
  sentUtcMs: number
  revision: string
  payload: {
    upserts: WsTrackDisplayPoint[]
    removals: WsTrackDisplayPointKey[]
    retirements?: WsTrackDisplayPointKey[] ///< packFlag=0 的目标键；清除对应整条旧路径。
  }
}

export type WsTracksMessage = WsTracksSnapshotMessage | WsTracksDeltaMessage
