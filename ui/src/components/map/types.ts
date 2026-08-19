/**
 * @file types.ts
 * @brief 定义地图层消费的雷达、阵面和航迹视图模型。
 * @details 这些类型面向绘制而非网络传输；API 数据应先在上层转换并明确经纬度、高度、角度的单位。
 */

export interface RadarFace {
  label: string               // 阵面标识: "A" / "B" / "C" / "D"
  boresightAzimuth: number    // 该阵面实测绝对朝向 (°), 0=North, CW
  scanAngle: number           // 该阵面扫描半角 (°), e.g. 45 for ±45°
  active: boolean             // 是否开启扫描
}

export type RadarBrand = 'YHF' | 'YF' | 'BW'

export const BRAND_LABELS: Record<RadarBrand, string> = {
  YHF: '云禾峰',
  YF: '耀峰',
  BW: '宝威',
}

export const BRAND_OPTIONS: RadarBrand[] = ['YHF', 'YF', 'BW']

export interface RadarDevice {
  id: string
  name: string
  brand: RadarBrand
  lng: number
  lat: number
  altitude: number
  pitch: number                 // 整机俯仰角 (°), 抬头为正
  roll: number                  // 整机横滚角 (°), 右倾为正
  maxRange: number            // km
  faces: RadarFace[]          // 阵面列表
  endpointId?: string         // 关联的后端通信端点 id
  backendManaged?: boolean    // 是否来自后端设备台账
}

export interface SubDevice {
  id: string
  name: string
  kind: 'opto' | 'level' | 'gps'
  brand: RadarBrand
  linkedRadarId?: string
  mountOffsetAzimuth?: number
  coordinateSystem?: string
}

export type AppDevice = RadarDevice | SubDevice

export const RADAR_CENTER: [number, number] = [118.653090, 31.935020]

export const DEFAULT_DEVICE: Omit<RadarDevice, 'id'> = {
  name: 'NODE_ALPHA_01',
  brand: 'YHF',
  lng: 118.653090,
  lat: 31.935020,
  altitude: 0,
  pitch: 0,
  roll: 0,
  maxRange: 5,
  faces: [
    { label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true },
  ],
  endpointId: 'radar_cpu',
}
