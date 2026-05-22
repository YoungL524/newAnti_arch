import { useState } from 'react'
import './styles/globals.css'
import TitleBar from './components/layout/TitleBar'
import Sidebar from './components/layout/Sidebar'
import MapRadarView from './components/map/MapRadarView'
import BottomPanel from './components/panels/BottomPanel'
import StatusBar from './components/layout/StatusBar'
import { CoreProvider } from './hooks/useCoreApi'

export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)

  return (
    <CoreProvider>
      <div className="h-screen flex flex-col overflow-hidden" style={{ background: 'var(--canvas)' }}>
        <div className="flex-1 flex overflow-hidden">
          {sidebarOpen && <Sidebar onClose={() => setSidebarOpen(false)} />}

          <main className="flex-1 flex flex-col relative overflow-hidden" style={{ background: 'var(--surfaceDim)' }}>
            <TitleBar />
            <div className="flex-1 relative overflow-hidden">
              <MapRadarView />

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

            <BottomPanel />
          </main>
        </div>

        <StatusBar />
      </div>
    </CoreProvider>
  )
}
