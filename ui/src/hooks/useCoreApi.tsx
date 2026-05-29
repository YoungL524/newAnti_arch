import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react'
import type { StatusResponse, WsMessage } from '../lib/protocol'

interface CoreState {
  connected: boolean
  acquisition: boolean
  state: string
  toggleAcquisition: () => void
}

const defaultState: CoreState = {
  connected: false,
  acquisition: false,
  state: 'Unknown',
  toggleAcquisition: () => {},
}

const CoreContext = createContext<CoreState>(defaultState)

export function CoreProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<CoreState>(() => ({
    ...defaultState,
    toggleAcquisition: () => {},
  }))

  const toggleAcquisition = useCallback(() => {
    setState(prev => {
      const next = !prev.acquisition
      // Notify backend
      fetch('/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command: next ? 'start_acquisition' : 'stop_acquisition' }),
      }).catch(() => { /* backend not available — revert on next poll */ })
      return { ...prev, acquisition: next }
    })
  }, [])

  // Fetch initial status
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
        // core not running yet
      }
    }
    fetchStatus()
    const id = setInterval(fetchStatus, 5000)
    return () => clearInterval(id)
  }, [])

  // WebSocket
  useEffect(() => {
    let ws: WebSocket | null = null
    let reconnectTimer: ReturnType<typeof setTimeout>

    const connect = () => {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
      const url = `${protocol}//${window.location.host}/ws`
      // During dev, proxy handles /ws → localhost:8089
      // In production (core serves directly), we need port 8089
      const wsUrl = import.meta.env.DEV
        ? url
        : 'ws://localhost:8089'

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
            // future: dispatch track data to context consumers
          }
        } catch { /* ignore bad messages */ }
      }
      ws.onclose = () => {
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
