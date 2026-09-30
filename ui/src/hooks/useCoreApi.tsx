/**
 * @file useCoreApi.tsx
 * @brief 集中管理核心服务唯一 WebSocket、页面状态、任务命令和当前会话航迹。
 * @details Provider 是浏览器内 /ws/v1 的唯一连接 Owner；近期原始点供表格使用，旧点压缩为地图路径形状，
 * 不额外轮询状态接口，也不复制后端 Topic 原始历史。
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { HeartbeatStatus, WsDeviceStateMessage, WsMessage, WsTracksMessage, WsTrackDisplayPoint, RadarTrack, LogEntry, TrackHistoryPoint, SystemEvent } from '../lib/protocol'
import { applyTrackStreamMessage, trackPointKey, trackTrailKey } from '../lib/trackStream'
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
  mapOldTracks: RadarTrack[] ///< 仅供地图绘制的旧路径形状点；数据表继续使用近期原始点。
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
  tracks: [], mapOldTracks: [], trackRate: 0, historyTracks: [], systemEvents: [], historyLogs: [], historyLoading: false, historyError: '',
  historyViewNotice: '', isHistoryView: false,
  logEntries: [], liveTracksNotice: '',
  setCheckedDevices: () => {}, dismissNoSelectionHint: () => {}, dismissCommandError: () => {}, dismissInteractionError: () => {}, toggleAcquisition: () => {},
  setTrackInteraction: async () => false,
}

const CoreContext = createContext<CoreState>(defaultState)

/** @brief 浏览器仅为当前采集会话保留旧路径形状和一个待压缩的小批次；近期原始点仍由 WebSocket 当前态持有。 */
interface OlderRoute {
  simplified: WsTrackDisplayPoint[] ///< 已按雷达局部三维空间 2 米误差压缩的旧路径端点。
  pending: WsTrackDisplayPoint[] ///< 尚未压缩的旧点，满 64 点后转入 simplified。
}

/**
 * @brief 在雷达局部 RFU 坐标系计算观测点；刚体安装姿态不改变三维点到线段距离。
 * @param point 已通过 Core 协议门禁的航迹显示点，角度单位为弧度。
 * @return 以米为单位的局部三维坐标。
 */
function localTrackXYZ(point: WsTrackDisplayPoint): { x: number; y: number; z: number } {
  const horizontal = point.rangeM * Math.cos(point.elevationRad)
  return {
    x: horizontal * Math.sin(point.azimuthRad),
    y: horizontal * Math.cos(point.azimuthRad),
    z: point.rangeM * Math.sin(point.elevationRad),
  }
}

/**
 * @brief 计算点到有限线段的平方距离，回头和急转弯不能因落在延长线上被删除。
 * @param point 待评估的局部三维点。
 * @param start 线段起点。
 * @param end 线段终点。
 * @return 平方米距离。
 */
function segmentDistanceSquared(
  point: { x: number; y: number; z: number },
  start: { x: number; y: number; z: number },
  end: { x: number; y: number; z: number },
): number {
  const dx = end.x - start.x
  const dy = end.y - start.y
  const dz = end.z - start.z
  const length2 = dx * dx + dy * dy + dz * dz
  const t = length2 === 0 ? 0 : Math.max(0, Math.min(1,
    ((point.x - start.x) * dx + (point.y - start.y) * dy + (point.z - start.z) * dz) / length2))
  const x = point.x - start.x - t * dx
  const y = point.y - start.y - t * dy
  const z = point.z - start.z - t * dz
  return x * x + y * y + z * z
}

/**
 * @brief 对最多 65 个已过近期窗口的旧点按 2 米误差保留起点、终点和转折点。
 * @details 只压缩本批原始点，不再次压缩此前已保留的路段，防止长时间运行时误差逐轮累积。
 * @param points 按点序号排列且属于同一 TrackKey 的旧点批次。
 * @return 保持输入顺序的路径形状点。
 */
function simplifyOlderChunk(points: WsTrackDisplayPoint[]): WsTrackDisplayPoint[] {
  if (points.length <= 2) return [...points]
  const xyz = points.map(localTrackXYZ)
  const keep = new Uint8Array(points.length)
  keep[0] = 1
  keep[points.length - 1] = 1
  const ranges: number[] = [0, points.length - 1]
  while (ranges.length > 0) {
    const end = ranges.pop()!
    const start = ranges.pop()!
    let farthest = -1
    let maximum = 4
    for (let i = start + 1; i < end; i++) {
      const distance = segmentDistanceSquared(xyz[i], xyz[start], xyz[end])
      if (distance > maximum) { maximum = distance; farthest = i }
    }
    if (farthest < 0) continue
    keep[farthest] = 1
    ranges.push(start, farthest, farthest, end)
  }
  return points.filter((_, index) => keep[index] === 1)
}

/**
 * @brief 将退出 Web 近期窗口的原始点移入当前会话旧路径，每 64 点压缩一次。
 * @param route 当前 TrackKey 在浏览器内唯一的旧路径派生状态。
 * @param point 刚被近期窗口覆盖的原始点。
 */
function appendOlderPoint(route: OlderRoute, point: WsTrackDisplayPoint): void {
  route.pending.push(point)
  if (route.pending.length < 64) return
  const previousEnd = route.simplified[route.simplified.length - 1]
  const chunk = previousEnd ? [previousEnd, ...route.pending] : route.pending
  const simplified = simplifyOlderChunk(chunk)
  route.simplified.push(...(previousEnd ? simplified.slice(1) : simplified))
  route.pending = []
}

/**
 * @brief 把 Web 显示点转为地图与表格共用的业务视图，保留会话身份和旧路径标志。
 * @param point WebSocket 中的一条当前会话显示点。
 * @param compacted 是否属于只绘制线路的旧路径。
 * @return 供实时图层消费的航迹点。
 */
function toRadarTrack(point: WsTrackDisplayPoint, compacted: boolean): RadarTrack {
  return {
    batchId: point.trackId,
    acquisitionId: point.acquisitionId,
    deviceId: point.deviceId,
    trackId: point.trackId,
    faceIndex: point.faceIndex,
    slantRange: point.rangeM,
    azimuth: point.azimuthRad * 180 / Math.PI,
    elevation: point.elevationRad * 180 / Math.PI,
    altitude: 0,
    radialSpeed: point.radialVelocityMps,
    amplitude: point.amplitude ?? 0, snr: 0, extrapolateFlag: 0,
    targetType: point.trackId, signalId: 0, sceneNum: 0,
    timestamp: point.receiveUtcMs,
    pointIndex: point.pointIndex,
    compacted,
    style: point.style,
    interaction: point.interaction,
  }
}

/**
 * @brief 从当前会话唯一的旧路径状态生成本轮地图视图，按任务设备过滤且不复制原始历史。
 * @param routes 浏览器当前会话的分目标简化路径。
 * @param deviceIds 当前任务允许显示的设备集合。
 * @return 仅包含地图所需形状点的短期 React 视图数组。
 */
function mapOlderRoutes(routes: Map<string, OlderRoute>, deviceIds: readonly string[]): RadarTrack[] {
  const allowed = new Set(deviceIds)
  const older: RadarTrack[] = []
  for (const route of routes.values()) {
    for (const point of route.simplified) {
      if (allowed.has(point.deviceId)) older.push(toRadarTrack(point, true))
    }
    for (const point of route.pending) {
      if (allowed.has(point.deviceId)) older.push(toRadarTrack(point, true))
    }
  }
  return older
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
  const olderRoutesRef = useRef<Map<string, OlderRoute>>(new Map())
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
        mapOldTracks: mapOlderRoutes(olderRoutesRef.current, activeTaskDeviceIdsRef.current),
      }
    })
  }, [applyPendingInteractions])

  const clearLiveTracks = useCallback(() => {
    tracksRef.current = []
    olderRoutesRef.current.clear()
    trackCountRef.current = 0
    pendingInteractionRef.current.clear()
    setState(prev => ({ ...prev, tracks: [], mapOldTracks: [], trackRate: 0 }))
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
        mapOldTracks: [],
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
    const olderRoutes = olderRoutesRef.current
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

      tracksRef.current = visibleTracks.map(point => toRadarTrack(point, false))
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

    /** @brief 先按 revision 应用完整批次，再以批次结束时仍存在的目标区分尾迹覆盖与目标消亡。 */
    const applyTracksMessage = (msg: WsTracksMessage) => {
      const expired = new Map<string, WsTrackDisplayPoint[]>()
      if (msg.type === 'trackSnapshot') {
        const currentTrails = new Set(msg.payload.tracks.map(trackTrailKey))
        for (const key of olderRoutes.keys()) {
          if (!currentTrails.has(key)) olderRoutes.delete(key)
        }
      } else {
        const sameBatchPoints = new Map(msg.payload.upserts.map(point => [trackPointKey(point), point]))
        for (const key of msg.payload.removals) {
          const trailKey = trackTrailKey(key)
          const pointKey = trackPointKey(key)
          const point = wireTracks.get(pointKey) ?? sameBatchPoints.get(pointKey)
          if (!point) continue
          const group = expired.get(trailKey) ?? []
          group.push(point)
          expired.set(trailKey, group)
        }
      }
      const nextRevision = applyTrackStreamMessage(wireTracks, tracksRevision, msg)
      if (nextRevision == null) {
        console.warn('[tracks] revision gap, reconnecting', tracksRevision, msg.revision)
        ws?.close(4000, 'tracks revision gap')
        return
      }
      if (msg.type === 'trackDelta') {
        const retired = new Set((msg.payload.retirements ?? []).map(trackTrailKey))
        const survivors = new Set([...wireTracks.values()].map(trackTrailKey))
        for (const trailKey of retired) olderRoutes.delete(trailKey)
        for (const key of msg.payload.removals) {
          const trailKey = trackTrailKey(key)
          if (!survivors.has(trailKey)) olderRoutes.delete(trailKey)
        }
        for (const [trailKey, points] of expired) {
          if (!survivors.has(trailKey) || retired.has(trailKey)) continue
          const route = olderRoutes.get(trailKey) ?? { simplified: [], pending: [] }
          points.sort((a, b) => a.pointIndex - b.pointIndex)
          for (const point of points) appendOlderPoint(route, point)
          olderRoutes.set(trailKey, route)
        }
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
            if (!acquisition) {
              /** 停止采集后丢弃尚未进入绘制帧的旧会话增量，避免清空后的画面被排队数据重新填充。 */
              pendingTracksRaw = []
              wireTracks.clear()
              olderRoutes.clear()
              tracksRef.current = []
              trackCountRef.current = 0
            }
            workingRef.current = acquisition
            setState(prev => ({
              ...prev,
              deviceHeartbeat: msg,
              devices: msg.payload.devices,
              acquisition,
              state: acquisition ? 'Working' : 'Standby',
              ...(!acquisition ? { tracks: [], mapOldTracks: [], trackRate: 0 } : {}),
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
          mapOldTracks: mapOlderRoutes(olderRoutesRef.current, activeTaskDeviceIdsRef.current),
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
  const pose = payload?.radarPose
  const validPose = pose === undefined || (
    typeof pose.deviceId === 'string' && pose.deviceId.length > 0
    && Number.isFinite(pose.longitudeDeg) && Math.abs(pose.longitudeDeg) <= 180
    && Number.isFinite(pose.latitudeDeg) && Math.abs(pose.latitudeDeg) <= 90
    && Number.isFinite(pose.altitudeM) && pose.altitudeM >= -1000 && pose.altitudeM <= 10000
    && Number.isInteger(pose.validFaceMask) && pose.validFaceMask > 0 && pose.validFaceMask <= 15
    && Array.isArray(pose.faces)
    && pose.faces.every((face, index) =>
      !!face && typeof face === 'object'
      && Number.isInteger(face.faceIndex) && face.faceIndex >= 0 && face.faceIndex < 4
      && (pose.validFaceMask & (1 << face.faceIndex)) !== 0
      && (index === 0 || pose.faces[index - 1].faceIndex < face.faceIndex)
      && Number.isFinite(face.headingDeg) && Math.abs(face.headingDeg) <= 180
      && Number.isFinite(face.pitchDeg) && Math.abs(face.pitchDeg) <= 90
      && Number.isFinite(face.rollDeg) && Math.abs(face.rollDeg) <= 90)
    && pose.faces.reduce((mask, face) => mask | (1 << face.faceIndex), 0) === pose.validFaceMask
  )
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
    && validPose
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
