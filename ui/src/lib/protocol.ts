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

/** @brief 历史视图使用的只读航迹点；归档 HTTP 落地前不会由网络填充。 */
export interface TrackHistoryPoint {
  taskId: string
  deviceId: string
  /** 协议阵面索引；旧归档尚未提供时不允许猜测安装姿态。 */
  faceIndex?: number
  tsMs: number
  sceneNum: number
  targetId: number
  range: number
  azimuth: number
  elevation: number
  radialSpeed: number
  altitude: number
  x: number
  y: number
  z: number
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

export type WsMessage = WsStatusMessage | WsTracksMessage | WsLogMessage | WsDeviceStateMessage

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
  altitude: number         // 目标高度 (m)
  radialSpeed: number      // 径向速度 (m/s)
  amplitude: number        // 幅度 (dB)
  snr: number              // 信噪比 (dB)
  extrapolateFlag: number  // 外推标志 (0=实测, 1=外推)
  targetType: number       // 目标类型
  signalId: number         // 信号识别标识
  sceneNum: number         // 景幅
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
