import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { StatusResponse, WsMessage, RadarTrack } from '../lib/protocol'

interface CoreState {
  connected: boolean
  acquisition: boolean
  state: string
  tracks: RadarTrack[]
  trackRate: number           // 每秒接收航迹数 (证明后端数据流)
  toggleAcquisition: () => void
}

const defaultState: CoreState = {
  connected: false,
  acquisition: false,
  state: 'Unknown',
  tracks: [],
  trackRate: 0,
  toggleAcquisition: () => {},
}

const CoreContext = createContext<CoreState>(defaultState)

export function CoreProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<CoreState>(() => ({
    ...defaultState,
    toggleAcquisition: () => {},
  }))

  // 全量缓冲：WS 到达全速写入，1s interval 节流刷到 React state
  const tracksRef = useRef<RadarTrack[]>([])
  const trackCountRef = useRef(0)

  const toggleAcquisition = useCallback(() => {
    setState(prev => {
      const next = !prev.acquisition
      fetch(`/api/${next ? 'start' : 'stop'}`, { method: 'POST' })
        .catch(() => {})
      if (!next) {
        tracksRef.current = []
        trackCountRef.current = 0
      }
      return { ...prev, acquisition: next, tracks: next ? prev.tracks : [], trackRate: 0 }
    })
  }, [])

  // ── HTTP 轮询状态 ──
  useEffect(() => {
    const fetchStatus = async () => {
      try {
        const res = await fetch('/api/status')
        const data: StatusResponse = await res.json()
        if (data.ok) {
          setState(prev => ({ ...prev,
            connected: data.connected,
            acquisition: data.acquisition,
            state: data.state,
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
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
      const url = `${protocol}//${window.location.host}/ws`
      const wsUrl = import.meta.env.DEV ? url : 'ws://localhost:8089'

      ws = new WebSocket(wsUrl)
      ws.onopen = () => console.debug('[WS] connected')
      ws.onmessage = (e) => {
        try {
          const msg: WsMessage = JSON.parse(e.data)
          if (msg.type === 'status') {
            setState(prev => ({ ...prev,
              connected: msg.connected,
              acquisition: msg.acquisition,
              state: msg.state,
            }))
          } else if (msg.type === 'tracks') {
            // 全速写入 ref，不触发 React 渲染
            const t = msg.track
            if (t) {
              const row: RadarTrack = {
                batchId: t.targetId,
                slantRange: t.range,
                azimuth: t.azimuth * 180 / Math.PI,
                elevation: t.elevation * 180 / Math.PI,
                altitude: t.altitude,
                radialSpeed: t.radialSpeed,
                amplitude: 0, snr: 0, extrapolateFlag: 0,
                targetType: t.targetId,
                signalId: 0, sceneNum: 0,
                timestamp: t.timestamp,
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
    return () => {
      ws?.close()
      clearTimeout(reconnectTimer)
    }
  }, [])

  // ── 1s 节流：flush 到 React state + 统计速率 ──
  useEffect(() => {
    const id = setInterval(() => {
      setState(prev => {
        if (!prev.acquisition) return prev
        return {
          ...prev,
          tracks: [...tracksRef.current],          // 节流快照
          trackRate: trackCountRef.current,         // 上一秒的接收量
        }
      })
      trackCountRef.current = 0
    }, 1000)
    return () => clearInterval(id)
  }, [])

  const contextValue = useMemo(() => ({ ...state, toggleAcquisition }), [state, toggleAcquisition])

  return (
    <CoreContext.Provider value={contextValue}>
      {children}
    </CoreContext.Provider>
  )
}

export function useCore() {
  return useContext(CoreContext)
}
