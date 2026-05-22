export default function ActivityBar({
  onToggleSidebar,
}: {
  onToggleSidebar?: () => void
}) {
  return (
    <aside
      className="w-10 shrink-0 flex flex-col items-center py-3 gap-3 select-none z-20"
      style={{
        background: 'var(--canvas)',
        borderRight: '1px solid var(--hairline)',
      }}
    >
      <button
        title="资源管理器"
        className="w-7 h-7 rounded flex items-center justify-center cursor-pointer border-none transition-colors
                   hover:bg-[var(--surfaceLow)] hover:text-[var(--ink)]"
        style={{ background: 'transparent', color: 'var(--secondary)' }}
        onClick={onToggleSidebar}
      >
        <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
          <path d="M2 4h6l2 2h8v12H2V4z" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
        </svg>
      </button>
    </aside>
  )
}
