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
import { loadTasks, saveTasks, loadActiveTaskId, saveActiveTaskId } from './lib/taskTypes'

let deviceIdCounter = 0
function nextTaskId() { return `task_${Date.now()}_${++deviceIdCounter}` }

export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)
  const [deviceRegistryStatus, setDeviceRegistryStatus] = useState<DeviceRegistryStatus>('loading')
  const [devices, setDevices] = useState<RadarDevice[]>([])
  const [subDevices, setSubDevices] = useState<SubDevice[]>([])
  const devicesRef = useRef<RadarDevice[]>([])
  const subDevicesRef = useRef<SubDevice[]>([])
  const deviceListEmpty = devices.length === 0 && subDevices.length === 0

  // Keep refs in sync with state
  useEffect(() => { devicesRef.current = devices }, [devices])
  useEffect(() => { subDevicesRef.current = subDevices }, [subDevices])

  // ── Load devices from backend API on startup ──
  useEffect(() => {
    fetchDevices()
      .then(({ radars, subs }) => {
        if (radars.length > 0 || subs.length > 0) {
          setDevices(radars)
          setSubDevices(subs)
        }
        setDeviceRegistryStatus('available')
      })
      .catch(() => {
        setDeviceRegistryStatus('unavailable')
      })
  }, [])

  // ── Task state ──
  const [tasks, setTasks] = useState<Task[]>(loadTasks)
  const [activeTaskId, setActiveTaskId] = useState<string | null>(loadActiveTaskId)
  const [showTaskDialog, setShowTaskDialog] = useState(false)

  const activeTask = useMemo(() => tasks.find(t => t.id === activeTaskId) ?? null, [tasks, activeTaskId])

  // Persist tasks to localStorage
  useEffect(() => { saveTasks(tasks) }, [tasks])
  useEffect(() => { saveActiveTaskId(activeTaskId) }, [activeTaskId])

  // Devices visible in the current task context
  const taskDeviceIds = useMemo(() => new Set(activeTask?.deviceIds ?? []), [activeTask])
  const taskDevices = useMemo(() =>
    activeTask
      ? devices.filter(d => taskDeviceIds.has(d.id))
      : devices,
  [devices, activeTask, taskDeviceIds])
  const taskSubDevices = useMemo(() =>
    activeTask
      ? subDevices.filter(s => taskDeviceIds.has(s.id))
      : subDevices,
  [subDevices, activeTask, taskDeviceIds])
  const allTaskDevices = useMemo(() => [...taskDevices, ...taskSubDevices], [taskDevices, taskSubDevices])

  // ── Task operations ──
  const createTask = useCallback((data: Omit<Task, 'id' | 'createdAt' | 'status'>) => {
    const task: Task = {
      ...data,
      id: nextTaskId(),
      status: 'active',
      createdAt: Date.now(),
    }
    setTasks(prev => [...prev, task])
    setActiveTaskId(task.id)
    setShowTaskDialog(false)
  }, [])

  const completeTask = useCallback(() => {
    if (!activeTaskId) return
    setTasks(prev => prev.map(t =>
      t.id === activeTaskId ? { ...t, status: 'completed' as const, completedAt: Date.now() } : t))
    setActiveTaskId(null)
  }, [activeTaskId])

  const archiveTask = useCallback((taskId: string) => {
    setTasks(prev => prev.map(t => t.id === taskId ? { ...t, status: 'archived' as const } : t))
  }, [])

  const deleteTask = useCallback((taskId: string) => {
    setTasks(prev => prev.filter(t => t.id !== taskId))
    if (activeTaskId === taskId) setActiveTaskId(null)
  }, [activeTaskId])

  // ── Device operations (task-aware) ──
  const addDevice = useCallback(async (d: Omit<RadarDevice, 'id'>) => {
    const r = await createDevice(radarToApiPayload('', d as RadarDevice))
    if (!r.ok) { console.warn('[devices] 添加设备失败:', r.error); return false }
    const apiDev = r.data?.device
    if (!apiDev) { console.warn('[devices] 后端未返回设备数据'); return false }
    const radar = apiDeviceToRadar(apiDev)
    if (!radar) { console.warn('[devices] 后端返回数据格式错误'); return false }
    setDevices(prev => [...prev, radar])
    if (activeTaskId) {
      setTasks(prev => prev.map(t =>
        t.id === activeTaskId ? { ...t, deviceIds: [...t.deviceIds, radar.id] } : t))
    }
    return true
  }, [activeTaskId])

  const removeDevice = useCallback(async (id: string) => {
    const r = await deleteDeviceApi(id)
    if (!r.ok) { console.warn('[devices] 删除失败:', r.error); return false }
    setDevices(prev => prev.filter(d => d.id !== id))
    setSubDevices(prev => prev.filter(s => s.linkedRadarId !== id))
    setTasks(prev => prev.map(t => t.deviceIds.includes(id)
      ? { ...t, deviceIds: t.deviceIds.filter(did => did !== id) }
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
    if (activeTaskId) {
      setTasks(prev => prev.map(t =>
        t.id === activeTaskId ? { ...t, deviceIds: [...t.deviceIds, sub.id] } : t))
    }
    return true
  }, [activeTaskId])

  const removeSubDevice = useCallback(async (id: string) => {
    const r = await deleteDeviceApi(id)
    if (!r.ok) { console.warn('[devices] 删除子设备失败:', r.error); return false }
    setSubDevices(prev => prev.filter(s => s.id !== id))
    setTasks(prev => prev.map(t => t.deviceIds.includes(id)
      ? { ...t, deviceIds: t.deviceIds.filter(did => did !== id) }
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

  const allDevices = useMemo(() => [...devices, ...subDevices], [devices, subDevices])

  return (
    <CoreProvider>
      <div className="h-screen flex flex-col overflow-hidden" style={{ background: 'var(--canvas)' }}>
        <div className="flex-1 flex overflow-hidden">
          {sidebarOpen && (
            <Sidebar
              onClose={() => setSidebarOpen(false)}
              allDevices={activeTask ? allTaskDevices : allDevices}
              onAddDevice={addDevice}
              onRemoveDevice={removeDevice}
              onUpdateDevice={updateDevice}
              onAddSubDevice={addSubDevice}
              onRemoveSubDevice={removeSubDevice}
              onEditDevice={setEditingDevice}
              activeTask={activeTask}
              tasks={tasks}
              deviceRegistryStatus={deviceRegistryStatus}
              deviceListEmpty={deviceListEmpty}
              onNewTask={() => setShowTaskDialog(true)}
              onCompleteTask={completeTask}
              onArchiveTask={archiveTask}
              onDeleteTask={deleteTask}
            />
          )}

          <main className="flex-1 flex flex-col relative overflow-hidden" style={{ background: 'var(--surfaceDim)' }}>
            <TitleBar activeTask={activeTask} />
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

            <BottomPanel allDevices={activeTask ? allTaskDevices : allDevices} />
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
