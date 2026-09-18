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

/** @brief 表示设备revision内嵌的一个UDP目标地址。 */
export interface RadarEndpointTarget { address: string; port: number }

/** @brief 表示随设备台账原子保存的主链通信端点。 */
export interface RadarEndpoint {
  id: string
  protocol: 'udp' | 'tcp'
  localAddress: string
  localPort: number
  description: string
  targets: Record<string, RadarEndpointTarget>
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
  revision: number
  name: string
  brand: RadarBrand
  model: string
  protocolAdapterKey: string
  lng: number
  lat: number
  altitude: number
  pitch: number                 // 整机俯仰角 (°), 抬头为正
  roll: number                  // 整机横滚角 (°), 右倾为正
  heading: number               // 整机真北顺时针绝对航向 (°)
  maxRange: number            // km
  faces: RadarFace[]          // 阵面列表
  endpointId?: string         // 关联的后端通信端点 id
  endpoint: RadarEndpoint     // 与本设备revision原子保存的内部UDP端点
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

export const RADAR_CENTER: [number, number] = [118.65303, 31.93494]

export const DEFAULT_DEVICE: Omit<RadarDevice, 'id'> = {
  revision: 0,
  name: 'NODE_ALPHA_01',
  brand: 'YHF',
  model: 'YHF-YZ-KU',
  protocolAdapterKey: 'dcmp.dp.v1',
  lng: 118.65303,
  lat: 31.93494,
  altitude: 63.9288,
  pitch: 0,
  roll: 0,
  heading: 0,
  maxRange: 5,
  faces: [
    { label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true },
  ],
  endpointId: 'radar_cpu',
  endpoint: {
    id: 'radar_cpu', protocol: 'udp', localAddress: '127.0.0.1', localPort: 7788,
    description: 'DCMP内部DP/SP通信', targets: {
      dph: { address: '127.0.0.1', port: 3456 },
      workmode: { address: '127.0.0.1', port: 1234 },
    },
  },
}
