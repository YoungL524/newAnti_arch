export interface EndpointTarget {
  address: string
  port: number
}

export interface CommEndpoint {
  id: string
  protocol: 'udp' | 'tcp'
  localAddress: string
  localPort: number
  description: string
  targets: Record<string, EndpointTarget>
}

export interface EndpointsResponse {
  ok: boolean
  error?: string
  endpoints?: CommEndpoint[]
  reloaded?: boolean
}

const BASE = '/api/endpoints'

function normalizeEndpoint(raw: Record<string, unknown>): CommEndpoint {
  const targets: Record<string, EndpointTarget> = {}
  const rawTargets = (raw.targets ?? {}) as Record<string, { address?: string; port?: number }>
  for (const [key, val] of Object.entries(rawTargets)) {
    targets[key] = { address: val.address ?? '127.0.0.1', port: val.port ?? 0 }
  }
  return {
    id: String(raw.id ?? ''),
    protocol: raw.protocol === 'tcp' ? 'tcp' : 'udp',
    localAddress: String(raw.localAddress ?? '127.0.0.1'),
    localPort: Number(raw.localPort ?? 0),
    description: String(raw.description ?? ''),
    targets,
  }
}

export function defaultEndpoint(id = ''): CommEndpoint {
  return {
    id,
    protocol: 'udp',
    localAddress: '127.0.0.1',
    localPort: 7788,
    description: '',
    targets: {
      dph: { address: '127.0.0.1', port: 3456 },
      workmode: { address: '127.0.0.1', port: 1234 },
    },
  }
}

export async function fetchEndpoints(): Promise<EndpointsResponse & { endpoints: CommEndpoint[] }> {
  try {
    const res = await fetch(BASE)
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || `HTTP ${res.status}`, endpoints: [] }
    const endpoints = (data.endpoints ?? []).map((e: Record<string, unknown>) => normalizeEndpoint(e))
    return { ok: true, endpoints }
  } catch (e: unknown) {
    return { ok: false, error: e instanceof Error ? e.message : '网络错误', endpoints: [] }
  }
}

export async function saveEndpoint(endpoint: CommEndpoint): Promise<EndpointsResponse & { endpoint?: CommEndpoint }> {
  try {
    const res = await fetch(`${BASE}/${encodeURIComponent(endpoint.id)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(endpoint),
    })
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || `HTTP ${res.status}` }
    const saved = data.endpoint
      ? normalizeEndpoint(data.endpoint as Record<string, unknown>)
      : endpoint
    return { ok: true, reloaded: data.reloaded, endpoint: saved }
  } catch (e: unknown) {
    return { ok: false, error: e instanceof Error ? e.message : '网络错误' }
  }
}

export async function saveEndpoints(endpoints: CommEndpoint[]): Promise<EndpointsResponse> {
  try {
    const res = await fetch(BASE, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ endpoints }),
    })
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || `HTTP ${res.status}` }
    return { ok: true, reloaded: data.reloaded, endpoints: (data.endpoints ?? endpoints).map((e: Record<string, unknown>) => normalizeEndpoint(e)) }
  } catch (e: unknown) {
    return { ok: false, error: e instanceof Error ? e.message : '网络错误' }
  }
}
