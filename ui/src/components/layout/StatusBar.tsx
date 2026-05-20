import { useState, useEffect } from 'react'

function useClock() {
  const [time, setTime] = useState(new Date())
  useEffect(() => {
    const id = setInterval(() => setTime(new Date()), 1000)
    return () => clearInterval(id)
  }, [])
  return time.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

export default function StatusBar() {
  const clock = useClock()

  return (
    <footer
      className="h-6 flex items-center justify-between px-4 shrink-0 select-none z-20"
      style={{
        background: 'var(--canvas)',
        borderTop: '1px solid var(--hairline)',
        color: 'var(--secondary)',
        fontSize: '10px',
        fontFamily: "'JetBrains Mono', monospace",
      }}
    >
      <div className="flex items-center gap-2">
        <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--targetGrep)' }} />
        <span>就绪 | DCMP</span>
      </div>
      <div className="flex gap-4">
        <span>Dist: 2.3 km</span>
        <span>Az: 47.2°</span>
        <span>El: 12.5°</span>
        <span>Zoom: 100%</span>
        <span>{clock}</span>
      </div>
    </footer>
  )
}
