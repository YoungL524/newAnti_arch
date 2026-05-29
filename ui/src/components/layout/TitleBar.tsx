import { useCore } from '../../hooks/useCoreApi'

export default function TitleBar() {
  const { connected, acquisition, toggleAcquisition } = useCore()

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

      <button
        className="px-3.5 py-1 rounded-lg text-white font-semibold text-[11px] border-none cursor-pointer
                   precision-shadow active:scale-95 transition-all"
        style={{ background: 'var(--primaryContainer)' }}
        onClick={toggleAcquisition}
      >
        {acquisition ? '停止采集' : '开始采集'}
      </button>
    </header>
  )
}
