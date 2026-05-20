export default function QuickActions() {
  return (
    <div
      className="flex flex-col px-3 py-2 gap-1.5"
      style={{ borderTop: '1px solid #374151' }}
    >
      <QuickBtn>
        <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
          <path d="M7 1v12M1 7h12" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
        <span>新建任务</span>
      </QuickBtn>
      <QuickBtn>
        <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
          <path d="M2 3h10v8H2V3z" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
          <path d="M5 7h4M7 5v4" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
        <span>导入数据</span>
      </QuickBtn>
      <QuickBtn>
        <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
          <path d="M7 1v8M4 6l3 3 3-3" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
          <path d="M2 10v2h10v-2" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
        </svg>
        <span>导出报告</span>
      </QuickBtn>
    </div>
  )
}

function QuickBtn({ children }: { children: React.ReactNode }) {
  return (
    <button
      className="flex items-center gap-2 px-2.5 py-1.5 rounded text-[11px] cursor-pointer
                 border-none transition-colors"
      style={{
        background: 'rgba(255,255,255,0.03)',
        color: '#9ca3af',
      }}
    >
      {children}
    </button>
  )
}
