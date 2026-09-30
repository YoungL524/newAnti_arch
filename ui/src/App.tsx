/**
 * @file App.tsx
 * @brief 组装任务控制、设备管理、地图航迹和底部数据面板的应用根组件。
 * @details 根组件协调各视图的选择状态与对话框，不复制 CoreApi Provider 和航迹 Hook 所拥有的服务端状态。
 */

import { useCallback, useEffect, useMemo, useState, useRef } from 'react'
import './styles/globals.css'
import TitleBar from './components/layout/TitleBar'
import Sidebar from './components/layout/Sidebar'
import MapRadarView from './components/map/MapRadarView'
import BottomPanel from './components/panels/BottomPanel'
import StatusBar from './components/layout/StatusBar'
import { CoreProvider } from './hooks/useCoreApi'
import { type AppDevice, type RadarDevice, type SubDevice } from './components/map/types'
import DeviceEditDialog from './components/dialogs/DeviceEditDialog'
import { fetchDevices, createDevice, deleteDeviceApi, updateDevice as apiUpdateDevice, radarToApiPayload, subToApiPayload, apiDeviceToRadar, apiDeviceToSub, type DeviceRegistryStatus } from './lib/devicesApi'
import TaskCreateDialog, { type TaskCreateResult } from './components/dialogs/TaskCreateDialog'
import type { RadarControlSettings, Task, TaskTemplate } from './lib/taskTypes'
import {
  fetchTasksFromBackend,
  createTaskApi,
  activateTaskApi,
  completeTaskApi,
  archiveTaskApi,
  deleteTaskApi,
  apiTaskToTask,
  taskToApiPayload,
  fetchTaskTemplates,
  updateTaskControlSettingsApi,
} from './lib/tasksApi'
/**
 * @brief 组装雷达上位机单页应用，并协调任务、设备及当前视图选择。
 * @return 包含 CoreProvider、地图、侧栏和数据面板的 React 根节点。
 * @note 后端事实由各 API/Hook owner 持有，本组件只保存页面编排状态。
 */
export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)
  const [deviceRegistryStatus, setDeviceRegistryStatus] = useState<DeviceRegistryStatus>('loading')
  const [devices, setDevices] = useState<RadarDevice[]>([])
  const [subDevices, setSubDevices] = useState<SubDevice[]>([])
  const devicesRef = useRef<RadarDevice[]>([])
  const subDevicesRef = useRef<SubDevice[]>([])

  const [tasks, setTasks] = useState<Task[]>([])
  const [activeTaskId, setActiveTaskId] = useState<string | null>(null)
  const [viewTaskRunId, setViewTaskRunId] = useState<string | null>(null)
  const [tasksReady, setTasksReady] = useState(false)
  const [tasksBackend, setTasksBackend] = useState(false)
  const [taskTemplates, setTaskTemplates] = useState<TaskTemplate[]>([])
  const [showTaskDialog, setShowTaskDialog] = useState(false)
  const [radarSettingsSaving, setRadarSettingsSaving] = useState(false)
  const [taskActionPendingId, setTaskActionPendingId] = useState<string | null>(null)
  const [taskActionError, setTaskActionError] = useState('')
  const tasksRef = useRef<Task[]>([])
  const activeTaskIdRef = useRef<string | null>(null)

  useEffect(() => { devicesRef.current = devices }, [devices])
  useEffect(() => { subDevicesRef.current = subDevices }, [subDevices])
  useEffect(() => { tasksRef.current = tasks }, [tasks])
  useEffect(() => { activeTaskIdRef.current = activeTaskId }, [activeTaskId])

  const reloadBackendData = useCallback(() => {
    fetchDevices()
      .then(({ ok, radars, subs }) => {
        if (!ok) {
          setDeviceRegistryStatus('unavailable')
          return
        }
        setDevices(radars)
        setSubDevices(subs)
        setDeviceRegistryStatus('available')
      })
      .catch(() => {
        setDeviceRegistryStatus('unavailable')
      })

    fetchTasksFromBackend()
      .then(({ ok, tasks: loaded, activeTaskId: activeId }) => {
        if (!ok) {
          setTasksBackend(false)
          return
        }
        setTasks(loaded)
        setActiveTaskId(activeId)
        setViewTaskRunId(prev => {
          if (prev && loaded.some(t => t.id === prev)) return prev
          return activeId
        })
        setTasksBackend(true)
      })
      .catch(() => setTasksBackend(false))
      .finally(() => setTasksReady(true))

    fetchTaskTemplates()
      .then(result => setTaskTemplates(result.ok ? result.data?.templates ?? [] : []))
      .catch(() => setTaskTemplates([]))
  }, [])

  useEffect(() => {
    reloadBackendData()
    const id = setInterval(() => {
      if (deviceRegistryStatus === 'unavailable' || !tasksBackend)
        reloadBackendData()
    }, 5000)
    return () => clearInterval(id)
  }, [reloadBackendData, deviceRegistryStatus, tasksBackend])

  const activeTask = useMemo(() => tasks.find(t => t.id === activeTaskId) ?? null, [tasks, activeTaskId])
  const viewTask = useMemo(() => tasks.find(t => t.id === viewTaskRunId) ?? null, [tasks, viewTaskRunId])
  const isHistoryView = !!viewTaskRunId && viewTaskRunId !== activeTaskId
  const displayTask = isHistoryView ? viewTask : activeTask
  const lastReusableTask = useMemo(() =>
    [...tasks].reverse().find(t => t.status === 'completed' || t.status === 'archived') ?? null,
  [tasks])
  const historyViewNotice = useMemo(() => {
    if (!viewTaskRunId || viewTaskRunId === activeTaskId) return ''
    return viewTask ? `正在查看历史执行记录：${viewTask.name}` : '正在查看历史执行记录'
  }, [viewTaskRunId, activeTaskId, viewTask])

  const canDeleteTask = useCallback((t: Task) =>
    ((t.status === 'draft' && !t.activatedAt) || t.status === 'archived') && t.id !== activeTaskId,
  [activeTaskId])

  const viewTaskRun = useCallback((taskId: string) => {
    setViewTaskRunId(taskId)
  }, [])

  const returnToActiveView = useCallback(() => {
    setViewTaskRunId(activeTaskIdRef.current)
  }, [])

  const displayTaskDeviceIds = useMemo(() => new Set(displayTask?.deviceIds ?? []), [displayTask])
  const activeTaskDeviceIdList = useMemo(() => activeTask?.deviceIds ?? [], [activeTask])
  const allDevices = useMemo(() => [...devices, ...subDevices], [devices, subDevices])
  const displayTaskDevices = useMemo(() =>
    displayTask ? allDevices.filter(d => displayTaskDeviceIds.has(d.id)) : allDevices,
  [allDevices, displayTask, displayTaskDeviceIds])
  const displayRadarDevices = useMemo(() =>
    displayTask ? devices.filter(d => displayTaskDeviceIds.has(d.id)) : devices,
  [devices, displayTask, displayTaskDeviceIds])
  const visibleDevices = displayTask ? displayTaskDevices : allDevices
  const visibleDeviceListEmpty = visibleDevices.length === 0
  const devicesReady = deviceRegistryStatus !== 'loading'

  const defaultCheckedDeviceIds = useMemo(() => {
    const controllable = devices.filter(d => d.backendManaged && d.endpointId)
    if (displayTask && displayTask.deviceIds.length > 0) {
      return controllable.filter(d => displayTask.deviceIds.includes(d.id)).map(d => d.id)
    }
    return controllable.map(d => d.id)
  }, [devices, displayTask])

  const createTask = useCallback(async (data: Omit<Task, 'id' | 'createdAt' | 'status'>): Promise<TaskCreateResult> => {
    setTaskActionError('')
    if (!tasksBackend) {
      return { ok: false, error: '设备与状态服务已连接，但任务 HTTP 服务尚未接通' }
    }
    if (activeTaskIdRef.current) {
      return { ok: false, error: '已有进行中的任务，请先完成当前任务' }
    }
    const created = await createTaskApi(taskToApiPayload(data))
    if (!created.ok || !created.data?.task) {
      console.warn('[tasks] 创建失败:', created.error)
      return { ok: false, error: created.error || '任务创建失败' }
    }
    const activated = await activateTaskApi(created.data.task.taskId)
    if (!activated.ok || !activated.data?.task) {
      console.warn('[tasks] 激活失败:', activated.error)
      const draftTask = {
        ...apiTaskToTask(created.data.task),
        radarArrayType: data.radarArrayType,
        deviceFaceMasks: data.deviceFaceMasks,
      }
      setTasks(prev => prev.some(t => t.id === draftTask.id) ? prev.map(t => t.id === draftTask.id ? draftTask : t) : [...prev, draftTask])
      return { ok: false, error: `任务已创建为草稿，但激活失败：${activated.error || '请确认任务已绑定可控雷达'}` }
    }
    const task = {
      ...apiTaskToTask(activated.data.task),
      radarArrayType: activated.data.task.radarArrayType ?? data.radarArrayType,
      deviceFaceMasks: Object.keys(activated.data.task.deviceFaceMasks ?? {}).length > 0
        ? activated.data.task.deviceFaceMasks ?? {}
        : data.deviceFaceMasks,
    }
    setTasks(prev => prev.some(t => t.id === task.id) ? prev.map(t => t.id === task.id ? task : t) : [...prev, task])
    setActiveTaskId(task.id)
    setViewTaskRunId(task.id)
    setShowTaskDialog(false)
    return { ok: true }
  }, [tasksBackend])

  /**
   * @brief 从已完成任务创建新的执行记录；激活前核对后端复制的参数快照，防止模板变化导致旧配置被替换。
   * @param sourceTaskId 历史任务的稳定 UUID；原记录和归档事实保持不变。
   * @note 核对失败时删除刚创建的草稿；删除失败则保留草稿并提示用户处理。
   */
  const rerunTask = useCallback(async (sourceTaskId: string) => {
    if (taskActionPendingId !== null) return
    const source = tasksRef.current.find(task => task.id === sourceTaskId)
    if (!source || (source.status !== 'completed' && source.status !== 'archived')) {
      setTaskActionError('只能再次执行已完成或已归档的任务')
      return
    }
    if (activeTaskIdRef.current) {
      setTaskActionError('已有当前任务，请先完成当前任务')
      return
    }
    if (!tasksBackend) {
      setTaskActionError('核心服务未连接，无法再次执行任务')
      return
    }
    setTaskActionPendingId(sourceTaskId)
    setTaskActionError('')
    try {
      const created = await createTaskApi(taskToApiPayload(source))
      if (!created.ok || !created.data?.task) {
        setTaskActionError(created.error || '复制任务失败')
        return
      }
      const copy = created.data.task
      if (JSON.stringify(copy.controlSettings) !== JSON.stringify(source.controlSettings)) {
        const removed = await deleteTaskApi(copy.taskId)
        setTaskActionError(removed.ok
          ? '原任务的参数快照与当前模板不同，已取消再次执行'
          : `原任务参数与当前模板不同；新草稿“${copy.name}”未能自动删除，请手动处理`)
        if (!removed.ok) reloadBackendData()
        return
      }
      const activated = await activateTaskApi(copy.taskId)
      if (!activated.ok || !activated.data?.task) {
        setTasks(prev => [...prev, apiTaskToTask(copy)])
        setTaskActionError(`任务已复制为草稿，但激活失败：${activated.error || '请检查设备绑定'}`)
        return
      }
      const task = apiTaskToTask(activated.data.task)
      setTasks(prev => [...prev, task])
      setActiveTaskId(task.id)
      setViewTaskRunId(task.id)
    } finally {
      setTaskActionPendingId(null)
    }
  }, [taskActionPendingId, tasksBackend, reloadBackendData])

  const completeTask = useCallback(async () => {
    const id = activeTaskIdRef.current
    if (!id) return
    setTaskActionError('')
    if (!tasksBackend) {
      setTaskActionError('核心服务未连接，无法完成任务')
      return
    }
    const r = await completeTaskApi(id)
    if (!r.ok) {
      setTaskActionError(r.error || '完成任务失败')
      return
    }
    if (r.data?.task) {
      const updated = apiTaskToTask(r.data.task)
      setTasks(prev => prev.map(t => t.id === id ? updated : t))
    }
    setActiveTaskId(null)
    setViewTaskRunId(id)
  }, [tasksBackend])

  /**
   * @brief 请求后端持久化任务归档状态，成功后只替换该任务展示快照。
   * @param taskId 已完成且非活动任务的稳定 UUID。
   */
  const archiveTask = useCallback(async (taskId: string) => {
    if (taskActionPendingId !== null) return
    setTaskActionError('')
    if (!tasksBackend) {
      setTaskActionError('核心服务未连接，无法归档任务')
      return
    }
    setTaskActionPendingId(taskId)
    try {
      const result = await archiveTaskApi(taskId)
      if (!result.ok || !result.data?.task) {
        setTaskActionError(result.error || '归档任务失败')
        return
      }
      const archived = apiTaskToTask(result.data.task)
      setTasks(prev => prev.map(task => task.id === taskId ? archived : task))
    } finally {
      setTaskActionPendingId(null)
    }
  }, [taskActionPendingId, tasksBackend])

  const deleteTask = useCallback(async (taskId: string) => {
    const task = tasksRef.current.find(t => t.id === taskId)
    if (task?.status === 'archived'
        && !window.confirm(`从任务列表删除“${task.name}”？历史航迹和事件仍按保留策略保存。`)) return
    setTaskActionError('')
    if (!tasksBackend) {
      setTaskActionError('核心服务未连接，无法删除任务')
      return
    }
    const r = await deleteTaskApi(taskId)
    if (!r.ok) { setTaskActionError(r.error || '删除失败'); return }
    setTasks(prev => prev.filter(t => t.id !== taskId))
    if (activeTaskIdRef.current === taskId) setActiveTaskId(null)
    if (viewTaskRunId === taskId) {
      setViewTaskRunId(activeTaskIdRef.current === taskId ? null : activeTaskIdRef.current)
    }
  }, [tasksBackend, viewTaskRunId])

  /**
   * @brief 提交当前活动任务的完整控制参数快照，并用响应替换 tasks 中的对应任务。
   * @param settings 对话框中的完整本地草稿。
   * @return ok 表示服务端已持久化并可关闭对话框；失败时保留草稿并回传可展示错误。
   * @note 成功后只替换该任务快照，activeTask 派生值随之刷新；不整页重载，也不另存第二份已应用参数。
   */
  const applyRadarSettings = useCallback(async (settings: RadarControlSettings) => {
    const id = activeTaskIdRef.current
    if (!id) return { ok: false, error: '当前没有活动任务' }
    setRadarSettingsSaving(true)
    try {
      const result = await updateTaskControlSettingsApi(id, settings)
      if (!result.ok || !result.data?.task) {
        return { ok: false, error: result.error || '参数更新失败' }
      }
      const updated = apiTaskToTask(result.data.task)
      setTasks(prev => prev.map(task => task.id === id ? updated : task))
      return { ok: true }
    } catch (e: unknown) {
      return { ok: false, error: e instanceof Error ? e.message : '参数更新失败' }
    } finally {
      setRadarSettingsSaving(false)
    }
  }, [])

  const radarSettingsDisabled = !tasksBackend || isHistoryView || !activeTask
    || (activeTask.status !== 'ready' && activeTask.status !== 'active')
    || radarSettingsSaving

  const addDevice = useCallback(async (d: Omit<RadarDevice, 'id'>) => {
    const r = await createDevice(radarToApiPayload('', d as RadarDevice))
    if (!r.ok) { console.warn('[devices] 添加设备失败:', r.error); return false }
    const apiDev = r.data?.device
    if (!apiDev) { console.warn('[devices] 后端未返回设备数据'); return false }
    const radar = apiDeviceToRadar(apiDev)
    if (!radar) { console.warn('[devices] 后端返回数据格式错误'); return false }
    setDevices(prev => {
      const migrated = r.data?.migratedDeviceIds ?? []
      const cleared = prev.map(item =>
        migrated.includes(item.id) ? { ...item, endpointId: undefined } : item)
      return [...cleared, radar]
    })
    return true
  }, [])

  const removeDevice = useCallback(async (id: string) => {
    const current = devicesRef.current.find(device => device.id === id)
    if (!current) return false
    const r = await deleteDeviceApi(id, current.revision)
    if (!r.ok) { console.warn('[devices] 删除失败:', r.error); return false }
    setDevices(prev => prev.filter(d => d.id !== id))
    setTasks(prev => prev.map(t => t.deviceIds.includes(id)
      ? { ...t, deviceIds: t.deviceIds.filter(deviceId => deviceId !== id) }
      : t))
    return true
  }, [])

  const addSubDevice = useCallback(async (d: Omit<SubDevice, 'id'>) => {
    const r = await createDevice(subToApiPayload('', d as SubDevice))
    if (!r.ok) { console.warn('[devices] 添加子设备失败:', r.error); return false }
    const apiDev = r.data?.device
    if (!apiDev) { console.warn('[devices] 后端未返回设备数据'); return false }
    const sub = apiDeviceToSub(apiDev)
    if (!sub) { console.warn('[devices] 后端返回数据格式错误'); return false }
    setSubDevices(prev => [...prev, sub])
    return true
  }, [])

  const removeSubDevice = useCallback(async (id: string) => {
    const current = subDevicesRef.current.find(device => device.id === id)
    if (!current) return false
    const r = await deleteDeviceApi(id, 1)
    if (!r.ok) { console.warn('[devices] 删除子设备失败:', r.error); return false }
    setSubDevices(prev => prev.filter(s => s.id !== id))
    setTasks(prev => prev.map(t => t.deviceIds.includes(id)
      ? { ...t, deviceIds: t.deviceIds.filter(deviceId => deviceId !== id) }
      : t))
    return true
  }, [])

  const [editingDevice, setEditingDevice] = useState<AppDevice | null>(null)

  const updateDevice = useCallback(async (id: string, updates: Partial<RadarDevice>) => {
    const current = devicesRef.current.find(d => d.id === id)
    if (!current) return false
    const merged = { ...current, ...updates }
    const r = await apiUpdateDevice(id, radarToApiPayload(id, merged))
    if (!r.ok) { console.warn('[devices] 更新雷达失败:', r.error); return false }
    const apiDev = r.data?.device
    const migrated = r.data?.migratedDeviceIds ?? []
    setDevices(prev => prev.map(d => {
      if (migrated.includes(d.id))
        return { ...d, endpointId: undefined }
      if (d.id === id) {
        const fromServer = apiDev ? apiDeviceToRadar(apiDev) : null
        return fromServer ?? { ...d, ...updates }
      }
      return d
    }))
    return true
  }, [])

  const updateSubDevice = useCallback(async (id: string, updates: Partial<SubDevice>) => {
    const current = subDevicesRef.current.find(s => s.id === id)
    if (!current) return false
    const merged = { ...current, ...updates }
    const r = await apiUpdateDevice(id, subToApiPayload(id, merged))
    if (!r.ok) { console.warn('[devices] 更新子设备失败:', r.error); return false }
    setSubDevices(prev => prev.map(s => s.id === id ? { ...s, ...updates } : s))
    return true
  }, [])

  return (
    <CoreProvider
      activeTaskId={activeTaskId}
      viewTaskRunId={viewTaskRunId}
      historyViewNotice={historyViewNotice}
      activeTaskDeviceIds={activeTaskDeviceIdList}
      tasksReady={tasksReady}
      devicesReady={devicesReady}
      defaultCheckedDeviceIds={defaultCheckedDeviceIds}
    >
      <div className="h-screen flex flex-col overflow-hidden" style={{ background: 'var(--canvas)' }}>
        <div className="flex-1 flex overflow-hidden">
          {sidebarOpen && (
            <Sidebar
              onClose={() => setSidebarOpen(false)}
              allDevices={visibleDevices}
              onAddDevice={addDevice}
              onRemoveDevice={removeDevice}
              onAddSubDevice={addSubDevice}
              onRemoveSubDevice={removeSubDevice}
              onEditDevice={setEditingDevice}
              activeTask={activeTask}
              viewTaskRunId={viewTaskRunId}
              tasks={tasks}
              deviceRegistryStatus={deviceRegistryStatus}
              deviceListEmpty={visibleDeviceListEmpty}
              onNewTask={() => setShowTaskDialog(true)}
              onCompleteTask={completeTask}
              onArchiveTask={archiveTask}
              onDeleteTask={deleteTask}
              onRerunTask={rerunTask}
              onViewTaskRun={viewTaskRun}
              onReturnToActiveView={returnToActiveView}
              canDeleteTask={canDeleteTask}
              taskActionPendingId={taskActionPendingId}
              taskActionError={taskActionError}
            />
          )}

          <main className="flex-1 flex flex-col relative overflow-hidden" style={{ background: 'var(--surfaceDim)' }}>
            <TitleBar
              activeTask={activeTask}
              viewTask={viewTask}
              isHistoryView={isHistoryView}
            />
            <div className="flex-1 relative overflow-hidden">
              <MapRadarView
                devices={displayRadarDevices}
                idleActionLabel={!activeTask && !isHistoryView
                  ? (lastReusableTask ? '再次执行上次任务' : '新建任务')
                  : undefined}
                idleActionPending={!!taskActionPendingId}
                idleActionError={taskActionError}
                onIdleAction={!activeTask && !isHistoryView
                  ? (lastReusableTask ? () => rerunTask(lastReusableTask.id) : () => setShowTaskDialog(true))
                  : undefined}
              />

              {!sidebarOpen && (
                <button
                  className="absolute top-4 left-3 z-20 p-2.5 rounded-xl border-none cursor-pointer
                             glass-panel transition-colors"
                  style={{ color: 'var(--onSurfaceVariant)' }}
                  onClick={() => setSidebarOpen(true)}
                >
                  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                    <path d="M3 12h18M3 6h18M3 18h18" strokeLinecap="round" />
                  </svg>
                </button>
              )}
            </div>

            <BottomPanel allDevices={visibleDevices} />
          </main>
        </div>

        <StatusBar activeTask={activeTask} />
      </div>

      {editingDevice && (
        <DeviceEditDialog
          device={editingDevice}
          radarDevices={devices}
          onUpdateDevice={updateDevice}
          onUpdateSubDevice={updateSubDevice}
          onClose={() => setEditingDevice(null)}
          controlTask={activeTask}
          onApplyControlSettings={applyRadarSettings}
          controlSettingsDisabled={radarSettingsDisabled || deviceRegistryStatus !== 'available'}
        />
      )}

      {showTaskDialog && (
        <TaskCreateDialog
          radarDevices={devices}
          taskTemplates={taskTemplates}
          onCreate={createTask}
          onCancel={() => setShowTaskDialog(false)}
        />
      )}

    </CoreProvider>
  )
}
