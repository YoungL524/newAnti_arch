/**
 * @file useCoreApi.tsx
 * @brief 集中管理核心服务唯一 WebSocket、页面状态、任务命令和当前会话航迹。
 * @details Provider 是浏览器内 /ws/v1 的唯一连接 Owner；近期原始点供表格使用，旧点压缩为地图路径形状，
 * 不额外轮询状态接口，也不复制后端 Topic 原始历史。
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import type { HeartbeatStatus, WsDeviceStateMessage, WsSystemStateMessage, WsTaskStateMessage, WsMessage, WsTracksMessage, WsTrackDisplayPoint, RadarTrack, LogEntry, TrackHistoryPoint, SystemEvent } from '../lib/protocol'
import { applyTrackStreamMessage, trackPointKey, trackTrailKey } from '../lib/trackStream'
import { startTaskAcquisitionApi, stopTaskAcquisitionApi } from '../lib/tasksApi'
import { fetchTaskLogPage } from '../lib/historyApi'
import { useTrackReplay } from './useTrackReplay'

interface DeviceState {
  id: string
  state: string
  acquisition: boolean
}

interface CoreState {
  connected: boolean
  deviceHeartbeat: WsDeviceStateMessage | null
  systemState: WsSystemStateMessage | null ///< Core 系统最后完整快照；断线或未初始化为 null。
  taskState: WsTaskStateMessage | null ///< TaskService 最后完整快照，None 与未收到消息的 null 含义不同。
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
  replay: ReturnType<typeof useTrackReplay> | null ///< 只读回放控制器；historyTracks 与其 points 为同一可见数组。
  systemEvents: SystemEvent[]
  historyLogs: LogEntry[]
  historyLoading: boolean
  historyError: string
  historyNextCursor: string | null ///< 当前页的更早游标，不是 Topic cursor，也不保存过去页面。
  loadEarlierHistoryLogs: () => void ///< 使用当前游标替换日志页。
  reloadHistoryLogs: () => void ///< 重新读取最新页，亦用于失败后重试。
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
  connected: false, deviceHeartbeat: null, systemState: null, taskState: null, acquisition: false, state: 'Unknown', commandPending: false,
  devices: [], checkedDeviceIds: [], noSelectionHint: false, commandError: '', interactionError: '',
  tracks: [], mapOldTracks: [], trackRate: 0, historyTracks: [], replay: null, systemEvents: [], historyLogs: [], historyLoading: false, historyError: '',
  historyViewNotice: '', isHistoryView: false,
  historyNextCursor: null, loadEarlierHistoryLogs: () => {}, reloadHistoryLogs: () => {},
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
 * @brief 将退出 Web 近期窗口的点移入旧路径，每 64 点压缩，形状和待压缩点合计最多 2048 点。
 * @param route 当前 TrackKey 在浏览器内唯一的旧路径派生状态。
 * @param point 刚被近期窗口覆盖的原始点。
 */
function appendOlderPoint(route: OlderRoute, point: WsTrackDisplayPoint): void {
  route.pending.push(point)
  const excess = route.simplified.length + route.pending.length - 2048
  if (excess > 0) {
    const oldCount = Math.min(excess, route.simplified.length)
    route.simplified.splice(0, oldCount)
    if (excess > oldCount) route.pending.splice(0, excess - oldCount)
  }
  if (route.pending.length < 64) return
  const previousEnd = route.simplified[route.simplified.length - 1]
  const chunk = previousEnd ? [previousEnd, ...route.pending] : route.pending
  const simplified = simplifyOlderChunk(chunk)
  route.simplified.push(...(previousEnd ? simplified.slice(1) : simplified))
  route.pending = []
  if (route.simplified.length > 2048) route.simplified.splice(0, route.simplified.length - 2048)
}

/**
 * @brief 全页旧路径最多 32768 点，优先裁剪最早前缀；不删除近期点，不修改设备消亡或归档。
 */
function trimOlderRoutes(routes: Map<string, OlderRoute>): void {
  let total = 0
  for (const route of routes.values()) total += route.simplified.length + route.pending.length
  while (total > 32768) {
    let oldest: OlderRoute | undefined
    let timestamp = Infinity
    for (const route of routes.values()) {
      const point = route.simplified[0] ?? route.pending[0]
      if (point && point.receiveUtcMs < timestamp) { oldest = route; timestamp = point.receiveUtcMs }
    }
    if (!oldest) break
    const prefix = oldest.simplified.length ? oldest.simplified : oldest.pending
    const count = Math.min(prefix.length, total - 32768)
    prefix.splice(0, count); total -= count
  }
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
    radialSpeed: point.radialVelocityMps,
    amplitude: point.amplitude,
    targetType: 0,
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

/** @brief 持有页面唯一实时连接与最后快照；任务快照变化通知台账 Owner 刷新，不在此复制台账。 */
export function CoreProvider({
  children,
  activeTaskId = null,
  viewTaskRunId = null,
  historyViewNotice = '',
  activeTaskDeviceIds = [],
  tasksReady = false,
  devicesReady = false,
  defaultCheckedDeviceIds = [],
  onTaskStateChange,
}: {
  children: ReactNode
  activeTaskId?: string | null
  viewTaskRunId?: string | null
  historyViewNotice?: string
  activeTaskDeviceIds?: string[]
  tasksReady?: boolean
  devicesReady?: boolean
  defaultCheckedDeviceIds?: string[]
  onTaskStateChange?: (snapshot: WsTaskStateMessage | null) => void ///< 已接受的任务变化或断线；由上层刷新台账与选择。
}) {
  const [state, setState] = useState<CoreState>(() => ({
    ...defaultState,
    setCheckedDevices: () => {}, toggleAcquisition: () => {},
  }))
  const [historyPage, setHistoryPage] = useState<{ taskId: string; cursor?: string }>({ taskId: '' })

  /** @brief React 提交最后快照后才通知台账 Owner，避免在 setState 更新函数内执行外部副作用。 */
  useEffect(() => {
    onTaskStateChange?.(state.taskState)
  }, [state.taskState, onTaskStateChange])

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
  const replay = useTrackReplay(isHistoryView ? viewTaskRunId ?? null : null, state.connected)
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
      historyNextCursor: null,
      historyViewNotice,
      isHistoryView,
    }))
  }, [viewTaskRunId, historyViewNotice, isHistoryView])

  /** @brief 历史任务切换、页切换或重连时读取唯一当前页；取消旧请求后禁止回写其他任务的页面。 */
  useEffect(() => {
    if (!isHistoryView || !viewTaskRunId) return
    if (!state.connected) {
      setState(prev => ({ ...prev, historyLoading: false, historyError: '后端未连接，无法读取归档日志', historyNextCursor: null }))
      return
    }
    const request = new AbortController()
    const cursor = historyPage.taskId === viewTaskRunId ? historyPage.cursor : undefined
    setState(prev => ({ ...prev, historyLoading: true, historyError: '', historyLogs: [], historyNextCursor: null }))
    fetchTaskLogPage(viewTaskRunId, cursor, request.signal)
      .then(page => {
        if (!request.signal.aborted) setState(prev => ({ ...prev, historyLogs: page.logs, historyNextCursor: page.nextCursor ?? null }))
      })
      .catch(error => {
        if (!request.signal.aborted) setState(prev => ({ ...prev, historyError: error instanceof Error ? error.message : '历史日志读取失败' }))
      })
      .finally(() => {
        if (!request.signal.aborted) setState(prev => ({ ...prev, historyLoading: false }))
      })
    return () => request.abort()
  }, [isHistoryView, viewTaskRunId, state.connected, historyPage])

  /** @brief 只替换当前历史页，不拼接全量历史；末页或已有请求时忽略重复点击。 */
  const loadEarlierHistoryLogs = useCallback(() => {
    if (isHistoryView && viewTaskRunId && state.historyNextCursor && !state.historyLoading)
      setHistoryPage({ taskId: viewTaskRunId, cursor: state.historyNextCursor })
  }, [isHistoryView, viewTaskRunId, state.historyNextCursor, state.historyLoading])

  /** @brief 清除该查询的分页位置并重新读取最新已提交记录，不要求后端强制排空归档。 */
  const reloadHistoryLogs = useCallback(() => {
    if (isHistoryView && viewTaskRunId && !state.historyLoading) setHistoryPage({ taskId: viewTaskRunId })
  }, [isHistoryView, viewTaskRunId, state.historyLoading])

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
    const applyTracksMessage = (msg: WsTracksMessage): boolean => {
      const expired = new Map<string, WsTrackDisplayPoint[]>()
      if (msg.type === 'trackSnapshot') {
        olderRoutes.clear()
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
        return false
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
      return true
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
        let changed = false
        for (const nextRaw of batch) {
          try {
            const msg = JSON.parse(nextRaw) as WsMessage
            if (msg.type === 'trackSnapshot' || msg.type === 'trackDelta') {
              if (!applyTracksMessage(msg)) break
              changed = true
            }
          } catch { ws?.close(4000, 'invalid tracks message'); break }
        }
        trimOlderRoutes(olderRoutes)
        if (changed) renderWireTracks()
      })
    }

    const WS_TRACKS_TYPE_RE = /"type"\s*:\s*"track(?:Snapshot|Delta)"/

    /** @brief 按同源实时接口建立连接；显式开发环境配置可覆盖地址，所有状态仍由本 Hook 独占。 */
    const connect = () => {
      if (cancelled) return

      /** @brief 默认使用同源实时出口，开发与部署共用代理；显式环境配置仍可覆盖地址。 */
      const url = (import.meta.env.VITE_WS_URL as string | undefined)
        ?? `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/ws/v1`

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
          if (isSystemStateMessage(msg)) {
            /** @brief 同一连接只接受更新 revision；重连已清空旧值，允许新进程从 1 开始。 */
            setState(prev => prev.systemState && BigInt(msg.revision) <= BigInt(prev.systemState.revision)
              ? prev : { ...prev, systemState: msg })
          } else if (isTaskStateMessage(msg)) {
            setState(prev => prev.taskState && BigInt(msg.revision) <= BigInt(prev.taskState.revision)
              ? prev : { ...prev, taskState: msg })
          } else if (isDeviceStateMessage(msg)) {
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
        if (tracksRaf) cancelAnimationFrame(tracksRaf)
        tracksRaf = 0; pendingTracksRaw = []; tracksRevision = null
        wireTracks.clear(); olderRoutes.clear(); tracksRef.current = []
        workingRef.current = false
        setState(prev => ({ ...prev, acquisition: false, tracks: [], mapOldTracks: [], trackRate: 0 }))
        logRef.current = []
        setState(prev => ({ ...prev, connected: false, deviceHeartbeat: null, systemState: null, taskState: null,
          historyLoading: false, historyNextCursor: null,
          historyError: prev.isHistoryView ? '后端未连接，无法读取归档日志' : prev.historyError }))
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
    historyTracks: replay.points, replay,
    loadEarlierHistoryLogs, reloadHistoryLogs,
  }), [state, setCheckedDevices, dismissNoSelectionHint, dismissCommandError, dismissInteractionError, toggleAcquisition, setTrackInteraction,
    loadEarlierHistoryLogs, reloadHistoryLogs, replay])

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
 * @brief 校验 Core 系统快照的时间、uint64 修订号、枚举与状态组合，拒绝伪造就绪及未知位。
 * @param value WebSocket JSON 解析后的未知输入；本函数不修改页面状态。
 * @return 满足 SystemState 网络合同才允许保留为最后快照。
 */
function isSystemStateMessage(value: unknown): value is WsSystemStateMessage {
  if (!value || typeof value !== 'object') return false
  const message = value as Partial<WsSystemStateMessage>
  const payload = message.payload
  return message.type === 'systemState'
    && typeof message.sentUtcMs === 'number' && Number.isSafeInteger(message.sentUtcMs) && message.sentUtcMs > 0
    && typeof message.revision === 'string' && /^[1-9]\d{0,19}$/.test(message.revision)
    && BigInt(message.revision) <= 18446744073709551615n
    && !!payload && typeof payload === 'object'
    && Number.isSafeInteger(payload.processStartedUtcMs) && payload.processStartedUtcMs > 0
    && payload.processStartedUtcMs <= message.sentUtcMs
    && Number.isInteger(payload.phase) && payload.phase >= 0 && payload.phase <= 4
    && Number.isInteger(payload.health) && payload.health >= 0 && payload.health <= 2
    && Number.isInteger(payload.readinessFlags) && payload.readinessFlags >= 0 && payload.readinessFlags <= 63
    && Number.isInteger(payload.degradationFlags) && payload.degradationFlags >= 0 && payload.degradationFlags <= 63
    && (payload.phase !== 1 || payload.readinessFlags === 63)
    && ((payload.phase !== 2 && payload.phase !== 3) || payload.readinessFlags === 0)
    && ((payload.phase === 4) === (payload.health === 2))
    && (payload.health === 2 || (payload.health === 0 ? payload.degradationFlags === 0 : payload.degradationFlags !== 0))
}

/**
 * @brief 校验独立任务流的时间、修订号、阶段和非零稳定身份；不把未收到状态当作 None。
 * @param value 网络 JSON 的未知输入，不修改任务台账或本地采集状态。
 * @return 只有 None 省略 taskId、其他阶段具有有效 UUID 时允许进入最后快照。
 */
function isTaskStateMessage(value: unknown): value is WsTaskStateMessage {
  if (!value || typeof value !== 'object') return false
  const message = value as Partial<WsTaskStateMessage>
  const payload = message.payload
  if (message.type !== 'taskState'
    || !Number.isSafeInteger(message.sentUtcMs) || (message.sentUtcMs ?? 0) <= 0
    || typeof message.revision !== 'string' || !/^[1-9]\d{0,19}$/.test(message.revision)
    || BigInt(message.revision) > 18446744073709551615n
    || !payload || typeof payload !== 'object') return false
  if (payload.phase === 'none') return payload.taskId === undefined
  return ['ready', 'running', 'completed', 'faulted'].includes(payload.phase)
    && typeof payload.taskId === 'string'
    && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(payload.taskId)
    && payload.taskId !== '00000000-0000-0000-0000-000000000000'
}

/**
 * @brief 读取 CoreProvider 拥有的页面级控制、实时显示和历史查询状态。
 * @return 当前 React 子树共享的只读状态与显式控制动作。
 * @warning 本 Hook 不允许直接读取 CoreDataDomain；浏览器只能通过 HTTP/WebSocket 序列化边界访问后端。
 */
export function useCore() {
  return useContext(CoreContext)
}
