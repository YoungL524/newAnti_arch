import { useCore } from '../../hooks/useCoreApi'

export default function TitleBar() {
  const { connected, acquisition } = useCore()

  return (
    <header
      className="flex items-center justify-between w-full h-11 px-4 shrink-0 select-none z-20"
      style={{
        background: 'var(--canvas)',
        borderBottom: '1px solid var(--hairline)',
      }}
    >
      {/* Brand */}
      <div className="flex items-center gap-2">
        <svg width="18" height="18" viewBox="0 0 22 22" fill="none">
          <circle cx="11" cy="11" r="10" stroke="#f54e00" strokeWidth="1.5" opacity="0.4" />
          <circle cx="11" cy="11" r="7" stroke="#f54e00" strokeWidth="1.2" opacity="0.6" />
          <circle cx="11" cy="11" r="3.5" stroke="#f54e00" strokeWidth="1" />
          <circle cx="11" cy="11" r="1.5" fill="#f54e00" />
        </svg>
        <span
          className="text-[18px] font-bold tracking-[-0.02em] leading-tight"
          style={{ color: 'var(--ink)' }}
        >
          DCMP
        </span>
      </div>

      {/* Right actions */}
      <div className="flex items-center gap-3">
        {/* Icons */}
        <span
          className="flex items-center justify-center w-7 h-7 rounded cursor-pointer transition-all hover:bg-[rgba(0,0,0,0.04)]"
          style={{ color: 'var(--secondary)' }}
          title="设置"
        >
          <svg width="16" height="16" viewBox="0 0 20 20" fill="none">
            <circle cx="10" cy="10" r="2.5" stroke="currentColor" strokeWidth="1.4" />
            <path d="M10 1v3M10 16v3M1 10h3M16 10h3M3.51 3.51l2.12 2.12M14.37 14.37l2.12 2.12M3.51 16.49l2.12-2.12M14.37 5.63l2.12-2.12"
                  stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
          </svg>
        </span>
        <span
          className="flex items-center justify-center w-7 h-7 rounded cursor-pointer transition-all hover:bg-[rgba(0,0,0,0.04)]"
          style={{ color: 'var(--secondary)' }}
          title="通知"
        >
          <svg width="16" height="16" viewBox="0 0 20 20" fill="none">
            <path d="M8 3h4l-1 7h-2L8 3z" stroke="currentColor" strokeWidth="1.4" strokeLinejoin="round" />
            <path d="M6 10h8v3a3 3 0 01-3 3H9a3 3 0 01-3-3v-3z" stroke="currentColor" strokeWidth="1.4" />
          </svg>
        </span>

        {/* Connection dot */}
        <span
          className="text-[10px]"
          style={{ color: connected ? 'var(--targetGrep)' : 'var(--error)' }}
        >
          ● {connected ? '已连接' : '未连接'}
        </span>

        {/* Action button */}
        <button
          className="text-white text-[12px] font-medium px-3 py-1.5 rounded cursor-pointer transition-colors flex items-center gap-1.5 border-none"
          style={{
            background: 'var(--primaryBright)',
          }}
          onMouseEnter={(e) => { e.currentTarget.style.background = 'var(--primary)' }}
          onMouseLeave={(e) => { e.currentTarget.style.background = 'var(--primaryBright)' }}
        >
          {acquisition ? '停止采集' : '开始采集'}
        </button>
      </div>
    </header>
  )
}
