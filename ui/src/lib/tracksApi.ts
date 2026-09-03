/**
 * @file tracksApi.ts
 * @brief 封装按任务分页查询持久化历史航迹的 HTTP 接口。
 * @details 历史查询模型与实时 WebSocket 当前态分离；调用方必须显式提供任务和分页边界。
 */

export interface TrackHistoryPoint {
  taskId: string
  deviceId: string
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

export interface FetchTrackHistoryParams {
  taskId: string
  deviceId?: string
  startMs?: number
  endMs?: number
  limit?: number
  order?: 'asc' | 'desc'
}

export interface FetchTrackHistoryResult {
  ok: boolean
  error?: string
  tracks: TrackHistoryPoint[]
}

const BASE = '/api/tracks/history'

function num(v: unknown, fallback = 0): number {
  const n = typeof v === 'number' ? v : Number(v)
  return Number.isFinite(n) ? n : fallback
}

function str(v: unknown): string {
  return typeof v === 'string' ? v : v != null ? String(v) : ''
}

function normalizeTrackHistoryPoint(raw: unknown): TrackHistoryPoint | null {
  if (!raw || typeof raw !== 'object') return null
  const o = raw as Record<string, unknown>
  const taskId = str(o.taskId)
  const deviceId = str(o.deviceId)
  if (!taskId || !deviceId) return null
  return {
    taskId,
    deviceId,
    tsMs: num(o.tsMs),
    sceneNum: num(o.sceneNum),
    targetId: num(o.targetId),
    range: num(o.range),
    azimuth: num(o.azimuth),
    elevation: num(o.elevation),
    radialSpeed: num(o.radialSpeed),
    altitude: num(o.altitude),
    x: num(o.x),
    y: num(o.y),
    z: num(o.z),
  }
}

export async function fetchTrackHistory(
  params: FetchTrackHistoryParams,
): Promise<FetchTrackHistoryResult> {
  const taskId = params.taskId.trim()
  if (!taskId) {
    return { ok: false, error: 'taskId 不能为空', tracks: [] }
  }

  const qs = new URLSearchParams()
  qs.set('taskId', taskId)
  if (params.deviceId?.trim()) qs.set('deviceId', params.deviceId.trim())
  if (params.startMs != null) qs.set('startMs', String(params.startMs))
  if (params.endMs != null) qs.set('endMs', String(params.endMs))
  if (params.limit != null) qs.set('limit', String(params.limit))
  if (params.order) qs.set('order', params.order)

  try {
    const res = await fetch(`${BASE}?${qs.toString()}`)
    if (!res.ok) return { ok: false, error: `HTTP ${res.status}`, tracks: [] }
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || 'API 返回失败', tracks: [] }

    const tracks: TrackHistoryPoint[] = []
    for (const raw of data.tracks ?? []) {
      const pt = normalizeTrackHistoryPoint(raw)
      if (pt) tracks.push(pt)
    }
    return { ok: true, tracks }
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : '网络错误'
    return { ok: false, error: msg, tracks: [] }
  }
}

