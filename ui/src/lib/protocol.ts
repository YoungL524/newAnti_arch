/**
 * @file protocol.ts
 * @brief 定义浏览器与核心 HTTP/WebSocket 接口共享的数据契约。
 * @details 字段名称、可选性和单位必须与 C++ JSON 序列化保持一致；本文件不承担网络请求或状态管理。
 */

/** Protocol types matching C++ HttpServer API */

export interface LogEntry {
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

export type WsMessage = WsStatusMessage | WsTracksMessage | WsLogMessage

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
  deviceId?: string
  trackId?: number
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
  trackId: number
  deviceId: string
  taskId: string
  timestamp: number
  range: number
  azimuth: number          // rad
  elevation: number        // rad
  altitude: number
  x?: number
  y?: number
  z?: number
  radialSpeed: number
  rcs?: number
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
  deviceId: string
  trackId: number
  pointIndex: number
}

export interface WsTracksSnapshotMessage {
  type: 'tracks'
  mode: 'snapshot'
  revision: number
  timestamp: number
  tracks: WsTrackDisplayPoint[]
}

export interface WsTracksDeltaMessage {
  type: 'tracks'
  mode: 'delta'
  revision: number
  timestamp: number
  upserts: WsTrackDisplayPoint[]
  removals: WsTrackDisplayPointKey[]
}

export type WsTracksMessage = WsTracksSnapshotMessage | WsTracksDeltaMessage
