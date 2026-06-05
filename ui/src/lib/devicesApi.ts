import type { RadarDevice, SubDevice } from '../components/map/types'

const BASE = '/api/devices'

export interface ApiDevice {
  id: string; name: string; brand: string; kind: number
  lng: number; lat: number; altitude: number; maxRange: number
  facesJson: string; endpointId: string; linkedRadarId: string
  mountOffsetAzimuth: number; coordinateSystem: string; status: number
}

export function apiDeviceToRadar(item: ApiDevice): RadarDevice | null {
  if (item.kind !== 0) return null
  let faces: any[] = []
  try { faces = JSON.parse(item.facesJson || '[]') } catch {}
  return {
    id: item.id,
    name: item.name,
    brand: (item.brand || 'cetc38') as any,
    lng: item.lng,
    lat: item.lat,
    altitude: item.altitude,
    maxRange: item.maxRange || 5,
    faces: faces.length > 0 ? faces : [{ label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true }],
  }
}

export function apiDeviceToSub(item: ApiDevice): SubDevice | null {
  if (item.kind === 0) return null
  const kinds = ['opto', 'level', 'gps'] as const
  return {
    id: item.id,
    name: item.name,
    kind: kinds[item.kind - 1] ?? 'opto',
    brand: (item.brand || 'cetc38') as any,
    linkedRadarId: item.linkedRadarId || undefined,
    mountOffsetAzimuth: item.mountOffsetAzimuth,
    coordinateSystem: item.coordinateSystem || 'WGS-84',
  }
}

export async function fetchDevices(): Promise<{ radars: RadarDevice[]; subs: SubDevice[] }> {
  const res = await fetch(BASE)
  const data = await res.json()
  const radars: RadarDevice[] = []
  const subs: SubDevice[] = []
  if (data.ok && data.devices) {
    for (const item of data.devices) {
      const r = apiDeviceToRadar(item)
      if (r) radars.push(r)
      const s = apiDeviceToSub(item)
      if (s) subs.push(s)
    }
  }
  return { radars, subs }
}

export function radarToApiPayload(id: string, d: RadarDevice): any {
  return {
    id, name: d.name, brand: d.brand, kind: 0,
    lng: d.lng, lat: d.lat, altitude: d.altitude,
    maxRange: d.maxRange, facesJson: JSON.stringify(d.faces),
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

export async function createDevice(payload: any): Promise<boolean> {
  const res = await fetch(BASE, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
  const data = await res.json()
  if (!data.ok) console.warn('[devicesApi] create failed:', data.error)
  return !!data.ok
}

export async function updateDevice(id: string, payload: any): Promise<boolean> {
  const res = await fetch(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
  const data = await res.json()
  if (!data.ok) console.warn('[devicesApi] update failed:', data.error)
  return !!data.ok
}

export async function deleteDeviceApi(id: string): Promise<boolean> {
  const res = await fetch(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'DELETE',
  })
  const data = await res.json()
  if (!data.ok) console.warn('[devicesApi] delete failed:', data.error)
  return !!data.ok
}
