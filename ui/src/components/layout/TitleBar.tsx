import { useCore } from '../../hooks/useCoreApi'

export default function TitleBar() {
  const { connected, acquisition } = useCore()

  return (
    <header className="h-11 glass-panel flex justify-between items-center w-full px-4 z-20 shrink-0"
      style={{ borderBottom: '1px solid rgba(229, 231, 226, 0.5)' }}
    >
      <div className="flex items-center gap-3">
        <div className="flex items-center gap-2 px-2.5 py-0.5 rounded-full border"
          style={{
            background: 'var(--emeraldSubtle)',
            borderColor: 'rgba(6, 78, 59, 0.1)',
          }}>
          <span className="w-1.5 h-1.5 rounded-full animate-pulse"
            style={{
              background: connected ? 'var(--primaryContainer)' : 'var(--error)',
              boxShadow: connected ? '0 0 8px rgba(6,78,59,0.5)' : 'none',
            }} />
          <span className="text-[10px] font-bold tracking-wider"
            style={{ color: 'var(--primaryContainer)', fontFamily: "'Inter', sans-serif" }}>
            {connected ? '实时传输中' : '已断开'}
          </span>
        </div>
      </div>

      <div className="flex items-center gap-3">
        <div className="flex items-center gap-1">
          <IconBtn title="连接">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
              <path d="M1 1l22 22M16.72 11.06A10.94 10.94 0 0119 12.55M5 12.55a10.94 10.94 0 015.17-2.39M8.63 3.7A16.94 16.94 0 0112 3c4.97 0 9.52 2.14 12 5.55M3.95 7.06A19.03 19.03 0 016.78 5.2"
                strokeLinecap="round" strokeLinejoin="round" />
              <path d="M12 20a3 3 0 100-6 3 3 0 000 6z" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </IconBtn>
          <IconBtn title="主题">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
              <circle cx="12" cy="12" r="5" />
              <path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42" />
            </svg>
          </IconBtn>
          <IconBtn title="设置">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
              <circle cx="12" cy="12" r="3" />
              <path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z" />
            </svg>
          </IconBtn>
        </div>
        <button
          className="px-3.5 py-1 rounded-lg text-white font-semibold text-[11px] border-none cursor-pointer
                     precision-shadow active:scale-95 transition-all"
          style={{ background: 'var(--primaryContainer)' }}
        >
          {acquisition ? '停止采集' : '开始采集'}
        </button>
        <div className="w-7 h-7 rounded-lg overflow-hidden border p-0.5 bg-white"
          style={{ borderColor: 'var(--stone200)' }}>
          <div className="w-full h-full rounded-md flex items-center justify-center text-[10px] font-bold"
            style={{ background: 'var(--surfaceLow)', color: 'var(--primaryContainer)' }}>
            OP
          </div>
        </div>
      </div>
    </header>
  )
}

function IconBtn({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <button
      title={title}
      className="p-1.5 rounded-md flex items-center border-none cursor-pointer transition-colors"
      style={{ color: 'var(--onSurfaceVariant)', background: 'transparent' }}
    >
      {children}
    </button>
  )
}
