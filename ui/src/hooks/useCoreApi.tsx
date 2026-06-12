import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { StatusResponse, LogsResponse, WsMessage, WsTrackDisplayPoint, RadarTrack, DeviceStatus } from '../lib/protocol'

interface CommandResponse {
  ok: boolean
  error?: string
  throttled?: boolean
  acceptedDeviceIds?: string[]
  rejectedDeviceIds?: string[]
}

async function safeJsonParse(res: Response): Promise<CommandResponse | null> {
  try {
    const body = await res.text()
    const data = JSON.parse(body)
    if (data && typeof data.ok === 'boolean') return data as CommandResponse
    return { ok: false, error: `HTTP ${res.status}` }
  } catch {
    return { ok: false, error: `HTTP ${res.status}` }
  }
}

interface DeviceState {
  id: string
  state: string
  acquisition: boolean
}

interface CoreState {
  connected: boolean
  acquisition: boolean
  state: string
  devices: DeviceState[]
  checkedDeviceIds: string[]
  noSelectionHint: boolean
  commandError: string          // 启停命令失败原因（用户可见）
  tracks: RadarTrack[]
  trackRate: number
  logEntries: { level: string; message: string; timestamp: number }[]
  setCheckedDevices: (ids: string[]) => void
  dismissNoSelectionHint: () => void
  dismissCommandError: () => void
  toggleAcquisition: () => void
  setTrackInteraction: (deviceId: string, trackId: number, selected: boolean, trackingEnabled: boolean) => void
}

const defaultState: CoreState = {
  connected: false, acquisition: false, state: 'Unknown',
  devices: [], checkedDeviceIds: [], noSelectionHint: false, commandError: '',
  tracks: [], trackRate: 0, logEntries: [],
  setCheckedDevices: () => {}, dismissNoSelectionHint: () => {}, dismissCommandError: () => {}, toggleAcquisition: () => {},
  setTrackInteraction: () => {},
}

const CoreContext = createContext<CoreState>(defaultState)

function computeAggregate(devices: DeviceState[]): { acquisition: boolean; state: string } {
  if (devices.length === 0) return { acquisition: false, state: 'Unknown' }
  const anyWorking = devices.some(d => d.acquisition)
  const allStandby = devices.every(d => d.state === 'Standby')
  const anyFault = devices.some(d => d.state === 'Fault')
  const anyTransient = devices.some(d => d.state === 'Starting' || d.state === 'Stopping')
  return {
    acquisition: anyWorking || anyTransient,
    state: anyFault ? 'Fault' : allStandby ? 'Standby' : anyTransient ? 'Starting' : 'Working',
  }
}

export function CoreProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<CoreState>(() => ({
    ...defaultState,
    setCheckedDevices: () => {}, toggleAcquisition: () => {},
  }))

  const tracksRef = useRef<RadarTrack[]>([])
  const trackCountRef = useRef(0)
  const logRef = useRef<{ level: string; message: string; timestamp: number }[]>([])
  const checkedRef = useRef<string[]>([])
  const pendingRef = useRef(false)
  const workingRef = useRef(false)  // 与 state.acquisition 同步，供 toggleAcquisition 读取

  const setCheckedDevices = useCallback((ids: string[]) => {
    checkedRef.current = ids
    setState(prev => ({ ...prev, checkedDeviceIds: ids, noSelectionHint: false }))
  }, [])

  const dismissNoSelectionHint = useCallback(() => {
    setState(prev => ({ ...prev, noSelectionHint: false }))
  }, [])

  const dismissCommandError = useCallback(() => {
    setState(prev => ({ ...prev, commandError: '' }))
  }, [])

  const setTrackInteraction = useCallback(async (deviceId: string, trackId: number, selected: boolean, trackingEnabled: boolean) => {
    try {
      const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/tracks/${trackId}/interaction`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ selected, trackingEnabled }),
      })
      const data = await safeJsonParse(res)
      if (!data?.ok) console.warn('[interaction]', data?.error || `HTTP ${res.status}`)
    } catch {
      console.warn('[interaction]', '网络错误')
    }
  }, [])

  // 同步 workingRef 与 state.acquisition
  useEffect(() => { workingRef.current = state.acquisition }, [state.acquisition])

  const toggleAcquisition = useCallback(async () => {
    const cids = checkedRef.current
    const isWorking = workingRef.current

    setState(prev => ({ ...prev, commandError: '' }))

    if (!isWorking && cids.length === 0) {
      setState(prev => ({ ...prev, noSelectionHint: true, commandError: '' }))
      return
    }

    if (pendingRef.current) return
    pendingRef.current = true
    setTimeout(() => { pendingRef.current = false }, 1500)

    // 保存快照以备回滚
    const savedTracks = tracksRef.current
    const savedTrackCount = trackCountRef.current

    if (!isWorking) { tracksRef.current = []; trackCountRef.current = 0 }

    setState(prev => ({
      ...prev,
      acquisition: !isWorking,
      tracks: !isWorking ? [] : prev.tracks,
      trackRate: 0,
    }))

    const rollback = (msg: string) => {
      console.warn('[acquisition]', msg)
      tracksRef.current = savedTracks
      trackCountRef.current = savedTrackCount
      setState(prev => ({ ...prev, acquisition: isWorking, tracks: savedTracks, trackRate: savedTrackCount, commandError: msg }))
    }

    try {
      const res = await fetch(`/api/${isWorking ? 'stop' : 'start'}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ deviceIds: cids }),
      })
      const data = await safeJsonParse(res)
      if (!data) { rollback('后端返回异常'); return }
      if (!data.ok) { rollback(data.error || '请求被拒绝'); return }

      // Partial success: accepted some, rejected some — keep command, warn user
      const accepted = data.acceptedDeviceIds?.length ?? 0
      const rejected = data.rejectedDeviceIds?.length ?? 0
      if (accepted === 0 && rejected > 0) {
        rollback('设备均不被支持: ' + JSON.stringify(data.rejectedDeviceIds))
        return
      }
      if (rejected > 0) {
        setState(prev => ({ ...prev, commandError: '部分设备未执行: ' + JSON.stringify(data.rejectedDeviceIds) }))
      }
    } catch {
      rollback('网络错误')
    }
  }, [])

  // ── HTTP 轮询 ──
  useEffect(() => {
    const fetchStatus = async () => {
      try {
        const res = await fetch('/api/status')
        const data: StatusResponse = await res.json()
        if (data.ok) {
          const devices: DeviceState[] = (data.devices || []).map((d: DeviceStatus) => ({
            id: d.id, state: d.state, acquisition: d.acquisition,
          }))
          const agg = computeAggregate(devices)
          setState(prev => ({
            ...prev, connected: data.connected, devices, ...agg,
          }))
        }
      } catch {
        setState(prev => ({ ...prev, connected: false }))
      }
    }
    fetchStatus()
    const id = setInterval(fetchStatus, 5000)
    return () => clearInterval(id)
  }, [])

  // ── 初始加载：拉取最近日志 backlog ──
  useEffect(() => {
    fetch('/api/logs')
      .then(r => r.json())
      .then((data: LogsResponse) => {
        if (data.ok && data.logs) {
          logRef.current = data.logs
          setState(prev => ({ ...prev, logEntries: data.logs }))
        }
      })
      .catch(() => {})
  }, [])

  // ── WebSocket ──
  useEffect(() => {
    let ws: WebSocket | null = null
    let reconnectTimer: ReturnType<typeof setTimeout>

    const connect = () => {
      const url = import.meta.env.DEV
        ? `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/ws`
        : 'ws://localhost:8089'

      ws = new WebSocket(url)
      ws.onopen = () => console.debug('[WS] connected')
      ws.onmessage = (e) => {
        try {
          const msg: WsMessage = JSON.parse(e.data)
          if (msg.type === 'status') {
            const devices: DeviceState[] = (msg.devices || []).map((d: DeviceStatus) => ({
              id: d.id, state: d.state, acquisition: d.acquisition,
            }))
            const agg = computeAggregate(devices)
            setState(prev => ({
              ...prev, connected: msg.connected, devices, ...agg,
            }))
          } else if (msg.type === 'tracks') {
            // 全量 snapshot 替换
            if (msg.mode === 'snapshot' && Array.isArray(msg.tracks)) {
              tracksRef.current = (msg.tracks as WsTrackDisplayPoint[]).map((t) => ({
                batchId: t.trackId ?? 0,
                deviceId: t.deviceId,
                trackId: t.trackId,
                slantRange: t.range ?? 0,
                azimuth: ((t.azimuth ?? 0) * 180) / Math.PI,
                elevation: ((t.elevation ?? 0) * 180) / Math.PI,
                altitude: t.altitude ?? 0,
                radialSpeed: t.radialSpeed ?? 0,
                amplitude: 0, snr: 0, extrapolateFlag: 0,
                targetType: t.trackId ?? 0, signalId: 0, sceneNum: 0,
                timestamp: t.timestamp ?? Date.now(),
                pointIndex: t.pointIndex,
                style: t.style,
                interaction: (t as any).interaction,
              } as RadarTrack))
              trackCountRef.current = msg.tracks.length
            }
          } else if (msg.type === 'log') {
            logRef.current = [...logRef.current, {
              level: msg.level, message: msg.message, timestamp: msg.timestamp,
            }].slice(-200)
          }
        } catch { /* ignore */ }
      }
      ws.onclose = () => {
        setState(prev => ({ ...prev, connected: false }))
        reconnectTimer = setTimeout(connect, 3000)
      }
      ws.onerror = () => ws?.close()
    }

    connect()
    return () => { ws?.close(); clearTimeout(reconnectTimer) }
  }, [])

  // ── 1s 节流 flush ──
  useEffect(() => {
    const id = setInterval(() => {
      setState(prev => {
        if (!prev.acquisition) return prev
        return { ...prev, tracks: [...tracksRef.current], trackRate: trackCountRef.current, logEntries: [...logRef.current] }
      })
      trackCountRef.current = 0
    }, 1000)
    return () => clearInterval(id)
  }, [])

  const contextValue = useMemo(() => ({
    ...state, setCheckedDevices, dismissNoSelectionHint, dismissCommandError, toggleAcquisition, setTrackInteraction,
  }), [state, setCheckedDevices, dismissNoSelectionHint, dismissCommandError, toggleAcquisition, setTrackInteraction])

  return (
    <CoreContext.Provider value={contextValue}>
      {children}
    </CoreContext.Provider>
  )
}

export function useCore() {
  return useContext(CoreContext)
}
