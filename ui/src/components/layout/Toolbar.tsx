export default function Toolbar() {
  return (
    <div
      className="flex items-center h-[34px] min-h-[34px] px-3 gap-0.5 select-none"
      style={{ background: '#111827', borderBottom: '1px solid #374151' }}
    >
      <ToolBtn title="主页">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M2 7l6-5 6 5v7H2V7z" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
          <path d="M6 14V8h4v6" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
        </svg>
      </ToolBtn>
      <ToolBtn title="保存">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M3 1h8l4 4v10H3V1z" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
          <path d="M5 1v5h5V1" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
          <path d="M5 11h6v4H5v-4z" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
        </svg>
      </ToolBtn>
      <ToolBtn title="上传">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M8 1v9M4 5l4-4 4 4" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
          <path d="M2 11v3h12v-3" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      </ToolBtn>

      <div className="w-[1px] h-[18px] mx-2" style={{ background: '#374151' }} />

      <ToolBtn title="搜索">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <circle cx="6.5" cy="6.5" r="4.5" stroke="currentColor" strokeWidth="1.3" />
          <path d="M10 10l4.5 4.5" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
      </ToolBtn>
      <ToolBtn title="放大">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <circle cx="7" cy="7" r="5" stroke="currentColor" strokeWidth="1.3" />
          <path d="M11 11l3.5 3.5" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
          <path d="M7 4v6M4 7h6" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
      </ToolBtn>
      <ToolBtn title="缩小">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <circle cx="7" cy="7" r="5" stroke="currentColor" strokeWidth="1.3" />
          <path d="M11 11l3.5 3.5" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
          <path d="M4 7h6" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
      </ToolBtn>

      <div className="w-[1px] h-[18px] mx-2" style={{ background: '#374151' }} />

      <ToolBtn title="撤销">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M5 3L1 6.5 5 10" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
          <path d="M2 6.5h8a4 4 0 010 8H7" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
      </ToolBtn>
      <ToolBtn title="重做">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M11 3l4 3.5L11 10" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
          <path d="M14 6.5H6a4 4 0 000 8h3" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
      </ToolBtn>
    </div>
  )
}

function ToolBtn({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <button
      title={title}
      className="w-[28px] h-[28px] flex items-center justify-center rounded cursor-pointer
                 hover:bg-[rgba(255,255,255,0.06)] active:bg-[rgba(255,255,255,0.10)]
                 transition-colors border-none"
      style={{ background: 'transparent', color: '#9ca3af' }}
    >
      {children}
    </button>
  )
}
