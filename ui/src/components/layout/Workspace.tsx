import { useEffect, useRef } from 'react'

export default function Workspace() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const phaseRef = useRef(0)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    let animId: number
    const draw = () => {
      phaseRef.current += 0.025
      if (phaseRef.current > 6.2832) phaseRef.current -= 6.2832

      const w = canvas.width
      const h = canvas.height

      // Base
      ctx.fillStyle = '#0F1218'
      ctx.fillRect(0, 0, w, h)

      // Breathing glow — center top
      const breath = 0.6 + 0.25 * Math.sin(phaseRef.current)
      const alpha = Math.floor(28 * breath)
      const cx = w * 0.5
      const cy = h * 0.18
      const radius = Math.max(w, h) * 0.7

      const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
      g.addColorStop(0, `rgba(18, 52, 96, ${alpha / 255})`)
      g.addColorStop(0.4, `rgba(18, 52, 96, ${alpha / 3 / 255})`)
      g.addColorStop(0.7, 'rgba(15, 18, 24, 0)')
      g.addColorStop(1, 'rgba(15, 18, 24, 0)')
      ctx.fillStyle = g
      ctx.fillRect(0, 0, w, h)

      // Second glow — right offset
      const g2 = ctx.createRadialGradient(
        w * 0.7, h * 0.3, 0,
        w * 0.7, h * 0.3, radius * 2 / 3,
      )
      g2.addColorStop(0, `rgba(18, 52, 96, ${alpha / 4 / 255})`)
      g2.addColorStop(1, 'rgba(15, 18, 24, 0)')
      ctx.fillStyle = g2
      ctx.fillRect(0, 0, w, h)

      animId = requestAnimationFrame(draw)
    }
    draw()

    return () => cancelAnimationFrame(animId)
  }, [])

  const resize = () => {
    const canvas = canvasRef.current
    if (!canvas) return
    const rect = canvas.parentElement!.getBoundingClientRect()
    canvas.width = rect.width
    canvas.height = rect.height
  }

  useEffect(() => {
    resize()
    window.addEventListener('resize', resize)
    return () => window.removeEventListener('resize', resize)
  }, [])

  return (
    <div className="flex-1 relative overflow-hidden">
      <canvas ref={canvasRef} className="absolute inset-0 w-full h-full" />
    </div>
  )
}
