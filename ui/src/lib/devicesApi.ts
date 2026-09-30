/**
 * @file devicesApi.ts
 * @brief 封装设备台账的读取、创建、更新和删除接口。
 * @details 雷达经纬高、逐阵面安装姿态与端点绑定均按核心JSON契约传输，客户端不叠加整机姿态。
 */

export type DeviceRegistryStatus = 'loading' | 'available' | 'unavailable'

import type { RadarDevice, RadarExternalForward, SubDevice, RadarBrand } from '../components/map/types'

const BASE = '/api/v1/devices'

export interface ApiResult<T = boolean> {
  ok: boolean
  error?: string
  data?: T
}

export interface ApiDevice {
  deviceId: string; revision: string; displayName: string; manufacturer: string; model: string
  protocolAdapterKey: string; arrayType: 'singleFace' | 'fourFace'
  longitudeDeg: number; latitudeDeg: number; altitudeM: number; maxRangeM: number
  faces: Array<{ faceIndex: number; headingDeg: number; pitchDeg: number; rollDeg: number; scanHalfAngleDeg: number; enabled: boolean }>
  endpoints: Array<{ endpointKey: string; role: string; transport: string; localAddress: string; localPort: number; remoteAddress: string; remotePort: number }>
  protocolConfig: Record<string, unknown>
}

/** @brief 表示当前雷达协议配置中两个具名 UDP 发送目标。 */
interface DcmpCommunicationConfig {
  description?: string
  targets?: Record<string, { address: string; port: number }>
}

/** @brief 设备协议配置中尚未启用的外部航迹转发断点。 */
interface DcmpProtocolConfig {
  communication?: DcmpCommunicationConfig
  externalForward?: RadarExternalForward
}

const DEFAULT_DCMP_TARGETS = {
  dph: { address: '127.0.0.1', port: 3456 },
  workmode: { address: '127.0.0.1', port: 1234 },
}

/**
 * @brief 从设备revision读取内部发送目标与外部转发断点；缺失时由调用方补默认值。
 * @param item 后端返回的完整设备revision。
 * @return 设备协议配置对象；调用方按需复制表单字段。
 */
function communicationConfig(item: ApiDevice): DcmpProtocolConfig {
  return item.protocolConfig && typeof item.protocolConfig === 'object'
    ? item.protocolConfig as DcmpProtocolConfig : {}
}

export interface CreateDeviceResponse {
  device: ApiDevice
  migratedDeviceIds?: string[]
}

export interface UpdateDeviceResponse {
  device?: ApiDevice
  migratedDeviceIds?: string[]
}

const LEGACY_BRANDS: Record<string, RadarBrand> = {
  cetc38: 'YHF',
  cetc14: 'YF',
  leiying: 'BW',
}

function normalizeBrand(raw: string): RadarBrand {
  if (raw in LEGACY_BRANDS) return LEGACY_BRANDS[raw]
  if (raw === 'YHF' || raw === 'YF' || raw === 'BW') return raw
  return 'YHF'
}

/**
 * @brief 调用设备台账 HTTP 接口并保留后端给出的校验或 revision 冲突原因。
 * @param url 同源设备接口路径。
 * @param options fetch 请求参数；省略时执行读取。
 * @return 网络、HTTP 与业务结果统一后的值对象。
 */
async function apiCall(url: string, options?: RequestInit): Promise<ApiResult<any>> {
  try {
    const res = await fetch(url, options)
    const data = await res.json().catch(() => null)
    if (!res.ok) return { ok: false, error: data?.error?.message || `HTTP ${res.status}` }
    if (!data) return { ok: false, error: 'API 未返回 JSON' }
    if (!data.ok) return { ok: false, error: data.error?.message || 'API 返回失败' }
    return { ok: true, data: data.data }
  } catch (e: any) {
    return { ok: false, error: e?.message || '网络错误' }
  }
}

export function apiDeviceToRadar(item: ApiDevice): RadarDevice | null {
  const endpoint = item.endpoints?.[0]
  const protocolConfig = communicationConfig(item)
  const communication = protocolConfig.communication ?? {}
  const configuredTargets = communication.targets ?? {}
  const targets = Object.fromEntries(Object.entries(DEFAULT_DCMP_TARGETS).map(([key, fallback]) => {
    const configured = configuredTargets[key]
    return [key, configured && configured.port > 0
      ? { address: configured.address, port: configured.port } : { ...fallback }]
  }))
  return {
    id: item.deviceId, revision: Number(item.revision),
    name: item.displayName,
    brand: normalizeBrand(item.manufacturer || 'YHF'),
    model: item.model,
    protocolAdapterKey: item.protocolAdapterKey === 'dcmp.dp.v1' ? '20260915' : item.protocolAdapterKey,
    lng: item.longitudeDeg,
    lat: item.latitudeDeg,
    altitude: item.altitudeM,
    maxRange: item.maxRangeM / 1000,
    faces: item.faces.map(face => ({ label: `${face.faceIndex + 1}#`,
      heading: face.headingDeg, pitch: face.pitchDeg, roll: face.rollDeg,
      scanAngle: face.scanHalfAngleDeg, active: face.enabled })),
    endpointId: endpoint?.endpointKey,
    endpoint: {
      id: endpoint?.endpointKey ?? 'radar_cpu', protocol: 'udp',
      localAddress: endpoint?.localAddress ?? '127.0.0.1', localPort: endpoint?.localPort ?? 7788,
      description: communication.description ?? 'DCMP内部DP/SP通信', targets,
    },
    externalForward: protocolConfig.externalForward ?? { protocol: 'udp', address: '', port: 0 },
    backendManaged: true,
  }
}

export function apiDeviceToSub(item: ApiDevice): SubDevice | null {
  void item
  return null
}

export async function fetchDevices(): Promise<{ radars: RadarDevice[]; subs: SubDevice[] } & ApiResult> {
  const result = await apiCall(BASE)
  const radars: RadarDevice[] = []
  const subs: SubDevice[] = []
  if (result.ok && result.data?.devices) {
    for (const item of result.data.devices) {
      const r = apiDeviceToRadar(item)
      if (r) radars.push(r)
      const s = apiDeviceToSub(item)
      if (s) subs.push(s)
    }
  }
  return { ...result, radars, subs }
}

export function radarToApiPayload(id: string, d: RadarDevice): any {
  return {
    deviceId: id || undefined, expectedRevision: d.revision > 0 ? String(d.revision) : undefined,
    displayName: d.name, manufacturer: d.brand, model: d.model,
    protocolAdapterKey: d.protocolAdapterKey,
    arrayType: d.faces.length === 4 ? 'fourFace' : 'singleFace',
    longitudeDeg: d.lng, latitudeDeg: d.lat, altitudeM: d.altitude,
    maxRangeM: d.maxRange * 1000,
    faces: d.faces.map((face, faceIndex) => ({ faceIndex,
      headingDeg: face.heading, pitchDeg: face.pitch, rollDeg: face.roll,
      scanHalfAngleDeg: face.scanAngle,
      enabled: face.active })),
    endpoints: [{ endpointKey: d.endpoint.id, role: 'track_receive', transport: d.endpoint.protocol,
      localAddress: d.endpoint.localAddress, localPort: d.endpoint.localPort,
      remoteAddress: '', remotePort: 0 }],
    protocolConfig: { communication: {
      description: d.endpoint.description,
      targets: d.endpoint.targets,
    }, externalForward: d.externalForward ?? { protocol: 'udp', address: '', port: 0 } },
    changedBy: 'UI', changeReason: id ? 'UI修改设备' : 'UI创建设备',
  }
}

export function subToApiPayload(id: string, d: SubDevice): any {
  const kindMap = { opto: 1, level: 2, gps: 3 }
  return {
    id, name: d.name, brand: d.brand, kind: kindMap[d.kind] ?? 0,
    linkedRadarId: d.linkedRadarId, mountOffsetAzimuth: d.mountOffsetAzimuth,
    coordinateSystem: d.coordinateSystem,
  }
}

export async function createDevice(payload: any): Promise<ApiResult<CreateDeviceResponse>> {
  return apiCall(BASE, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
}

export async function updateDevice(id: string, payload: any): Promise<ApiResult<UpdateDeviceResponse>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
}

export async function deleteDeviceApi(id: string, revision: number): Promise<ApiResult> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'DELETE', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ expectedRevision: String(revision), changedBy: 'UI', changeReason: 'UI删除设备' }),
  })
}
