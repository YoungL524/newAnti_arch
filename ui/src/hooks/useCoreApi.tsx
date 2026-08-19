/**
 * @file useCoreApi.tsx
 * @brief 集中管理核心服务状态、设备台账、任务命令和周期刷新。
 * @details Provider 是页面共享的控制面状态入口；实时航迹走独立 WebSocket Hook，避免低频 REST 刷新阻塞显示链路。
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { StatusResponse, LogsResponse, WsMessage, WsTracksMessage, WsTrackDisplayPoint, RadarTrack, DeviceStatus, LogEntry } from '../lib/protocol'
import { fetchTrackHistory, type TrackHistoryPoint } from '../lib/tracksApi'
import { fetchSystemEvents, type SystemEvent } from '../lib/systemEventsApi'
import { fetchTaskLogs } from '../lib/taskLogsApi'
import { applyTrackStreamMessage } from '../lib/trackStream'

interface CommandResponse {
  ok: boolean
  error?: string
  throttled?: boolean
  mode?: 'single-controller' | 'multi-controller'
  controllerId?: string
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
  historyTracks: TrackHistoryPoint[]
  systemEvents: SystemEvent[]
  historyLogs: LogEntry[]
  historyLoading: boolean
  historyError: string
  historyViewNotice: string
  isHistoryView: boolean
  trackRate: number
  logEntries: LogEntry[]
  liveTracksNotice: string
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
  tracks: [], trackRate: 0, historyTracks: [], systemEvents: [], historyLogs: [], historyLoading: false, historyError: '',
  historyViewNotice: '', isHistoryView: false,
  logEntries: [], liveTracksNotice: '',
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

export function CoreProvider({
  children,
  activeTaskId = null,
  viewTaskRunId = null,
  historyViewNotice = '',
  activeTaskDeviceIds = [],
  tasksReady = false,
  devicesReady = false,
  defaultCheckedDeviceIds = [],
}: {
  children: ReactNode
  activeTaskId?: string | null
  viewTaskRunId?: string | null
  historyViewNotice?: string
  activeTaskDeviceIds?: string[]
  tasksReady?: boolean
  devicesReady?: boolean
  defaultCheckedDeviceIds?: string[]
}) {
  const [state, setState] = useState<CoreState>(() => ({
    ...defaultState,
    setCheckedDevices: () => {}, toggleAcquisition: () => {},
  }))

  const tracksRef = useRef<RadarTrack[]>([])
  const trackCountRef = useRef(0)
  const logRef = useRef<LogEntry[]>([])
  const checkedRef = useRef<string[]>([])
  const pendingRef = useRef(false)
  const workingRef = useRef(false)
  const stateRef = useRef('Unknown')
  const pendingInteractionRef = useRef<Map<string, { selected: boolean; trackingEnabled: boolean }>>(new Map())
  const activeTaskIdRef = useRef<string | null>(activeTaskId ?? null)
  const activeTaskDeviceIdsRef = useRef<string[]>(activeTaskDeviceIds)
  const tasksReadyRef = useRef(tasksReady)
  const checkedUserOverrideRef = useRef(false)
  const lastCheckedInitKeyRef = useRef('')
  const prevActiveTaskIdForCheckRef = useRef<string | null | undefined>(undefined)
  activeTaskIdRef.current = activeTaskId ?? null
  activeTaskDeviceIdsRef.current = activeTaskDeviceIds
  tasksReadyRef.current = tasksReady
  const isHistoryView = !!viewTaskRunId && viewTaskRunId !== activeTaskId
  const selectionContextId = viewTaskRunId ?? activeTaskId

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
      }
    })
  }, [applyPendingInteractions])

  const clearLiveTracks = useCallback(() => {
    tracksRef.current = []
    trackCountRef.current = 0
    pendingInteractionRef.current.clear()
    setState(prev => ({ ...prev, tracks: [], trackRate: 0 }))
  }, [])

  const setCheckedDevices = useCallback((ids: string[]) => {
    checkedUserOverrideRef.current = true
    checkedRef.current = ids
    setState(prev => ({ ...prev, checkedDeviceIds: ids, noSelectionHint: false, liveTracksNotice: '' }))
  }, [])

  // 首次就绪或当前/历史任务切换时，按对应任务设备初始化勾选项。
  useEffect(() => {
    if (!devicesReady || !tasksReady) return

    const taskSwitched = prevActiveTaskIdForCheckRef.current !== undefined
      && prevActiveTaskIdForCheckRef.current !== selectionContextId
    prevActiveTaskIdForCheckRef.current = selectionContextId

    const initKey = `${selectionContextId ?? ''}|${defaultCheckedDeviceIds.join(',')}`
    const firstInit = checkedRef.current.length === 0
    const shouldInit = taskSwitched || firstInit
    if (!shouldInit) return
    if (checkedUserOverrideRef.current && !taskSwitched) return
    if (initKey === lastCheckedInitKeyRef.current && !taskSwitched) return

    lastCheckedInitKeyRef.current = initKey
    if (taskSwitched) checkedUserOverrideRef.current = false
    checkedRef.current = defaultCheckedDeviceIds
    setState(prev => ({ ...prev, checkedDeviceIds: defaultCheckedDeviceIds, noSelectionHint: false }))
  }, [selectionContextId, defaultCheckedDeviceIds, devicesReady, tasksReady])

  const dismissNoSelectionHint = useCallback(() => {
    setState(prev => ({ ...prev, noSelectionHint: false }))
  }, [])

  const dismissCommandError = useCallback(() => {
    setState(prev => ({ ...prev, commandError: '' }))
  }, [])

  const dismissInteractionError = useCallback(() => {
    setState(prev => ({ ...prev, interactionError: '' }))
  }, [])

  // REST 成功（mode=queued）仅表示命令已发布到总线；权威 interaction 状态以 WS 更新为准。
  // pendingInteractionRef 做乐观覆盖，待 snapshot/delta 回推一致后清除。
  const setTrackInteraction = useCallback(async (deviceId: string, trackId: number, selected: boolean, trackingEnabled: boolean): Promise<boolean> => {
    if (isHistoryView || !activeTaskIdRef.current) {
      setState(prev => ({ ...prev, interactionError: '历史记录为只读，当前没有可操作的实时任务' }))
      return false
    }

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
        if (pendingInteractionRef.current.get(key) === interaction) {
          pendingInteractionRef.current.delete(key)
          flushTracksToState()
        }
        const msg = data?.error || `HTTP ${res.status}`
        console.warn('[interaction]', msg)
        setState(prev => ({ ...prev, interactionError: `航迹交互失败: ${msg}` }))
        return false
      }
      window.setTimeout(() => {
        if (pendingInteractionRef.current.get(key) !== interaction) return
        pendingInteractionRef.current.delete(key)
        flushTracksToState()
        setState(prev => ({
          ...prev,
          interactionError: '航迹交互未收到实时快照确认，已撤销临时显示状态',
        }))
      }, 5000)
      return true
    } catch {
      if (pendingInteractionRef.current.get(key) === interaction) {
        pendingInteractionRef.current.delete(key)
        flushTracksToState()
      }
      console.warn('[interaction]', '网络错误')
      setState(prev => ({ ...prev, interactionError: '航迹交互失败: 网络错误' }))
      return false
    }
  }, [flushTracksToState, isHistoryView])

  // 同步 workingRef / stateRef 与 state
  useEffect(() => { workingRef.current = state.acquisition }, [state.acquisition])
  useEffect(() => { stateRef.current = state.state }, [state.state])

  const applyStatusResponse = useCallback((data: StatusResponse) => {
    if (!data.ok) return
    const devices: DeviceState[] = (data.devices || []).map((d: DeviceStatus) => ({
      id: d.id, state: d.state, acquisition: d.acquisition,
    }))
    const agg = computeAggregate(devices)
    setState(prev => ({
      ...prev, connected: data.connected, devices, ...agg,
    }))
  }, [])

  const refreshStatus = useCallback(async () => {
    try {
      const res = await fetch('/api/status')
      const data: StatusResponse = await res.json()
      applyStatusResponse(data)
    } catch {
      setState(prev => ({ ...prev, connected: false }))
    }
  }, [applyStatusResponse])

  // acquisition 变为 true 时，把 tracksRef 中已有数据刷到 state（避免 WS 早于 status 到达）
  useEffect(() => {
    if (state.acquisition) flushTracksToState()
  }, [state.acquisition, flushTracksToState])

  const toggleAcquisition = useCallback(async () => {
    const cids = checkedRef.current
    const isWorking = workingRef.current
    const isFault = stateRef.current === 'Fault'

    setState(prev => ({ ...prev, commandError: '' }))

    if (!isFault && !isWorking && !activeTaskIdRef.current) {
      setState(prev => ({
        ...prev,
        commandError: '请先创建并激活任务',
        noSelectionHint: false,
      }))
      return
    }

    if (!isFault && !isWorking && cids.length === 0) {
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

    if (isFault) {
      setState(prev => ({
        ...prev,
        acquisition: false,
        state: 'Standby',
        commandPending: true,
        commandError: '',
      }))

      const rollbackFault = (msg: string) => {
        console.warn('[reset]', msg)
        setState(prev => ({
          ...prev,
          acquisition: savedAcquisition,
          state: savedState,
          commandPending: false,
          commandError: msg,
        }))
      }

      try {
        const res = await fetch('/api/reset', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ deviceIds: cids }),
        })
        const data = await safeJsonParse(res)
        if (!data) { rollbackFault('后端返回异常'); return }
        if (!data.ok) { rollbackFault(data.error || '复位被拒绝'); return }

        const accepted = data.acceptedDeviceIds?.length ?? 0
        const rejected = data.rejectedDeviceIds?.length ?? 0
        if (accepted === 0 && rejected > 0) {
          rollbackFault('设备均不被支持: ' + JSON.stringify(data.rejectedDeviceIds))
          return
        }
        if (rejected > 0) {
          setState(prev => ({ ...prev, commandError: '部分设备未复位: ' + JSON.stringify(data.rejectedDeviceIds) }))
        }
        setState(prev => ({
          ...prev,
          acquisition: false,
          state: 'Standby',
          commandPending: false,
          commandError: '',
        }))
        await refreshStatus()
      } catch {
        rollbackFault('网络错误')
      }
      return
    }

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
      await refreshStatus()
    } catch {
      rollback('网络错误')
    }
  }, [refreshStatus])

  // ── active task 切换：仅清空实时航迹层 ──
  useEffect(() => {
    clearLiveTracks()

    if (!activeTaskId) {
      setState(prev => ({
        ...prev,
        liveTracksNotice: tasksReady ? '当前无激活任务，航迹显示未启用' : '',
        tracks: [],
        trackRate: 0,
      }))
      return
    }

    setState(prev => ({ ...prev, liveTracksNotice: '' }))
  }, [activeTaskId, clearLiveTracks, tasksReady])

  // ── viewTaskRunId：加载历史航迹与系统事件（不影响实时采集）──
  useEffect(() => {
    const historyTaskId = viewTaskRunId ?? activeTaskId

    if (!historyTaskId) {
      setState(prev => ({
        ...prev,
        historyTracks: [],
        systemEvents: [],
        historyLogs: [],
        historyLoading: false,
        historyError: '',
        historyViewNotice: '',
        isHistoryView: false,
      }))
      return
    }

    let cancelled = false
    setState(prev => ({
      ...prev,
      historyTracks: [],
      systemEvents: [],
      historyLogs: [],
      historyLoading: true,
      historyError: '',
      historyViewNotice,
      isHistoryView,
    }))

    ;(async () => {
      const [tracksRes, eventsRes, logsRes] = await Promise.all([
        fetchTrackHistory({ taskId: historyTaskId, limit: 1000, order: 'desc' }),
        fetchSystemEvents({ taskId: historyTaskId, limit: 200 }),
        fetchTaskLogs(historyTaskId),
      ])
      if (cancelled) return

      const errors: string[] = []
      if (!tracksRes.ok) errors.push(tracksRes.error ?? '历史航迹加载失败')
      if (!eventsRes.ok) errors.push(eventsRes.error ?? '系统事件加载失败')
      if (!logsRes.ok) errors.push(logsRes.error ?? '任务日志加载失败')

      setState(prev => ({
        ...prev,
        historyTracks: tracksRes.ok
          ? [...tracksRes.tracks].sort((a, b) => a.tsMs - b.tsMs)
          : [],
        systemEvents: eventsRes.ok ? eventsRes.events : [],
        historyLogs: logsRes.ok ? (logsRes.logs ?? []) : [],
        historyLoading: false,
        historyError: errors.join('；'),
        historyViewNotice,
        isHistoryView,
      }))
    })()

    return () => { cancelled = true }
  }, [viewTaskRunId, activeTaskId, historyViewNotice, isHistoryView, tasksReady])

  // ── HTTP 轮询 ──
  useEffect(() => {
    refreshStatus()
    const id = setInterval(refreshStatus, 5000)
    return () => clearInterval(id)
  }, [refreshStatus])

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
    let tracksRaf = 0
    const wireTracks = new Map<string, WsTrackDisplayPoint>()
    let tracksRevision = 0
    let pendingTracksRaw: string[] = []

    const renderWireTracks = () => {
      const ready = tasksReadyRef.current
      const currentTaskId = activeTaskIdRef.current
      const taskDevices = new Set(activeTaskDeviceIdsRef.current)
      const allTracks = [...wireTracks.values()]
      const received = allTracks.length

      let visibleTracks: WsTrackDisplayPoint[] = []
      let notice = ''

      if (!ready) {
        return
      }
      if (!currentTaskId) {
        notice = '当前无激活任务，航迹显示未启用'
      } else {
        visibleTracks = allTracks.filter(t =>
          t.taskId === currentTaskId
          && typeof t.deviceId === 'string'
          && taskDevices.has(t.deviceId))
      }

      if (import.meta.env.DEV && received > 0) {
        console.debug('[tracks]', {
          received,
          afterTaskFilter: visibleTracks.length,
          activeTaskId: currentTaskId,
          tasksReady: ready,
          checkedDeviceIds: checkedRef.current,
          acquisition: workingRef.current,
        })
      }

      tracksRef.current = visibleTracks.map((t): RadarTrack => ({
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
      trackCountRef.current = visibleTracks.length
      setState(prev => ({ ...prev, liveTracksNotice: notice }))
      for (const k of [...pendingInteractionRef.current.keys()]) {
        const [devId, tidStr] = k.split(':')
        const tid = Number(tidStr)
        const head = tracksRef.current.find(
          tr => tr.deviceId === devId && (tr.trackId ?? tr.batchId) === tid,
        )
        const pending = pendingInteractionRef.current.get(k)
        const confirmed = head
          ? (head.interaction ?? { selected: false, trackingEnabled: false })
          : null
        if (confirmed && pending
            && confirmed.selected === pending.selected
            && confirmed.trackingEnabled === pending.trackingEnabled) {
          pendingInteractionRef.current.delete(k)
        }
      }
      flushTracksToState()
    }

    const applyTracksMessage = (msg: WsTracksMessage) => {
      const nextRevision = applyTrackStreamMessage(wireTracks, tracksRevision, msg)
      if (nextRevision == null) {
        console.warn('[tracks] revision gap, reconnecting', tracksRevision, msg.revision)
        ws?.close(4000, 'tracks revision gap')
        return
      }
      tracksRevision = nextRevision
      renderWireTracks()
    }

    const scheduleTracksMessage = (raw: string) => {
      pendingTracksRaw.push(raw)
      if (pendingTracksRaw.length > 120) {
        pendingTracksRaw = []
        ws?.close(4001, 'tracks client backlog')
        return
      }
      if (tracksRaf) return
      tracksRaf = requestAnimationFrame(() => {
        tracksRaf = 0
        const batch = pendingTracksRaw
        pendingTracksRaw = []
        for (const nextRaw of batch) {
          try {
            const msg = JSON.parse(nextRaw) as WsMessage
            if (msg.type === 'tracks') applyTracksMessage(msg)
          } catch { /* ignore */ }
        }
      })
    }

    const WS_TRACKS_TYPE_RE = /"type"\s*:\s*"tracks"/

    const connect = () => {
      if (cancelled) return

      // 开发模式 WS：优先直连 core:8089；可通过 VITE_WS_URL 覆盖
      const url = (import.meta.env.VITE_WS_URL as string | undefined)
        ?? `ws://${window.location.hostname}:8089`

      ws = new WebSocket(url)

      ws.onopen = () => {
        if (cancelled) return
        console.debug('[WS] connected')
      }

      ws.onmessage = (e) => {
        if (cancelled) return
        const raw = String(e.data)
        // 航迹消息按序延后到 RAF 处理；delta 不允许像旧 snapshot 那样覆盖中间帧。
        if (WS_TRACKS_TYPE_RE.test(raw)) {
          scheduleTracksMessage(raw)
          return
        }
        try {
          const msg: WsMessage = JSON.parse(raw)
          if (msg.type === 'status') {
            const devices: DeviceState[] = (msg.devices || []).map((d: DeviceStatus) => ({
              id: d.id, state: d.state, acquisition: d.acquisition,
            }))
            const agg = computeAggregate(devices)
            setState(prev => ({
              ...prev, connected: msg.connected, devices, ...agg,
            }))
          } else if (msg.type === 'log') {
            const entry: LogEntry = {
              level: msg.level,
              message: msg.message,
              timestamp: msg.timestamp,
              ...(msg.taskId ? { taskId: msg.taskId } : {}),
              ...(msg.deviceId ? { deviceId: msg.deviceId } : {}),
            }
            const next = [...logRef.current, entry].slice(-200)
            logRef.current = next
            setState(prev => ({ ...prev, logEntries: next }))
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
      if (tracksRaf) cancelAnimationFrame(tracksRaf)
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

  // ── 1s 节流：采集中同步 tracks/trackRate；日志始终同步 ──
  useEffect(() => {
    const id = setInterval(() => {
      setState(prev => ({
        ...prev,
        logEntries: [...logRef.current],
        ...(prev.acquisition ? {
          tracks: applyPendingInteractions([...tracksRef.current]),
          trackRate: trackCountRef.current,
        } : {}),
      }))
      trackCountRef.current = 0
    }, 1000)
    return () => clearInterval(id)
  }, [applyPendingInteractions])

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
