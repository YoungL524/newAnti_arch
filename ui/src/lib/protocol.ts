/** Protocol types matching C++ HttpServer API */

export interface DeviceStatus {
  id: string
  state: string       // Standby | Starting | Working | Stopping | Fault
  acquisition: boolean
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

export type WsMessage = WsStatusMessage | WsTracksMessage

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

export interface WsTrackItem {
  targetId: number
  range: number
  azimuth: number        // rad
  elevation: number      // rad
  radialSpeed: number
  rcs: number
  altitude: number
  x: number; y: number; z: number
  vx: number; vy: number; vz: number
  timestamp: number
}

export interface WsTracksMessage {
  type: 'tracks'
  deviceType: number
  track: WsTrackItem       // 单条航迹（每 UDP 包一个 GeneralPackage）
}
