export default function TabBar() {
  return (
    <div
      className="flex items-center h-[36px] min-h-[36px] select-none"
      style={{ background: '#111827', borderBottom: '1px solid #374151' }}
    >
      <div className="flex items-center h-full">
        {/* Active tab */}
        <div
          className="flex items-center h-full px-3 gap-2 text-[12px] cursor-pointer relative"
          style={{
            background: '#0b0f19',
            color: '#f3f4f6',
            borderRight: '1px solid #374151',
          }}
        >
          {/* Blue top border indicator */}
          <div
            className="absolute top-0 left-0 right-0 h-[2px]"
            style={{ background: '#3b82f6' }}
          />
          <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
            <circle cx="7" cy="7" r="5" stroke="#3b82f6" strokeWidth="1.2" />
            <path d="M3.5 7h7M7 3.5v7" stroke="#3b82f6" strokeWidth="1.2" strokeLinecap="round" />
          </svg>
          <span>雷达显示</span>
          <button
            className="w-[16px] h-[16px] flex items-center justify-center rounded
                       hover:bg-[rgba(255,255,255,0.08)] cursor-pointer border-none
                       text-[10px] leading-none"
            style={{ background: 'transparent', color: 'rgba(255,255,255,0.25)' }}
          >
            ✕
          </button>
        </div>
      </div>

      {/* New tab button */}
      <button
        className="w-[28px] h-[28px] flex items-center justify-center ml-1 rounded
                   cursor-pointer border-none text-[16px] font-light
                   hover:bg-[rgba(255,255,255,0.06)] transition-colors"
        style={{ background: 'transparent', color: 'rgba(255,255,255,0.35)' }}
        title="新建标签页"
      >
        +
      </button>

      <div className="flex-1" />

      {/* Right side actions */}
      <button
        className="h-full px-3 flex items-center gap-1.5 text-[11px] cursor-pointer border-none
                   hover:bg-[rgba(255,255,255,0.04)] transition-colors"
        style={{ background: 'transparent', color: '#9ca3af' }}
      >
        <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
          <rect x="1" y="1" width="12" height="12" rx="1" stroke="currentColor" strokeWidth="1.2" />
          <path d="M1 4h12" stroke="currentColor" strokeWidth="1.2" />
          <path d="M4 1v12" stroke="currentColor" strokeWidth="1.2" />
        </svg>
        <span>布局</span>
      </button>
    </div>
  )
}
