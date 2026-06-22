import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { StatusResponse, LogsResponse, WsMessage, RadarTrack, DeviceStatus } from '../lib/protocol'

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
  commandPending: boolean
  devices: DeviceState[]
  checkedDeviceIds: string[]
  noSelectionHint: boolean
  commandError: string          // 启停命令失败原因（用户可见）
  interactionError: string      // 航迹交互失败原因（用户可见）
  tracks: RadarTrack[]
  trackRate: number
  logEntries: { level: string; message: string; timestamp: number }[]
  setCheckedDevices: (ids: string[]) => void
  dismissNoSelectionHint: () => void
  dismissCommandError: () => void
  dismissInteractionError: () => void
  toggleAcquisition: () => void
  setTrackInteraction: (deviceId: string, trackId: number, selected: boolean, trackingEnabled: boolean) => Promise<boolean>
}

const defaultState: CoreState = {
  connected: false, acquisition: false, state: 'Unknown', commandPending: false,
  devices: [], checkedDeviceIds: [], noSelectionHint: false, commandError: '', interactionError: '',
  tracks: [], trackRate: 0, logEntries: [],
  setCheckedDevices: () => {}, dismissNoSelectionHint: () => {}, dismissCommandError: () => {}, dismissInteractionError: () => {}, toggleAcquisition: () => {},
  setTrackInteraction: async () => false,
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
  const workingRef = useRef(false)
  const stateRef = useRef('Unknown')
  const pendingInteractionRef = useRef<Map<string, { selected: boolean; trackingEnabled: boolean }>>(new Map())

  const applyPendingInteractions = useCallback((list: RadarTrack[]): RadarTrack[] => {
    const pending = pendingInteractionRef.current
    if (pending.size === 0) return list
    return list.map(t => {
      const trackId = t.trackId ?? t.batchId
      if (!t.deviceId || trackId == null) return t
      const p = pending.get(`${t.deviceId}:${trackId}`)
      return p ? { ...t, interaction: p } : t
    })
  }, [])

  const flushTracksToState = useCallback(() => {
    setState(prev => {
      if (!prev.acquisition) return prev
      return {
        ...prev,
        tracks: applyPendingInteractions([...tracksRef.current]),
        logEntries: [...logRef.current],
      }
    })
  }, [applyPendingInteractions])

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

  const dismissInteractionError = useCallback(() => {
    setState(prev => ({ ...prev, interactionError: '' }))
  }, [])

  // REST 成功（mode=queued）仅表示命令已发布到总线；权威 interaction 状态以 WS snapshot 为准。
  // pendingInteractionRef 做乐观覆盖，待 snapshot 回推一致后清除。
  const setTrackInteraction = useCallback(async (deviceId: string, trackId: number, selected: boolean, trackingEnabled: boolean): Promise<boolean> => {
    const key = `${deviceId}:${trackId}`
    const interaction = { selected, trackingEnabled }
    pendingInteractionRef.current.set(key, interaction)
    flushTracksToState()
    setState(prev => ({ ...prev, interactionError: '' }))

    try {
      const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/tracks/${trackId}/interaction`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ selected, trackingEnabled }),
      })
      const data = await safeJsonParse(res)
      if (!data?.ok) {
        pendingInteractionRef.current.delete(key)
        flushTracksToState()
        const msg = data?.error || `HTTP ${res.status}`
        console.warn('[interaction]', msg)
        setState(prev => ({ ...prev, interactionError: `航迹交互失败: ${msg}` }))
        return false
      }
      return true
    } catch {
      pendingInteractionRef.current.delete(key)
      flushTracksToState()
      console.warn('[interaction]', '网络错误')
      setState(prev => ({ ...prev, interactionError: '航迹交互失败: 网络错误' }))
      return false
    }
  }, [flushTracksToState])

  // 同步 workingRef / stateRef 与 state
  useEffect(() => { workingRef.current = state.acquisition }, [state.acquisition])
  useEffect(() => { stateRef.current = state.state }, [state.state])

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

    const savedAcquisition = isWorking
    const savedState = stateRef.current

    // 保存快照以备回滚
    const savedTracks = tracksRef.current
    const savedTrackCount = trackCountRef.current

    if (!isWorking) {
      tracksRef.current = []
      trackCountRef.current = 0
      pendingInteractionRef.current.clear()
    }

    const nextTransient = !isWorking ? 'Starting' : 'Stopping'
    setState(prev => ({
      ...prev,
      acquisition: !isWorking,
      state: nextTransient,
      commandPending: true,
      tracks: !isWorking ? [] : prev.tracks,
      trackRate: 0,
    }))

    const rollback = (msg: string) => {
      console.warn('[acquisition]', msg)
      tracksRef.current = savedTracks
      trackCountRef.current = savedTrackCount
      setState(prev => ({
        ...prev,
        acquisition: savedAcquisition,
        state: savedState,
        commandPending: false,
        tracks: savedTracks,
        trackRate: savedTrackCount,
        commandError: msg,
      }))
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
      setState(prev => ({ ...prev, commandPending: false }))
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

  // ── WebSocket（实时推送；connected 状态由 HTTP/WS status 消息维护，不在 onclose 里误标断开）──
  useEffect(() => {
    let cancelled = false
    let ws: WebSocket | null = null
    let reconnectTimer: ReturnType<typeof setTimeout>

    const connect = () => {
      if (cancelled) return

      // 开发模式直连 core WS 端口，避免经 Vite 代理刷新时 ECONNRESET 刷屏
      const url = 'ws://localhost:8089'

      ws = new WebSocket(url)

      ws.onopen = () => {
        if (cancelled) return
        console.debug('[WS] connected')
      }

      ws.onmessage = (e) => {
        if (cancelled) return
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
            if (msg.mode === 'snapshot' && Array.isArray(msg.tracks)) {
              tracksRef.current = msg.tracks.map((t): RadarTrack => ({
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
                interaction: t.interaction,
              }))
              trackCountRef.current = msg.tracks.length
              for (const k of [...pendingInteractionRef.current.keys()]) {
                const [devId, tidStr] = k.split(':')
                const tid = Number(tidStr)
                const head = tracksRef.current.find(
                  tr => tr.deviceId === devId && (tr.trackId ?? tr.batchId) === tid,
                )
                const pending = pendingInteractionRef.current.get(k)
                if (head?.interaction && pending
                    && head.interaction.selected === pending.selected
                    && head.interaction.trackingEnabled === pending.trackingEnabled) {
                  pendingInteractionRef.current.delete(k)
                }
              }
              flushTracksToState()
            }
          } else if (msg.type === 'log') {
            logRef.current = [...logRef.current, {
              level: msg.level, message: msg.message, timestamp: msg.timestamp,
            }].slice(-200)
          }
        } catch { /* ignore */ }
      }

      ws.onclose = () => {
        if (cancelled) return
        // 刷新/StrictMode 卸载会主动 close，此处只负责重连，不把 connected 置 false
        // （connected 表示后端通信健康，由 /api/status 与 WS status 推送维护）
        reconnectTimer = setTimeout(connect, 1000)
      }
    }

    connect()

    return () => {
      cancelled = true
      clearTimeout(reconnectTimer)
      if (ws) {
        ws.onopen = null
        ws.onmessage = null
        ws.onclose = null
        ws.onerror = null
        ws.close()
        ws = null
      }
    }
  }, [flushTracksToState])

  // ── 1s 节流：仅刷新 trackRate，航迹数据已在 WS 到达时即时更新 ──
  useEffect(() => {
    const id = setInterval(() => {
      setState(prev => {
        if (!prev.acquisition) return prev
        return { ...prev, trackRate: trackCountRef.current, logEntries: [...logRef.current] }
      })
      trackCountRef.current = 0
    }, 1000)
    return () => clearInterval(id)
  }, [])

  const contextValue = useMemo(() => ({
    ...state, setCheckedDevices, dismissNoSelectionHint, dismissCommandError, dismissInteractionError, toggleAcquisition, setTrackInteraction,
  }), [state, setCheckedDevices, dismissNoSelectionHint, dismissCommandError, dismissInteractionError, toggleAcquisition, setTrackInteraction])

  return (
    <CoreContext.Provider value={contextValue}>
      {children}
    </CoreContext.Provider>
  )
}

export function useCore() {
  return useContext(CoreContext)
}
