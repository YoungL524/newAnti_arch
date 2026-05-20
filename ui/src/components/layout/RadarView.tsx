import { useEffect, useRef, useCallback } from 'react'

interface Point {
  angle: number
  dist: number
  color: string
  label: string
}

const points: Point[] = [
  { angle: 0.3,  dist: 0.40, color: '#9fc9a2', label: 'T-001' },
  { angle: 1.2,  dist: 0.65, color: '#9fc9a2', label: 'T-002' },
  { angle: 2.1,  dist: 0.35, color: '#dfa88f', label: 'T-004' },
  { angle: 3.0,  dist: 0.72, color: '#9fbbe0', label: 'T-003' },
  { angle: 4.5,  dist: 0.50, color: '#9fc9a2', label: 'T-005' },
  { angle: 5.2,  dist: 0.25, color: '#dfa88f', label: 'T-006' },
  { angle: 0.8,  dist: 0.85, color: '#9fbbe0', label: 'T-007' },
]

export default function RadarView() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const sweepRef = useRef(0)
  const animRef = useRef(0)

  const draw = useCallback(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const w = canvas.width
    const h = canvas.height
    const cx = w / 2
    const cy = h / 2
    const radius = Math.min(cx, cy) - 30

    sweepRef.current += 0.005
    if (sweepRef.current > Math.PI * 2) sweepRef.current -= Math.PI * 2
    const sweep = sweepRef.current

    ctx.clearRect(0, 0, w, h)

    // Canvas background
    ctx.fillStyle = '#f7f7f4'
    ctx.fillRect(0, 0, w, h)

    // PPI disc
    const disc = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
    disc.addColorStop(0, '#ffffff')
    disc.addColorStop(0.6, '#f9f9f6')
    disc.addColorStop(1, '#f0efec')
    ctx.fillStyle = disc
    ctx.beginPath()
    ctx.arc(cx, cy, radius, 0, Math.PI * 2)
    ctx.fill()

    // Concentric rings
    for (let i = 1; i <= 5; i++) {
      const r = radius * i / 5
      ctx.beginPath()
      ctx.arc(cx, cy, r, 0, Math.PI * 2)
      ctx.strokeStyle = i === 5 ? '#cfcdc4' : '#e6e5e0'
      ctx.lineWidth = i === 5 ? 1 : 0.5
      ctx.stroke()

      if (i === 5) {
        for (let deg = 0; deg < 360; deg += 10) {
          const rad = (deg - 90) * Math.PI / 180
          const len = deg % 30 === 0 ? 7 : 3.5
          ctx.beginPath()
          ctx.moveTo(cx + Math.cos(rad) * (radius - len), cy + Math.sin(rad) * (radius - len))
          ctx.lineTo(cx + Math.cos(rad) * radius, cy + Math.sin(rad) * radius)
          ctx.strokeStyle = deg % 30 === 0 ? '#cfcdc4' : '#e6e5e0'
          ctx.lineWidth = deg % 30 === 0 ? 1 : 0.5
          ctx.stroke()
        }
      }
    }

    // Crosshairs
    ctx.strokeStyle = '#e6e5e0'
    ctx.lineWidth = 0.5
    ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(w, cy); ctx.stroke()
    ctx.beginPath(); ctx.moveTo(cx, 0); ctx.lineTo(cx, h); ctx.stroke()
    ctx.beginPath(); ctx.moveTo(cx - radius * 0.7, cy - radius * 0.7); ctx.lineTo(cx + radius * 0.7, cy + radius * 0.7); ctx.stroke()
    ctx.beginPath(); ctx.moveTo(cx + radius * 0.7, cy - radius * 0.7); ctx.lineTo(cx - radius * 0.7, cy + radius * 0.7); ctx.stroke()

    // Angle labels
    ctx.fillStyle = '#605e56'
    ctx.font = '10px Inter, sans-serif'
    ctx.textAlign = 'center'
    ctx.textBaseline = 'middle'
    ctx.fillText('0°', cx, cy - radius - 14)
    ctx.fillText('90°', cx + radius + 18, cy)
    ctx.fillText('180°', cx, cy + radius + 16)
    ctx.fillText('270°', cx - radius - 18, cy)
    ctx.font = '9px Inter, sans-serif'
    const d45 = radius * 0.707
    ctx.fillText('45°', cx + d45 + 16, cy - d45 - 10)
    ctx.fillText('135°', cx + d45 + 16, cy + d45 + 12)
    ctx.fillText('225°', cx - d45 - 16, cy + d45 + 12)
    ctx.fillText('315°', cx - d45 - 16, cy - d45 - 10)

    // Range labels
    ctx.fillStyle = '#a09c94'
    ctx.font = '9px Inter, sans-serif'
    ctx.textAlign = 'left'
    ctx.textBaseline = 'top'
    for (let i = 1; i <= 4; i++) {
      const r = radius * i / 5
      ctx.fillText(`${i * 0.5}km`, cx + 4, cy - r + 2)
    }

    // Sweep gradient trail
    const sweepStart = sweep - Math.PI / 2 - 0.5
    const sweepEnd = sweep - Math.PI / 2 + 0.3
    const grad = ctx.createConicGradient(sweepStart, cx, cy)
    grad.addColorStop(0, 'transparent')
    grad.addColorStop(0.06, 'rgba(245,78,0,0.03)')
    grad.addColorStop(0.14, 'rgba(245,78,0,0.08)')
    grad.addColorStop(0.18, 'rgba(245,78,0,0.16)')
    grad.addColorStop(0.21, 'rgba(168,51,0,0.10)')
    grad.addColorStop(0.26, 'transparent')
    grad.addColorStop(1, 'transparent')

    ctx.beginPath()
    ctx.moveTo(cx, cy)
    ctx.arc(cx, cy, radius, sweepStart, sweepEnd)
    ctx.closePath()
    ctx.fillStyle = grad
    ctx.fill()

    // Sweep line
    ctx.beginPath()
    ctx.moveTo(cx, cy)
    ctx.lineTo(
      cx + Math.cos(sweep - Math.PI / 2) * radius,
      cy + Math.sin(sweep - Math.PI / 2) * radius,
    )
    ctx.strokeStyle = 'rgba(245,78,0,0.55)'
    ctx.lineWidth = 1.5
    ctx.stroke()

    // Sweep endpoint glow
    const epX = cx + Math.cos(sweep - Math.PI / 2) * radius
    const epY = cy + Math.sin(sweep - Math.PI / 2) * radius
    const epGlow = ctx.createRadialGradient(epX, epY, 0, epX, epY, 10)
    epGlow.addColorStop(0, 'rgba(245,78,0,0.4)')
    epGlow.addColorStop(1, 'rgba(245,78,0,0)')
    ctx.fillStyle = epGlow
    ctx.beginPath()
    ctx.arc(epX, epY, 10, 0, Math.PI * 2)
    ctx.fill()

    // Center dot
    ctx.beginPath()
    ctx.arc(cx, cy, 2, 0, Math.PI * 2)
    ctx.fillStyle = '#26251e'
    ctx.fill()

    // Data points
    for (const p of points) {
      const px = cx + Math.cos(p.angle - Math.PI / 2) * radius * p.dist
      const py = cy + Math.sin(p.angle - Math.PI / 2) * radius * p.dist

      const glow = ctx.createRadialGradient(px, py, 0, px, py, 14)
      glow.addColorStop(0, `${p.color}40`)
      glow.addColorStop(1, 'transparent')
      ctx.fillStyle = glow
      ctx.beginPath()
      ctx.arc(px, py, 14, 0, Math.PI * 2)
      ctx.fill()

      // Dot
      ctx.beginPath()
      ctx.arc(px, py, 4, 0, Math.PI * 2)
      ctx.fillStyle = p.color
      ctx.fill()
      ctx.beginPath()
      ctx.arc(px, py, 4, 0, Math.PI * 2)
      ctx.strokeStyle = 'rgba(255,255,255,0.9)'
      ctx.lineWidth = 1
      ctx.stroke()

      // Label
      ctx.fillStyle = '#26251e'
      ctx.font = 'bold 9px Inter, sans-serif'
      ctx.textAlign = 'left'
      ctx.textBaseline = 'bottom'
      ctx.fillText(p.label, px + 6, py - 4)
    }

    animRef.current = requestAnimationFrame(draw)
  }, [])

  // Canvas resize
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return

    const dpr = window.devicePixelRatio || 1
    const resize = () => {
      canvas.width = window.innerWidth * dpr
      canvas.height = window.innerHeight * dpr
      canvas.style.width = `${window.innerWidth}px`
      canvas.style.height = `${window.innerHeight}px`
    }
    resize()
    window.addEventListener('resize', resize)
    return () => window.removeEventListener('resize', resize)
  }, [])

  // Animation
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const dpr = window.devicePixelRatio || 1
    ctx.scale(dpr, dpr)

    animRef.current = requestAnimationFrame(draw)
    return () => cancelAnimationFrame(animRef.current)
  }, [draw])

  return (
    <div className="absolute inset-0 overflow-hidden">
      <canvas ref={canvasRef} className="block" />

      {/* Floating header bar — positioned after sidebar */}
      <div
        className="absolute top-4 h-10 flex items-center px-4 rounded z-10 shadow-sm"
        style={{
          left: '272px',
          right: '16px',
          background: 'rgba(255,255,255,0.9)',
          backdropFilter: 'blur(10px)',
          border: '1px solid var(--hairline)',
        }}
      >
        <span className="text-[12px]" style={{ color: 'var(--secondary)', fontFamily: "'JetBrains Mono', monospace" }}>
          Monitoring Area: Sector Alpha
        </span>
        <div className="flex-1" />
        <span className="flex items-center gap-1 text-[12px] font-medium" style={{ color: 'var(--targetGrep)', fontFamily: "'JetBrains Mono', monospace" }}>
          <span className="w-2 h-2 rounded-full" style={{ background: 'var(--targetGrep)' }} />
          LIVE
        </span>
      </div>

      {/* Radar info — top-left of PPI area */}
      <div
        className="absolute top-[60px] left-4 rounded p-4 z-10 shadow-sm w-52"
        style={{
          background: 'rgba(255,255,255,0.9)',
          backdropFilter: 'blur(10px)',
          border: '1px solid var(--hairline)',
        }}
      >
        <div className="flex items-center gap-2 mb-3">
          <span className="w-2 h-2 rounded-full" style={{ background: 'var(--targetGrep)' }} />
          <span className="text-[13px] font-medium" style={{ color: 'var(--ink)' }}>雷达运行中</span>
        </div>
        <div className="flex flex-col gap-1 font-mono text-[11px]" style={{ color: 'var(--secondary)' }}>
          <div>PRF: 1200 Hz | PW: 1.0 μs</div>
          <div>距离范围: 0–2 km</div>
          <div className="flex items-center gap-2" style={{ color: 'var(--ink)' }}>
            检测目标: <span className="font-bold" style={{ color: 'var(--primaryBright)' }}>7</span>
            &nbsp;|&nbsp; 跟踪: <span className="font-bold" style={{ color: 'var(--targetGrep)' }}>5</span>
          </div>
        </div>
      </div>

      {/* Legend — top-right of PPI area */}
      <div
        className="absolute top-[60px] right-4 rounded p-3 z-10 shadow-sm flex gap-4"
        style={{
          background: 'rgba(255,255,255,0.9)',
          backdropFilter: 'blur(10px)',
          border: '1px solid var(--hairline)',
        }}
      >
        <LegendDot color="var(--targetGrep)" label="跟踪" />
        <LegendDot color="var(--targetRead)" label="目标" />
        <LegendDot color="var(--targetThinking)" label="干扰" />
      </div>
    </div>
  )
}

function LegendDot({ color, label }: { color: string; label: string }) {
  return (
    <div className="flex items-center gap-1.5 text-[12px]" style={{ color: 'var(--secondary)' }}>
      <span className="w-2 h-2 rounded-full" style={{ background: color }} />
      {label}
    </div>
  )
}
