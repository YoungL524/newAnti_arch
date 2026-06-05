import { useCallback, useEffect, useMemo, useState, useRef } from 'react'
import './styles/globals.css'
import TitleBar from './components/layout/TitleBar'
import Sidebar from './components/layout/Sidebar'
import MapRadarView from './components/map/MapRadarView'
import BottomPanel from './components/panels/BottomPanel'
import StatusBar from './components/layout/StatusBar'
import { CoreProvider } from './hooks/useCoreApi'
import { type AppDevice, type RadarDevice, type SubDevice, DEFAULT_DEVICE } from './components/map/types'
import DeviceEditDialog from './components/dialogs/DeviceEditDialog'
import { createDevice, deleteDevice, updateDevice as apiUpdateDevice } from './lib/devicesApi'
import TaskCreateDialog from './components/dialogs/TaskCreateDialog'
import type { Task } from './lib/taskTypes'
import { loadTasks, saveTasks, loadActiveTaskId, saveActiveTaskId } from './lib/taskTypes'

let deviceIdCounter = 0
function nextDeviceId() { return `dev_${Date.now()}_${++deviceIdCounter}` }
function nextSubId() { return `sub_${Date.now()}_${++deviceIdCounter}` }
function nextTaskId() { return `task_${Date.now()}_${++deviceIdCounter}` }

export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)
  const [devices, setDevices] = useState<RadarDevice[]>(() => [
    { id: nextDeviceId(), ...DEFAULT_DEVICE },
  ])
  const [subDevices, setSubDevices] = useState<SubDevice[]>([])
  const devicesLoadedRef = useRef(false)

  // ── Load devices from backend API on startup ──
  useEffect(() => {
    if (devicesLoadedRef.current) return
    devicesLoadedRef.current = true
    fetch('/api/devices')
      .then(r => r.json())
      .then(async (data: any) => {
        if (!data.ok || !data.devices) return
        const radars: RadarDevice[] = []
        const subs: SubDevice[] = []
        for (const item of data.devices) {
          if (item.kind === 0) {
            let faces: any[] = []
            try { faces = JSON.parse(item.facesJson || '[]') } catch {}
            radars.push({
              id: item.id,
              name: item.name,
              brand: item.brand || 'cetc38' as any,
              lng: item.lng,
              lat: item.lat,
              altitude: item.altitude,
              maxRange: item.maxRange || 5,
              faces: faces.length > 0 ? faces : [{ label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true }],
            })
          } else {
            const kinds = ['opto', 'level', 'gps'] as const
            subs.push({
              id: item.id,
              name: item.name,
              kind: kinds[item.kind - 1] ?? 'opto',
              brand: item.brand || 'cetc38' as any,
              linkedRadarId: item.linkedRadarId || undefined,
              mountOffsetAzimuth: item.mountOffsetAzimuth,
              coordinateSystem: item.coordinateSystem || 'WGS-84',
            })
          }
        }
        if (radars.length > 0 || subs.length > 0) {
          setDevices(radars)
          setSubDevices(subs)
        }
      })
      .catch(() => {
        // backend not available, keep default device
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
  const addDevice = useCallback((d: Omit<RadarDevice, 'id'>) => {
    const id = nextDeviceId()
    setDevices(prev => [...prev, { id, ...d }])
    if (activeTaskId) {
      setTasks(prev => prev.map(t =>
        t.id === activeTaskId ? { ...t, deviceIds: [...t.deviceIds, id] } : t))
    }
    createDevice({
      id, name: d.name, brand: d.brand, kind: 0,
      lng: d.lng, lat: d.lat, altitude: d.altitude,
      maxRange: d.maxRange, facesJson: JSON.stringify(d.faces),
    }).catch(() => {})
  }, [activeTaskId])

  const removeDevice = useCallback((id: string) => {
    setDevices(prev => prev.filter(d => d.id !== id))
    setSubDevices(prev => prev.filter(s => s.linkedRadarId !== id))
    setTasks(prev => prev.map(t => t.deviceIds.includes(id)
      ? { ...t, deviceIds: t.deviceIds.filter(did => did !== id) }
      : t))
    deleteDevice(id).catch(() => {})
  }, [])

  const addSubDevice = useCallback((d: Omit<SubDevice, 'id'>) => {
    const id = nextSubId()
    setSubDevices(prev => [...prev, { id, ...d }])
    if (activeTaskId) {
      setTasks(prev => prev.map(t =>
        t.id === activeTaskId ? { ...t, deviceIds: [...t.deviceIds, id] } : t))
    }
    const kindMap = { opto: 1, level: 2, gps: 3 } as const
    createDevice({
      id, name: d.name, brand: d.brand, kind: kindMap[d.kind],
      linkedRadarId: d.linkedRadarId, mountOffsetAzimuth: d.mountOffsetAzimuth,
      coordinateSystem: d.coordinateSystem,
    }).catch(() => {})
  }, [activeTaskId])

  const removeSubDevice = useCallback((id: string) => {
    setSubDevices(prev => prev.filter(s => s.id !== id))
    setTasks(prev => prev.map(t => t.deviceIds.includes(id)
      ? { ...t, deviceIds: t.deviceIds.filter(did => did !== id) }
      : t))
    deleteDevice(id).catch(() => {})
  }, [])

  const [editingDevice, setEditingDevice] = useState<AppDevice | null>(null)

  const updateDevice = useCallback((id: string, updates: Partial<RadarDevice>) => {
    setDevices(prev => prev.map(d => d.id === id ? { ...d, ...updates } : d))
    const payload: any = { ...updates }
    if (updates.faces) payload.facesJson = JSON.stringify(updates.faces)
    delete payload.faces
    apiUpdateDevice(id, payload).catch(() => {})
  }, [])

  const updateSubDevice = useCallback((id: string, updates: Partial<SubDevice>) => {
    setSubDevices(prev => prev.map(s => s.id === id ? { ...s, ...updates } : s))
    apiUpdateDevice(id, updates).catch(() => {})
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
