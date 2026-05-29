import { useCallback, useMemo, useState } from 'react'
import './styles/globals.css'
import TitleBar from './components/layout/TitleBar'
import Sidebar from './components/layout/Sidebar'
import MapRadarView from './components/map/MapRadarView'
import BottomPanel from './components/panels/BottomPanel'
import StatusBar from './components/layout/StatusBar'
import { CoreProvider } from './hooks/useCoreApi'
import { type AppDevice, type RadarDevice, type SubDevice, DEFAULT_DEVICE } from './components/map/types'
import DeviceEditDialog from './components/dialogs/DeviceEditDialog'

let deviceIdCounter = 0
function nextDeviceId() { return `dev_${Date.now()}_${++deviceIdCounter}` }
function nextSubId() { return `sub_${Date.now()}_${++deviceIdCounter}` }

export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)
  const [devices, setDevices] = useState<RadarDevice[]>(() => [
    { id: nextDeviceId(), ...DEFAULT_DEVICE },
  ])
  const [subDevices, setSubDevices] = useState<SubDevice[]>([])

  const addDevice = useCallback((d: Omit<RadarDevice, 'id'>) => {
    setDevices(prev => [...prev, { id: nextDeviceId(), ...d }])
  }, [])

  const removeDevice = useCallback((id: string) => {
    setDevices(prev => prev.filter(d => d.id !== id))
    // also remove sub-devices linked to this radar
    setSubDevices(prev => prev.filter(s => s.linkedRadarId !== id))
  }, [])

  const addSubDevice = useCallback((d: Omit<SubDevice, 'id'>) => {
    setSubDevices(prev => [...prev, { id: nextSubId(), ...d }])
  }, [])

  const removeSubDevice = useCallback((id: string) => {
    setSubDevices(prev => prev.filter(s => s.id !== id))
  }, [])

  const [editingDevice, setEditingDevice] = useState<AppDevice | null>(null)

  const updateDevice = useCallback((id: string, updates: Partial<RadarDevice>) => {
    setDevices(prev => prev.map(d => d.id === id ? { ...d, ...updates } : d))
  }, [])

  const updateSubDevice = useCallback((id: string, updates: Partial<SubDevice>) => {
    setSubDevices(prev => prev.map(s => s.id === id ? { ...s, ...updates } : s))
  }, [])

  const allDevices = useMemo(() => [...devices, ...subDevices], [devices, subDevices])

  return (
    <CoreProvider>
      <div className="h-screen flex flex-col overflow-hidden" style={{ background: 'var(--canvas)' }}>
        <div className="flex-1 flex overflow-hidden">
          {sidebarOpen && (
            <Sidebar
              onClose={() => setSidebarOpen(false)}
              allDevices={allDevices}
              onAddDevice={addDevice}
              onRemoveDevice={removeDevice}
              onUpdateDevice={updateDevice}
              onAddSubDevice={addSubDevice}
              onRemoveSubDevice={removeSubDevice}
              onEditDevice={setEditingDevice}
            />
          )}

          <main className="flex-1 flex flex-col relative overflow-hidden" style={{ background: 'var(--surfaceDim)' }}>
            <TitleBar />
            <div className="flex-1 relative overflow-hidden">
              <MapRadarView devices={devices} />

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

            <BottomPanel allDevices={allDevices} />
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
    </CoreProvider>
  )
}
