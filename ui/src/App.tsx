import { useState } from 'react'
import './styles/globals.css'
import TitleBar from './components/layout/TitleBar'
import ActivityBar from './components/layout/ActivityBar'
import FileExplorer from './components/layout/FileExplorer'
import RadarView from './components/layout/RadarView'
import BottomPanel from './components/panels/BottomPanel'
import StatusBar from './components/layout/StatusBar'
import { CoreProvider } from './hooks/useCoreApi'

export default function App() {
  const [sidebarOpen, setSidebarOpen] = useState(true)

  return (
    <CoreProvider>
      <div className="h-screen flex flex-col overflow-hidden select-none">
        <TitleBar />

        {/* Main area */}
        <main
          className="flex-1 flex relative overflow-hidden"
          style={{ background: 'var(--canvas)' }}
        >
          <ActivityBar onToggleSidebar={() => setSidebarOpen(!sidebarOpen)} />

          {/* Floating sidebar */}
          {sidebarOpen && (
            <div
              className="absolute top-4 bottom-4 z-20"
              style={{ left: '52px', width: '196px' }}
            >
              <FileExplorer onClose={() => setSidebarOpen(false)} />
            </div>
          )}

          {/* Radar + content area */}
          <div className="flex-1 relative overflow-hidden">
            <RadarView />

            {/* Bottom panel — positioned to clear sidebar */}
            <div
              className="absolute bottom-0 z-10 transition-all"
              style={{
                left: sidebarOpen ? '256px' : '4px',
                right: '4px',
              }}
            >
              <BottomPanel />
            </div>
          </div>
        </main>

        <StatusBar />
      </div>
    </CoreProvider>
  )
}
