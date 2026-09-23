/**
 * @file useCoreApi.tsx
 * @brief 集中管理核心服务唯一 WebSocket、页面状态、任务命令和实时航迹。
 * @details Provider 是浏览器内 /ws/v1 的唯一连接 Owner；DeviceState 与航迹消息在此分流给最终视图，
 * 不额外轮询状态接口，也不复制后端 Topic 历史。
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { HeartbeatStatus, WsDeviceStateMessage, WsMessage, WsTracksMessage, WsTrackDisplayPoint, RadarTrack, LogEntry, TrackHistoryPoint, SystemEvent } from '../lib/protocol'
import { applyTrackStreamMessage } from '../lib/trackStream'
import { startTaskAcquisitionApi, stopTaskAcquisitionApi } from '../lib/tasksApi'

interface DeviceState {
  id: string
  state: string
  acquisition: boolean
}

interface CoreState {
  connected: boolean
  deviceHeartbeat: WsDeviceStateMessage | null
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
  connected: false, deviceHeartbeat: null, acquisition: false, state: 'Unknown', commandPending: false,
  devices: [], checkedDeviceIds: [], noSelectionHint: false, commandError: '', interactionError: '',
  tracks: [], trackRate: 0, historyTracks: [], systemEvents: [], historyLogs: [], historyLoading: false, historyError: '',
  historyViewNotice: '', isHistoryView: false,
  logEntries: [], liveTracksNotice: '',
  setCheckedDevices: () => {}, dismissNoSelectionHint: () => {}, dismissCommandError: () => {}, dismissInteractionError: () => {}, toggleAcquisition: () => {},
  setTrackInteraction: async () => false,
}

const CoreContext = createContext<CoreState>(defaultState)

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
  const workingRef = useRef(false)
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

  /**
   * @brief 在 DeviceController 尚未实现时明确拒绝航迹控制，避免旧 UI 向不存在的接口发送请求。
   * @return 恒为 false；不修改航迹投影，也不伪造设备 ACK。
   */
  const setTrackInteraction = useCallback(async (deviceId: string, trackId: number, selected: boolean, trackingEnabled: boolean): Promise<boolean> => {
    void deviceId
    void trackId
    void selected
    void trackingEnabled
    setState(prev => ({ ...prev, interactionError: '航迹控制尚未接入 DeviceController，当前只允许查看' }))
    return false
  }, [])

  // WebSocket消息回调使用ref读取最近一次已确认采集状态，避免重建连接。
  useEffect(() => { workingRef.current = state.acquisition }, [state.acquisition])

  // acquisition 变为 true 时，把 tracksRef 中已有数据刷到 state（避免 WS 早于 status 到达）
  useEffect(() => {
    if (state.acquisition) flushTracksToState()
  }, [state.acquisition, flushTracksToState])

  /**
   * @brief 把当前活动任务的启停意图提交给后端唯一ApplicationController。
   * @return 完成的 Promise；最终采集事实仍以后端DeviceState WebSocket为准。
   */
  const toggleAcquisition = useCallback(async () => {
    const taskId = activeTaskIdRef.current
    if (!taskId) {
      setState(prev => ({ ...prev, commandError: '当前没有活动任务' }))
      return
    }
    const stopping = workingRef.current
    setState(prev => ({ ...prev, commandPending: true, commandError: '' }))
    const result = stopping
      ? await stopTaskAcquisitionApi(taskId)
      : await startTaskAcquisitionApi(taskId)
    setState(prev => ({
      ...prev,
      commandPending: false,
      commandError: result.ok ? '' : (result.error ?? '采集命令失败'),
    }))
  }, [])

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

  // 历史归档 HTTP 尚未实现；只维护视图语义，禁止提前请求不存在的接口。
  useEffect(() => {
    setState(prev => ({
      ...prev,
      historyTracks: [],
      systemEvents: [],
      historyLogs: [],
      historyLoading: false,
      historyError: '',
      historyViewNotice,
      isHistoryView,
    }))
  }, [viewTaskRunId, historyViewNotice, isHistoryView])

  // ── WebSocket（页面唯一实时连接；开关事件就是后端出口连接事实）──
  useEffect(() => {
    let cancelled = false
    let ws: WebSocket | null = null
    let reconnectTimer: ReturnType<typeof setTimeout>
    let tracksRaf = 0
    const wireTracks = new Map<string, WsTrackDisplayPoint>()
    let tracksRevision: string | null = null
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
          typeof t.deviceId === 'string'
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
        faceIndex: t.faceIndex,
        slantRange: t.rangeM,
        azimuth: (t.azimuthRad * 180) / Math.PI,
        elevation: (t.elevationRad * 180) / Math.PI,
        altitude: 0,
        radialSpeed: t.radialVelocityMps,
        amplitude: t.amplitude ?? 0, snr: 0, extrapolateFlag: 0,
        targetType: t.trackId ?? 0, signalId: 0, sceneNum: 0,
        timestamp: t.receiveUtcMs,
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
            if (msg.type === 'trackSnapshot' || msg.type === 'trackDelta') applyTracksMessage(msg)
          } catch { /* ignore */ }
        }
      })
    }

    const WS_TRACKS_TYPE_RE = /"type"\s*:\s*"track(?:Snapshot|Delta)"/

    const connect = () => {
      if (cancelled) return

      // V1 实时出口固定使用/ws/v1；部署地址仍可通过VITE_WS_URL覆盖。
      const url = (import.meta.env.VITE_WS_URL as string | undefined)
        ?? `ws://${window.location.hostname || '127.0.0.1'}:8089/ws/v1`

      ws = new WebSocket(url)

      ws.onopen = () => {
        if (cancelled) return
        console.debug('[WS] connected')
        setState(prev => ({ ...prev, connected: true }))
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
          if (isDeviceStateMessage(msg)) {
            const acquisition = msg.payload.devices.some(device => device.acquisition)
            setState(prev => ({
              ...prev,
              deviceHeartbeat: msg,
              devices: msg.payload.devices,
              acquisition,
              state: acquisition ? 'Working' : 'Standby',
              ...(!acquisition ? { tracks: [], trackRate: 0 } : {}),
            }))
          } else if (msg.type === 'log') {
            if (msg.sequence && logRef.current.some(log => log.sequence === msg.sequence)) return
            const entry: LogEntry = {
              ...(msg.sequence ? { sequence: msg.sequence } : {}),
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
        logRef.current = []
        setState(prev => ({ ...prev, connected: false, deviceHeartbeat: null }))
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

/**
 * @brief 验证设备状态消息的冻结字段和四阵面位图边界。
 * @param value WebSocket JSON 解析后的未知消息。
 * @return 满足 V1 DeviceState 完整快照合同返回 true。
 */
function isDeviceStateMessage(value: unknown): value is WsDeviceStateMessage {
  if (!value || typeof value !== 'object') return false
  const message = value as Partial<WsDeviceStateMessage>
  const validStatus = (status: unknown): status is HeartbeatStatus =>
    status === 'unknownOrTimedOut' || status === 'online' || status === 'offlineOrLate'
  const payload = message.payload
  return message.type === 'deviceState'
    && typeof message.sentUtcMs === 'number'
    && typeof message.revision === 'string'
    && !!payload
    && validStatus(payload.dpHeartbeat)
    && validStatus(payload.spHeartbeat)
    && Number.isInteger(payload.fpOnlineMask)
    && Number.isInteger(payload.fpDataMask)
    && payload.fpOnlineMask >= 0 && payload.fpOnlineMask <= 0x0f
    && payload.fpDataMask >= 0 && payload.fpDataMask <= 0x0f
    && Array.isArray(payload.devices)
}

/**
 * @brief 读取 CoreProvider 拥有的页面级控制、实时显示和历史查询状态。
 * @return 当前 React 子树共享的只读状态与显式控制动作。
 * @warning 本 Hook 不允许直接读取 CoreDataDomain；浏览器只能通过 HTTP/WebSocket 序列化边界访问后端。
 */
export function useCore() {
  return useContext(CoreContext)
}
