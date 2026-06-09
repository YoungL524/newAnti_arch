export interface RadarFace {
  label: string               // 阵面标识: "A" / "B" / "C" / "D"
  boresightAzimuth: number    // 该阵面朝向 (°), 0=North, CW
  scanAngle: number           // 该阵面扫描半角 (°), e.g. 45 for ±45°
  active: boolean             // 是否开启扫描
}

export type RadarBrand = 'cetc38' | 'cetc14' | 'leiying'

export const BRAND_LABELS: Record<RadarBrand, string> = {
  cetc38: '中电38所',
  cetc14: '中电14所',
  leiying: '雷鹰',
}

export interface RadarDevice {
  id: string
  name: string
  brand: RadarBrand
  lng: number
  lat: number
  altitude: number
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
  brand: 'cetc38',
  lng: 118.653090,
  lat: 31.935020,
  altitude: 0,
  maxRange: 5,
  faces: [
    { label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true },
  ],
  endpointId: 'radar_cpu',
}
