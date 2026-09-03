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
import type { Task } from './lib/taskTypes'
import {
  fetchTasksFromBackend,
  createTaskApi,
  activateTaskApi,
  rerunTaskApi,
  completeTaskApi,
  archiveTaskApi,
  deleteTaskApi,
  updateTaskApi,
  apiTaskToTask,
  taskToApiPayload,
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
  const [coreBackendAvailable, setCoreBackendAvailable] = useState<boolean | null>(null)
  const [showTaskDialog, setShowTaskDialog] = useState(false)
  const [taskActionPendingId, setTaskActionPendingId] = useState<string | null>(null)
  const [taskActionError, setTaskActionError] = useState('')
  const tasksRef = useRef<Task[]>([])
  const activeTaskIdRef = useRef<string | null>(null)

  useEffect(() => { devicesRef.current = devices }, [devices])
  useEffect(() => { subDevicesRef.current = subDevices }, [subDevices])
  useEffect(() => { tasksRef.current = tasks }, [tasks])
  useEffect(() => { activeTaskIdRef.current = activeTaskId }, [activeTaskId])

  const backendStatus = useMemo<'loading' | 'online' | 'offline'>(() => {
    if (coreBackendAvailable === null) return 'loading'
    return coreBackendAvailable ? 'online' : 'offline'
  }, [coreBackendAvailable])

  useEffect(() => {
    let cancelled = false
    const checkCoreBackend = () => {
      fetch('/api/status')
        .then(response => {
          if (!cancelled) setCoreBackendAvailable(response.ok)
        })
        .catch(() => {
          if (!cancelled) setCoreBackendAvailable(false)
        })
    }
    checkCoreBackend()
    const id = setInterval(checkCoreBackend, 5000)
    return () => {
      cancelled = true
      clearInterval(id)
    }
  }, [])

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
    tasks.find(t => t.status === 'completed' || t.status === 'archived') ?? null,
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

  const syncTaskDevices = useCallback(async (taskId: string, deviceIds: string[]) => {
    if (!tasksBackend) {
      setTaskActionError('核心服务未连接，无法修改任务设备')
      return false
    }
    const r = await updateTaskApi(taskId, { deviceIds })
    if (!r.ok) {
      const error = r.error || '更新任务设备失败'
      console.warn('[tasks] 更新任务设备失败:', error)
      setTaskActionError(error)
      return false
    }
    if (r.data?.task) {
      const updated = apiTaskToTask(r.data.task)
      setTasks(prev => prev.map(t => t.id === taskId ? updated : t))
    }
    return true
  }, [tasksBackend])

  const createTask = useCallback(async (data: Omit<Task, 'id' | 'createdAt' | 'status'>): Promise<TaskCreateResult> => {
    setTaskActionError('')
    if (!tasksBackend) {
      return { ok: false, error: '核心服务未连接，无法创建任务' }
    }
    if (activeTaskIdRef.current) {
      return { ok: false, error: '已有进行中的任务，请先完成当前任务' }
    }
    const created = await createTaskApi(taskToApiPayload(data))
    if (!created.ok || !created.data?.task) {
      console.warn('[tasks] 创建失败:', created.error)
      return { ok: false, error: created.error || '任务创建失败' }
    }
    const activated = await activateTaskApi(created.data.task.id)
    if (!activated.ok || !activated.data?.task) {
      console.warn('[tasks] 激活失败:', activated.error)
      const draftTask = apiTaskToTask(created.data.task)
      setTasks(prev => prev.some(t => t.id === draftTask.id) ? prev.map(t => t.id === draftTask.id ? draftTask : t) : [...prev, draftTask])
      return { ok: false, error: `任务已创建为草稿，但激活失败：${activated.error || '请确认任务已绑定可控雷达'}` }
    }
    const task = apiTaskToTask(activated.data.task)
    setTasks(prev => prev.some(t => t.id === task.id) ? prev.map(t => t.id === task.id ? task : t) : [...prev, task])
    setActiveTaskId(task.id)
    setViewTaskRunId(task.id)
    setShowTaskDialog(false)
    return { ok: true }
  }, [tasksBackend])

  const rerunTask = useCallback(async (sourceTaskId: string) => {
    if (activeTaskIdRef.current || taskActionPendingId) return
    setTaskActionPendingId(sourceTaskId)
    setTaskActionError('')
    try {
      if (!tasksBackend) {
        throw new Error('核心服务未连接，无法再次执行任务')
      }

      const r = await rerunTaskApi(sourceTaskId)
      if (!r.ok || !r.data?.task)
        throw new Error(r.error || '再次执行任务失败')
      const task = apiTaskToTask(r.data.task)
      setTasks(prev => [task, ...prev.filter(t => t.id !== task.id)])
      setActiveTaskId(task.id)
      setViewTaskRunId(task.id)
    } catch (e: any) {
      setTaskActionError(e?.message || '再次执行任务失败')
    } finally {
      setTaskActionPendingId(null)
    }
  }, [tasksBackend, taskActionPendingId])

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

  const archiveTask = useCallback(async (taskId: string) => {
    setTaskActionError('')
    if (!tasksBackend) {
      setTaskActionError('核心服务未连接，无法归档任务')
      return
    }
    const r = await archiveTaskApi(taskId)
    if (!r.ok) { setTaskActionError(r.error || '归档失败'); return }
    if (r.data?.task) {
      const updated = apiTaskToTask(r.data.task)
      setTasks(prev => prev.map(t => t.id === taskId ? updated : t))
    }
    if (activeTaskIdRef.current === taskId) setActiveTaskId(null)
    if (viewTaskRunId === taskId) {
      setViewTaskRunId(activeTaskIdRef.current === taskId ? null : activeTaskIdRef.current)
    }
  }, [tasksBackend, viewTaskRunId])

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
    const tid = activeTaskIdRef.current
    if (tid) {
      const task = tasksRef.current.find(t => t.id === tid)
      if (task) await syncTaskDevices(tid, [...task.deviceIds, radar.id])
    }
    return true
  }, [syncTaskDevices])

  const removeDevice = useCallback(async (id: string) => {
    const r = await deleteDeviceApi(id)
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
    const tid = activeTaskIdRef.current
    if (tid) {
      const task = tasksRef.current.find(t => t.id === tid)
      if (task) await syncTaskDevices(tid, [...task.deviceIds, sub.id])
    }
    return true
  }, [syncTaskDevices])

  const removeSubDevice = useCallback(async (id: string) => {
    const r = await deleteDeviceApi(id)
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
              onUpdateDevice={updateDevice}
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
            <TitleBar activeTask={activeTask} viewTask={viewTask} isHistoryView={isHistoryView} backendStatus={backendStatus} />
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

        <StatusBar />
      </div>

      {editingDevice && (
        <DeviceEditDialog
          device={editingDevice}
          radarDevices={devices}
          onUpdateDevice={updateDevice}
          onUpdateSubDevice={updateSubDevice}
          onClose={() => setEditingDevice(null)}
        />
      )}

      {showTaskDialog && (
        <TaskCreateDialog
          radarDevices={devices}
          onCreate={createTask}
          onCancel={() => setShowTaskDialog(false)}
        />
      )}
    </CoreProvider>
  )
}
