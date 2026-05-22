import { useState } from 'react'

interface NavItem {
  id: string; icon: string; label: string
}

const topNav: NavItem[] = [
  { id: 'resources', icon: 'inventory_2', label: '资源管理器' },
  { id: 'files', icon: 'folder_open', label: '项目文件' },
  { id: 'devices', icon: 'router', label: '设备列表' },
  { id: 'analytics', icon: 'analytics', label: '数据分析' },
  { id: 'archives', icon: 'history', label: '历史归档' },
]

export default function Sidebar({ onClose }: { onClose?: () => void }) {
  const [activeNav, setActiveNav] = useState('resources')

  return (
    <aside
      className="w-56 flex flex-col h-full py-4 px-3 gap-y-2 z-30 shrink-0 overflow-y-auto"
      style={{
        background: 'var(--surfaceLow)',
        borderRight: '1px solid rgba(191, 201, 195, 0.3)',
      }}
    >
      {/* Brand */}
      <div className="mb-3 px-1">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-[15px] font-bold tracking-tight" style={{ color: 'var(--primaryContainer)', fontFamily: "'Inter', sans-serif" }}>
              DCMP
            </h1>
            <p className="text-[9px] uppercase tracking-[0.18em] mt-0.5 opacity-60" style={{ color: 'var(--outline)', fontFamily: "'Inter', sans-serif" }}>
              系统注册表 V2.4.0
            </p>
          </div>
          {onClose && (
            <button onClick={onClose} className="border-none bg-transparent cursor-pointer p-1"
              style={{ color: 'var(--onSurfaceVariant)' }}>
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <path d="M18 6L6 18M6 6l12 12" strokeLinecap="round" />
              </svg>
            </button>
          )}
        </div>
      </div>

      {/* New Project button */}
      <button className="mb-3 mx-0 py-2 px-3 rounded-lg text-white font-medium text-[12px]
                         flex items-center justify-center gap-2 precision-shadow
                         hover:brightness-110 transition-all active:scale-[0.98]
                         border-none cursor-pointer"
        style={{ background: 'var(--primaryContainer)' }}>
        <span className="material-symbols-outlined !text-[16px]">add</span>
        新建项目
      </button>

      {/* Navigation */}
      <nav className="flex-1 space-y-0.5">
        {topNav.map((item) => (
          <NavButton
            key={item.id}
            icon={item.icon}
            label={item.label}
            active={activeNav === item.id}
            onClick={() => setActiveNav(item.id)}
          />
        ))}

        {/* Structure tree */}
        <div className="mt-5 px-1">
          <p className="text-[9px] uppercase tracking-[0.18em] mb-2 opacity-80 font-semibold"
            style={{ color: 'var(--outline)', fontFamily: "'Inter', sans-serif" }}>
            结构
          </p>
          <div className="space-y-1.5">
            <TreeNode icon="hub" label="全局集群" arrow="keyboard_arrow_down" />
            <TreeNode icon="sensors" label="Node_Alpha_01" arrow="keyboard_arrow_down" active depth={1} />
            <TreeNode icon="settings_input_component" label="Sensor_A1" depth={2} />
            <TreeNode icon="settings_input_component" label="Sensor_A2" depth={2} />
          </div>
        </div>
      </nav>

      {/* Bottom */}
      <div className="mt-auto pt-3 space-y-0.5">
        <button className="w-full px-2 py-1.5 rounded-md flex items-center gap-2
                           border-none bg-transparent cursor-pointer transition-colors
                           hover:bg-white/60"
          style={{ color: 'rgba(64, 73, 68, 0.6)' }}>
          <span className="material-symbols-outlined !text-[16px]">help</span>
          <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>帮助</span>
        </button>
        <button className="w-full px-2 py-1.5 rounded-md flex items-center gap-2
                           border-none bg-transparent cursor-pointer transition-colors
                           hover:bg-white/60"
          style={{ color: 'rgba(64, 73, 68, 0.6)' }}>
          <span className="material-symbols-outlined !text-[16px]">terminal</span>
          <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>系统日志</span>
        </button>
      </div>
    </aside>
  )
}

function NavButton({ icon, label, active, onClick }: {
  icon: string; label: string; active: boolean; onClick: () => void
}) {
  return (
    <button
      onClick={onClick}
      className={`w-full px-2 py-1.5 rounded-md flex items-center gap-2
                  border-none cursor-pointer transition-all ${active ? '' : 'hover:bg-white/60'}`}
      style={{
        background: active ? '#ffffff' : 'transparent',
        color: active ? 'var(--primaryContainer)' : 'var(--onSurfaceVariant)',
        fontWeight: active ? 600 : 400,
        boxShadow: active ? '0 1px 3px rgba(0,0,0,0.05)' : 'none',
        border: active ? '1px solid rgba(6, 78, 59, 0.05)' : '1px solid transparent',
      }}
    >
      <span className="material-symbols-outlined !text-[16px]">{icon}</span>
      <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>{label}</span>
    </button>
  )
}

function TreeNode({ icon, label, arrow, active, depth = 0 }: {
  icon: string; label: string; arrow?: string; active?: boolean; depth?: number
}) {
  return (
    <div
      className="flex items-center gap-3 cursor-pointer transition-colors group"
      style={{
        marginLeft: `${depth * 10}px`,
        color: active ? 'var(--primaryContainer)' : depth === 0 ? 'var(--onSurfaceVariant)' : 'rgba(64, 73, 68, 0.7)',
        fontWeight: active ? 600 : 400,
        borderLeft: depth > 0 ? '2px solid' : 'none',
        borderColor: depth === 1 ? 'rgba(6, 78, 59, 0.2)' : 'var(--stone200)',
        paddingLeft: depth > 0 ? '8px' : '0',
      }}
    >
      {arrow && <span className="material-symbols-outlined !text-[14px] opacity-40">{arrow}</span>}
      <span className="material-symbols-outlined !text-[16px]">{icon}</span>
      <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>{label}</span>
    </div>
  )
}
