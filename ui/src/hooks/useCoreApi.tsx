import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { StatusResponse, WsMessage, RadarTrack, DeviceStatus } from '../lib/protocol'

interface DeviceState {
  id: string
  state: string
  acquisition: boolean
}

interface CoreState {
  connected: boolean
  acquisition: boolean        // 聚合：是否有设备在工作
  state: string
  devices: DeviceState[]       // per-device 状态
  checkedDeviceIds: string[]   // 用户勾选的设备
  noSelectionHint: boolean     // 未勾选设备时的弹窗提示
  tracks: RadarTrack[]
  trackRate: number
  setCheckedDevices: (ids: string[]) => void
  dismissNoSelectionHint: () => void
  toggleAcquisition: () => void
}

const defaultState: CoreState = {
  connected: false, acquisition: false, state: 'Unknown',
  devices: [], checkedDeviceIds: [], noSelectionHint: false,
  tracks: [], trackRate: 0,
  setCheckedDevices: () => {}, dismissNoSelectionHint: () => {}, toggleAcquisition: () => {},
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
  const checkedRef = useRef<string[]>([])

  const setCheckedDevices = useCallback((ids: string[]) => {
    checkedRef.current = ids
    setState(prev => ({ ...prev, checkedDeviceIds: ids, noSelectionHint: false }))
  }, [])

  const dismissNoSelectionHint = useCallback(() => {
    setState(prev => ({ ...prev, noSelectionHint: false }))
  }, [])

  const toggleAcquisition = useCallback(() => {
    setState(prev => {
      const cids = checkedRef.current
      // 聚合判断：如果当前有设备在工作 → 停止；否则启动勾选的设备
      const anyWorking = prev.devices.some(d => d.acquisition)
      // 无勾选 → 弹窗提示
      if (!anyWorking && cids.length === 0) {
        setTimeout(() => setState(p => ({ ...p, noSelectionHint: true })), 0)
        return prev
      }
      const endpoint = anyWorking ? 'stop' : 'start'

      fetch(`/api/${endpoint}`, { method: 'POST' }).catch(() => {})

      if (!anyWorking) {
        tracksRef.current = []
        trackCountRef.current = 0
      }
      return { ...prev, acquisition: !anyWorking, tracks: !anyWorking ? [] : prev.tracks, trackRate: 0 }
    })
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
            const t = msg.track
            if (t) {
              const row: RadarTrack = {
                batchId: t.targetId, slantRange: t.range,
                azimuth: t.azimuth * 180 / Math.PI, elevation: t.elevation * 180 / Math.PI,
                altitude: t.altitude, radialSpeed: t.radialSpeed,
                amplitude: 0, snr: 0, extrapolateFlag: 0,
                targetType: t.targetId, signalId: 0, sceneNum: 0, timestamp: t.timestamp,
              }
              tracksRef.current = [...tracksRef.current, row].slice(-500)
              trackCountRef.current++
            }
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
        return { ...prev, tracks: [...tracksRef.current], trackRate: trackCountRef.current }
      })
      trackCountRef.current = 0
    }, 1000)
    return () => clearInterval(id)
  }, [])

  const contextValue = useMemo(() => ({
    ...state, setCheckedDevices, dismissNoSelectionHint, toggleAcquisition,
  }), [state, setCheckedDevices, dismissNoSelectionHint, toggleAcquisition])

  return (
    <CoreContext.Provider value={contextValue}>
      {children}
    </CoreContext.Provider>
  )
}

export function useCore() {
  return useContext(CoreContext)
}
