import { useState, useEffect } from 'react'
import { useCore } from '../../hooks/useCoreApi'

function useClock() {
  const [time, setTime] = useState(new Date())
  useEffect(() => {
    const id = setInterval(() => setTime(new Date()), 1000)
    return () => clearInterval(id)
  }, [])
  return time.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

export default function StatusBar() {
  const clock = useClock()
  const { trackRate, acquisition } = useCore()

  return (
    <footer
      className="h-6 flex items-center justify-between px-4 shrink-0 select-none z-20 text-[10px]"
      style={{
        background: 'var(--surfaceLow)',
        borderTop: '1px solid rgba(191, 201, 195, 0.3)',
        color: 'var(--onSurfaceVariant)',
        fontFamily: "'JetBrains Mono', monospace",
      }}
    >
      <div className="flex items-center gap-2">
        <span className="w-1.5 h-1.5 rounded-full animate-pulse"
          style={{ background: 'var(--primaryContainer)' }} />
        <span>DCMP v2.4.0 · 就绪</span>
      </div>
      <div className="flex gap-4 items-center">
        {acquisition && (
          <span className="flex items-center gap-1.5">
            <span>显示点数 {trackRate}</span>
            <span className="w-1.5 h-1.5 rounded-full"
              style={{ background: trackRate > 0 ? '#22c55e' : 'var(--hairlineStrong)' }} />
          </span>
        )}
        <span>{clock}</span>
      </div>
    </footer>
  )
}
