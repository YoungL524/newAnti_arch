import { useState } from 'react'

const buttons = [
  { label: 'Radar', render: RadarIcon },
  { label: 'Table', render: TableIcon },
  { label: 'Files', render: FolderIcon },
]

export default function ActivityBar({ onToggleSidebar }: { onToggleSidebar?: () => void }) {
  const [active, setActive] = useState(0)

  return (
    <aside
      className="w-10 shrink-0 flex flex-col items-center py-3 gap-3 select-none z-20"
      style={{
        background: 'var(--canvas)',
        borderRight: '1px solid var(--hairline)',
      }}
    >
      {buttons.map((btn, i) => {
        const isActive = active === i
        return (
          <button
            key={btn.label}
            title={btn.label}
            className="w-7 h-7 rounded flex items-center justify-center cursor-pointer border-none transition-colors"
            style={{
              background: isActive ? 'var(--surfaceHigh)' : 'transparent',
              color: isActive ? 'var(--primaryBright)' : 'var(--secondary)',
            }}
            onMouseEnter={(e) => {
              if (!isActive) {
                e.currentTarget.style.background = 'var(--surfaceLow)'
                e.currentTarget.style.color = 'var(--ink)'
              }
            }}
            onMouseLeave={(e) => {
              if (!isActive) {
                e.currentTarget.style.background = 'transparent'
                e.currentTarget.style.color = 'var(--secondary)'
              }
            }}
            onClick={() => {
              setActive(i)
              if (btn.label === 'Files' && onToggleSidebar) onToggleSidebar()
            }}
          >
            {btn.render()}
          </button>
        )
      })}
    </aside>
  )
}

function RadarIcon() {
  return (
    <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
      <circle cx="10" cy="10" r="9" stroke="currentColor" strokeWidth="1.3" />
      <circle cx="10" cy="10" r="6" stroke="currentColor" strokeWidth="1" />
      <circle cx="10" cy="10" r="3" stroke="currentColor" strokeWidth="1" />
      <path d="M10 10l5-5" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
    </svg>
  )
}

function TableIcon() {
  return (
    <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
      <rect x="2" y="3" width="16" height="14" rx="1.5" stroke="currentColor" strokeWidth="1.3" />
      <path d="M2 8h16M2 13h16M8 3v14M13 3v14" stroke="currentColor" strokeWidth="1.3" />
    </svg>
  )
}

function FolderIcon() {
  return (
    <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
      <path d="M2 4h6l2 2h8v12H2V4z" stroke="currentColor" strokeWidth="1.3" strokeLinejoin="round" />
    </svg>
  )
}
