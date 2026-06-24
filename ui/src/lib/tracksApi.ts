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

export async function fetchTrackHistory(
  params: FetchTrackHistoryParams,
): Promise<FetchTrackHistoryResult> {
  const qs = new URLSearchParams()
  qs.set('taskId', params.taskId)
  if (params.deviceId) qs.set('deviceId', params.deviceId)
  if (params.startMs != null) qs.set('startMs', String(params.startMs))
  if (params.endMs != null) qs.set('endMs', String(params.endMs))
  if (params.limit != null) qs.set('limit', String(params.limit))
  if (params.order) qs.set('order', params.order)

  try {
    const res = await fetch(`${BASE}?${qs.toString()}`)
    if (!res.ok) return { ok: false, error: `HTTP ${res.status}`, tracks: [] }
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || 'API 返回失败', tracks: [] }
    return { ok: true, tracks: (data.tracks ?? []) as TrackHistoryPoint[] }
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : '网络错误'
    return { ok: false, error: msg, tracks: [] }
  }
}
