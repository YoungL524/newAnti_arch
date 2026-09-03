/**
 * @file systemEventsApi.ts
 * @brief 封装系统事件的条件过滤与分页查询接口。
 * @details 查询条件可限定任务、设备、类别和时间窗口；返回数据来自持久化事件表而非浏览器日志缓存。
 */

export interface SystemEvent {
  tsMs: number
  category: string
  source: string
  detail: string
  taskId?: string
  deviceId?: string
}

export interface FetchSystemEventsParams {
  taskId?: string
  deviceId?: string
  category?: string
  startMs?: number
  endMs?: number
  limit?: number
}

export interface FetchSystemEventsResult {
  ok: boolean
  error?: string
  events: SystemEvent[]
}

const BASE = '/api/system-events'

function num(v: unknown, fallback = 0): number {
  const n = typeof v === 'number' ? v : Number(v)
  return Number.isFinite(n) ? n : fallback
}

function str(v: unknown): string {
  return typeof v === 'string' ? v : v != null ? String(v) : ''
}

function normalizeSystemEvent(raw: unknown): SystemEvent | null {
  if (!raw || typeof raw !== 'object') return null
  const o = raw as Record<string, unknown>
  const category = str(o.category)
  if (!category) return null
  const event: SystemEvent = {
    tsMs: num(o.tsMs),
    category,
    source: str(o.source),
    detail: str(o.detail),
  }
  const taskId = str(o.taskId)
  const deviceId = str(o.deviceId)
  if (taskId) event.taskId = taskId
  if (deviceId) event.deviceId = deviceId
  return event
}

export async function fetchSystemEvents(
  params: FetchSystemEventsParams = {},
): Promise<FetchSystemEventsResult> {
  const taskId = params.taskId?.trim()
  const deviceId = params.deviceId?.trim()
  const category = params.category?.trim()
  const qs = new URLSearchParams()
  if (taskId) qs.set('taskId', taskId)
  if (deviceId) qs.set('deviceId', deviceId)
  if (category) qs.set('category', category)
  if (params.startMs != null) qs.set('startMs', String(params.startMs))
  if (params.endMs != null) qs.set('endMs', String(params.endMs))
  if (params.limit != null) qs.set('limit', String(params.limit))

  try {
    const url = qs.toString() ? `${BASE}?${qs.toString()}` : BASE
    const res = await fetch(url)
    if (!res.ok) return { ok: false, error: `HTTP ${res.status}`, events: [] }
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || 'API 返回失败', events: [] }

    const events: SystemEvent[] = []
    for (const raw of data.events ?? []) {
      const ev = normalizeSystemEvent(raw)
      if (ev) events.push(ev)
    }
    return { ok: true, events }
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : '网络错误'
    return { ok: false, error: msg, events: [] }
  }
}

