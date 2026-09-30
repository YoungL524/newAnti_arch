/**
 * @file types.ts
 * @brief 定义地图层消费的雷达、阵面和航迹视图模型。
 * @details 这些类型面向绘制而非网络传输；API 数据应先在上层转换并明确经纬度、高度、角度的单位。
 */

export interface RadarFace {
  /** 阵面显示标识，依次为1#、2#、3#、4#。 */
  label: string
  /** 该阵面实测绝对航向，单位度；正北为0且顺时针为正。 */
  heading: number
  /** 该阵面安装俯仰角，单位度；抬头为正。 */
  pitch: number
  /** 该阵面安装横滚角，单位度；右侧下沉为正。 */
  roll: number
  /** 该阵面扫描半角，单位度；45表示覆盖正负45度。 */
  scanAngle: number
  /** 表示该阵面是否参与当前设备配置。 */
  active: boolean
}

/** @brief 表示设备revision内嵌的一个UDP目标地址。 */
export interface RadarEndpointTarget { address: string; port: number }

/** @brief 预留的航迹外部转发目标；只保存配置，不启动转发。 */
export interface RadarExternalForward {
  /** 未来转发传输协议；当前仅用于保存配置。 */
  protocol: 'udp' | 'tcp' | 'http'
  /** 外部上位机IP地址；空字符串表示尚未配置。 */
  address: string
  /** 目标端口；未配置时为0，已配置时必须在1～65535。 */
  port: number
}

/** @brief 表示随设备台账原子保存的主链通信端点。 */
export interface RadarEndpoint {
  id: string
  /** 当前内部通信只支持UDP。 */
  protocol: 'udp'
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
  /** 台账显示最大距离，单位千米。 */
  maxRange: number
  /** 随设备revision原子保存的逐阵面安装参数。 */
  faces: RadarFace[]
  /** 关联的后端通信端点稳定键。 */
  endpointId?: string
  /** 与本设备revision原子保存的内部UDP端点。 */
  endpoint: RadarEndpoint
  /** 与设备revision一起保存的外部航迹转发断点；当前不发送数据。 */
  externalForward?: RadarExternalForward
  /** 表示该对象是否来自后端权威设备台账。 */
  backendManaged?: boolean
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
  protocolAdapterKey: '20260915',
  lng: 118.65303,
  lat: 31.93494,
  altitude: 63.9288,
  maxRange: 5,
  faces: [
    { label: '1#', heading: 0, pitch: 0, roll: 0, scanAngle: 45, active: true },
  ],
  endpointId: 'radar_cpu',
  endpoint: {
    id: 'radar_cpu', protocol: 'udp', localAddress: '127.0.0.1', localPort: 7788,
    description: 'DCMP内部DP/SP通信', targets: {
      dph: { address: '127.0.0.1', port: 3456 },
      workmode: { address: '127.0.0.1', port: 1234 },
    },
  },
  externalForward: { protocol: 'udp', address: '', port: 0 },
}
