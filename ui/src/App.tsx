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
import TaskCreateDialog from './components/dialogs/TaskCreateDialog'
import type { Task } from './lib/taskTypes'
import {
  fetchTasksFromBackend,
  createTaskApi,
  activateTaskApi,
  completeTaskApi,
  archiveTaskApi,
  deleteTaskApi,
  updateTaskApi,
  apiTaskToTask,
  taskToApiPayload,
} from './lib/tasksApi'

export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)
  const [deviceRegistryStatus, setDeviceRegistryStatus] = useState<DeviceRegistryStatus>('loading')
  const [devices, setDevices] = useState<RadarDevice[]>([])
  const [subDevices, setSubDevices] = useState<SubDevice[]>([])
  const devicesRef = useRef<RadarDevice[]>([])
  const subDevicesRef = useRef<SubDevice[]>([])

  const [tasks, setTasks] = useState<Task[]>([])
  const [activeTaskId, setActiveTaskId] = useState<string | null>(null)
  const [tasksBackend, setTasksBackend] = useState(true)
  const [showTaskDialog, setShowTaskDialog] = useState(false)
  const tasksRef = useRef<Task[]>([])
  const activeTaskIdRef = useRef<string | null>(null)

  useEffect(() => { devicesRef.current = devices }, [devices])
  useEffect(() => { subDevicesRef.current = subDevices }, [subDevices])
  useEffect(() => { tasksRef.current = tasks }, [tasks])
  useEffect(() => { activeTaskIdRef.current = activeTaskId }, [activeTaskId])

  const backendStatus = useMemo<'loading' | 'online' | 'offline'>(() => {
    if (deviceRegistryStatus === 'loading') return 'loading'
    if (deviceRegistryStatus === 'unavailable' && !tasksBackend) return 'offline'
    if (deviceRegistryStatus === 'available' || tasksBackend) return 'online'
    return 'offline'
  }, [deviceRegistryStatus, tasksBackend])

  const reloadBackendData = useCallback(() => {
    fetchDevices()
      .then(({ ok, radars, subs }) => {
        if (!ok) {
          setDevices([])
          setSubDevices([])
          setDeviceRegistryStatus('unavailable')
          return
        }
        setDevices(radars)
        setSubDevices(subs)
        setDeviceRegistryStatus('available')
      })
      .catch(() => {
        setDevices([])
        setSubDevices([])
        setDeviceRegistryStatus('unavailable')
      })

    fetchTasksFromBackend()
      .then(({ ok, tasks: loaded, activeTaskId: activeId }) => {
        setTasks(loaded)
        setActiveTaskId(activeId)
        setTasksBackend(ok)
      })
      .catch(() => setTasksBackend(false))
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

  const taskDeviceIds = useMemo(() => new Set(activeTask?.deviceIds ?? []), [activeTask])
  const taskDevices = useMemo(() =>
    activeTask ? devices.filter(d => taskDeviceIds.has(d.id)) : devices,
  [devices, activeTask, taskDeviceIds])
  const taskSubDevices = useMemo(() =>
    activeTask ? subDevices.filter(s => taskDeviceIds.has(s.id)) : subDevices,
  [subDevices, activeTask, taskDeviceIds])
  const allTaskDevices = useMemo(() => [...taskDevices, ...taskSubDevices], [taskDevices, taskSubDevices])
  const allDevices = useMemo(() => [...devices, ...subDevices], [devices, subDevices])
  const visibleDevices = activeTask ? allTaskDevices : allDevices
  const visibleDeviceListEmpty = visibleDevices.length === 0

  const syncTaskDevices = useCallback(async (taskId: string, deviceIds: string[]) => {
    if (!tasksBackend) {
      setTasks(prev => prev.map(t => t.id === taskId ? { ...t, deviceIds } : t))
      return true
    }
    const r = await updateTaskApi(taskId, { deviceIds })
    if (!r.ok) { console.warn('[tasks] 更新任务设备失败:', r.error); return false }
    if (r.data?.task) {
      const updated = apiTaskToTask(r.data.task)
      setTasks(prev => prev.map(t => t.id === taskId ? updated : t))
    }
    return true
  }, [tasksBackend])

  const createTask = useCallback(async (data: Omit<Task, 'id' | 'createdAt' | 'status'>) => {
    if (!tasksBackend) {
      const task: Task = { ...data, id: `task_${Date.now()}`, status: 'active', createdAt: Date.now() }
      setTasks(prev => [...prev, task])
      setActiveTaskId(task.id)
      setShowTaskDialog(false)
      return
    }
    const created = await createTaskApi(taskToApiPayload(data))
    if (!created.ok || !created.data?.task) {
      console.warn('[tasks] 创建失败:', created.error)
      return
    }
    const activated = await activateTaskApi(created.data.task.id)
    if (!activated.ok || !activated.data?.task) {
      console.warn('[tasks] 激活失败:', activated.error)
      return
    }
    const task = apiTaskToTask(activated.data.task)
    setTasks(prev => [...prev, task])
    setActiveTaskId(task.id)
    setShowTaskDialog(false)
  }, [tasksBackend])

  const completeTask = useCallback(async () => {
    const id = activeTaskIdRef.current
    if (!id) return
    if (tasksBackend) {
      const r = await completeTaskApi(id)
      if (!r.ok) { console.warn('[tasks] 完成任务失败:', r.error); return }
      if (r.data?.task) {
        const updated = apiTaskToTask(r.data.task)
        setTasks(prev => prev.map(t => t.id === id ? updated : t))
      }
    } else {
      setTasks(prev => prev.map(t =>
        t.id === id ? { ...t, status: 'completed' as const, completedAt: Date.now() } : t))
    }
    setActiveTaskId(null)
  }, [tasksBackend])

  const archiveTask = useCallback(async (taskId: string) => {
    if (tasksBackend) {
      const r = await archiveTaskApi(taskId)
      if (!r.ok) { console.warn('[tasks] 归档失败:', r.error); return }
      if (r.data?.task) {
        const updated = apiTaskToTask(r.data.task)
        setTasks(prev => prev.map(t => t.id === taskId ? updated : t))
      }
    } else {
      setTasks(prev => prev.map(t => t.id === taskId ? { ...t, status: 'archived' as const } : t))
    }
    if (activeTaskIdRef.current === taskId) setActiveTaskId(null)
  }, [tasksBackend])

  const deleteTask = useCallback(async (taskId: string) => {
    if (tasksBackend) {
      const r = await deleteTaskApi(taskId)
      if (!r.ok) { console.warn('[tasks] 删除失败:', r.error); return }
    }
    setTasks(prev => prev.filter(t => t.id !== taskId))
    if (activeTaskIdRef.current === taskId) setActiveTaskId(null)
  }, [tasksBackend])

  const addDevice = useCallback(async (d: Omit<RadarDevice, 'id'>) => {
    const r = await createDevice(radarToApiPayload('', d as RadarDevice))
    if (!r.ok) { console.warn('[devices] 添加设备失败:', r.error); return false }
    const apiDev = r.data?.device
    if (!apiDev) { console.warn('[devices] 后端未返回设备数据'); return false }
    const radar = apiDeviceToRadar(apiDev)
    if (!radar) { console.warn('[devices] 后端返回数据格式错误'); return false }
    setDevices(prev => [...prev, radar])
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
    setSubDevices(prev => prev.filter(s => s.linkedRadarId !== id))
    for (const t of tasksRef.current) {
      if (t.deviceIds.includes(id))
        await syncTaskDevices(t.id, t.deviceIds.filter(did => did !== id))
    }
    return true
  }, [syncTaskDevices])

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
    for (const t of tasksRef.current) {
      if (t.deviceIds.includes(id))
        await syncTaskDevices(t.id, t.deviceIds.filter(did => did !== id))
    }
    return true
  }, [syncTaskDevices])

  const [editingDevice, setEditingDevice] = useState<AppDevice | null>(null)

  const updateDevice = useCallback(async (id: string, updates: Partial<RadarDevice>) => {
    const current = devicesRef.current.find(d => d.id === id)
    if (!current) return false
    const merged = { ...current, ...updates }
    const r = await apiUpdateDevice(id, radarToApiPayload(id, merged))
    if (!r.ok) { console.warn('[devices] 更新雷达失败:', r.error); return false }
    setDevices(prev => prev.map(d => d.id === id ? { ...d, ...updates } : d))
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
    <CoreProvider>
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
              tasks={tasks}
              deviceRegistryStatus={deviceRegistryStatus}
              deviceListEmpty={visibleDeviceListEmpty}
              onNewTask={() => setShowTaskDialog(true)}
              onCompleteTask={completeTask}
              onArchiveTask={archiveTask}
              onDeleteTask={deleteTask}
            />
          )}

          <main className="flex-1 flex flex-col relative overflow-hidden" style={{ background: 'var(--surfaceDim)' }}>
            <TitleBar activeTask={activeTask} backendStatus={backendStatus} />
            <div className="flex-1 relative overflow-hidden">
              <MapRadarView devices={activeTask ? taskDevices : devices} />

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
          tasks={tasks}
          onCreate={createTask}
          onCancel={() => setShowTaskDialog(false)}
        />
      )}
    </CoreProvider>
  )
}
