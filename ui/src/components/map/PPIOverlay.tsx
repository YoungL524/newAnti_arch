/**
 * @file PPIOverlay.tsx
 * @brief 绘制雷达量程环、方位刻度和阵面扫描扇区覆盖层。
 * @details 阵面朝向直接采用设备安装后测得的绝对航向：正北为 0°、顺时针为正，不叠加机体 Yaw。
 */

import { useEffect, useRef } from 'react'
import type { RadarDevice } from './types'
import { absoluteFaceHeading } from '../../lib/radarPose'

interface PPIOverlayProps {
  device: RadarDevice
  mapInstance: any
  mapClass: any
  connected: boolean
  acquiring: boolean
  hasFault?: boolean
}

export default function PPIOverlay({ device, mapInstance, mapClass, connected, acquiring, hasFault }: PPIOverlayProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const animRef = useRef<number>(0)
  const phaseRef = useRef<number>(0)
  const cycleRef = useRef<number>(0)
  const timeRef = useRef<number>(0)

  useEffect(() => {
    // Reset sweep state when acquisition starts
    if (acquiring) {
      phaseRef.current = 0
      cycleRef.current = 0
      timeRef.current = 0
    }

    const drawPPI = () => {
      const canvas = canvasRef.current
      const AMap = mapClass
      const m = mapInstance
      if (!canvas || !m || !AMap) {
        animRef.current = requestAnimationFrame(drawPPI)
        return
      }

      const ctx = canvas.getContext('2d')
      if (!ctx) return

      const container = m.getContainer()
      const cw = container.clientWidth
      const ch = container.clientHeight
      if (canvas.width !== cw || canvas.height !== ch) {
        canvas.width = cw
        canvas.height = ch
      }

      const LngLat = AMap.LngLat
      const center = m.lngLatToContainer(new LngLat(device.lng, device.lat))
      const cx = center.x
      const cy = center.y

      const latRad = (device.lat * Math.PI) / 180
      const metersPerDegLng = 111320 * Math.cos(latRad)
      const refPt = m.lngLatToContainer(new LngLat(device.lng + 0.01, device.lat))
      const pxPerKm = Math.abs(refPt.x - cx) / ((0.01 * metersPerDegLng) / 1000)
      const maxR = device.maxRange * pxPerKm

      // --- Color helpers ---
      const ringColor = (alpha: number) =>
        connected ? `rgba(30,90,70,${alpha})` : `rgba(160,160,160,${alpha})`
      const fillColor = connected ? 'rgba(249,250,247,0.28)' : 'rgba(200,200,198,0.32)'
      const labelColor = connected ? 'rgba(30,90,70,0.32)' : 'rgba(160,160,160,0.45)'
      const crossColor = connected ? 'rgba(30,90,70,0.08)' : 'rgba(160,160,160,0.14)'

      // --- Clear ---
      ctx.clearRect(0, 0, cw, ch)

      // --- PPI background ---
      ctx.beginPath()
      ctx.arc(cx, cy, maxR, 0, Math.PI * 2)
      ctx.fillStyle = fillColor
      ctx.fill()

      // --- Concentric rings ---
      for (let i = 1; i <= device.maxRange; i++) {
        const r = i * pxPerKm
        ctx.beginPath()
        ctx.arc(cx, cy, r, 0, Math.PI * 2)
        ctx.strokeStyle = i === device.maxRange ? ringColor(0.30) : ringColor(0.12)
        ctx.lineWidth = i === device.maxRange ? 1.5 : 1
        ctx.stroke()
      }

      // --- Solid crosshairs (horizontal = E-W, vertical = N-S) ---
      ctx.strokeStyle = crossColor
      ctx.lineWidth = 1
      ctx.setLineDash([])
      const d45 = maxR * 0.707
      ;[
        [[cx - maxR, cy], [cx + maxR, cy]],
        [[cx, cy - maxR], [cx, cy + maxR]],
      ].forEach(([from, to]) => {
        ctx.beginPath()
        ctx.moveTo(from[0], from[1])
        ctx.lineTo(to[0], to[1])
        ctx.stroke()
      })
      // Dashed diagonals
      ctx.setLineDash([5, 5])
      ;[
        [[cx - d45, cy - d45], [cx + d45, cy + d45]],
        [[cx + d45, cy - d45], [cx - d45, cy + d45]],
      ].forEach(([from, to]) => {
        ctx.beginPath()
        ctx.moveTo(from[0], from[1])
        ctx.lineTo(to[0], to[1])
        ctx.stroke()
      })
      ctx.setLineDash([])

      // --- Scan sector boundary lines (dashed) — one per active face ---
      ctx.setLineDash([4, 4])
      device.faces.filter(f => f.active).forEach(face => {
        const boresightRad = (absoluteFaceHeading(face.boresightAzimuth) * Math.PI) / 180
        const scanHalfRad = (face.scanAngle * Math.PI) / 180
        for (const sign of [-1, 1]) {
          const boundAngle = boresightRad + sign * scanHalfRad
          ctx.strokeStyle = ringColor(0.16)
          ctx.lineWidth = 1
          ctx.beginPath()
          ctx.moveTo(cx, cy)
          ctx.lineTo(cx + maxR * Math.cos(boundAngle - Math.PI / 2), cy + maxR * Math.sin(boundAngle - Math.PI / 2))
          ctx.stroke()
        }
      })
      ctx.setLineDash([])

      // --- Range labels (NE quadrant) ---
      ctx.fillStyle = labelColor
      ctx.font = '10px Inter, sans-serif'
      ctx.textAlign = 'left'
      ctx.textBaseline = 'top'
      for (let i = 1; i < device.maxRange; i++) {
        const r = i * pxPerKm
        ctx.fillText(`${i}km`, cx + r + 3, cy + 3)
      }

      // --- Angle labels ---
      ctx.textBaseline = 'middle'
      ctx.textAlign = 'center'
      const labelDist = maxR + 14
      ctx.fillText('90°', cx + labelDist, cy)      // East
      ctx.fillText('270°', cx - labelDist, cy)     // West
      ctx.fillText('0°', cx, cy - labelDist)       // North
      ctx.fillText('180°', cx, cy + labelDist)     // South

      // --- Sweep: synchronous, rotating shade depth per cycle ---
      if (acquiring) {
        const activeFaces = device.faces.filter(f => f.active)
        if (activeFaces.length > 0) {
          const now = performance.now()
          if (timeRef.current === 0) { timeRef.current = now; phaseRef.current = 0; cycleRef.current = 0 }
          const dt = (now - timeRef.current) / 1000
          timeRef.current = now

          let phase = phaseRef.current + dt
          if (phase >= 1) { phase = phase - 1; cycleRef.current++ }
          phaseRef.current = phase

          const boundaryZone = 0.07
          const rgb = connected ? '30,90,70' : '160,160,160'
          const N = activeFaces.length

          // 4-tier opacity: darkest → lightest, rotates each sweep cycle
          const sweepAlphas = [0.42, 0.32, 0.24, 0.16]
          const trailAlphas = [0.36, 0.26, 0.18, 0.10]

          activeFaces.forEach((face, fi) => {
            const shadeIdx = N > 1 ? ((cycleRef.current - fi) % N + N) % N : 0
            const sweepAlpha = sweepAlphas[Math.min(shadeIdx, 3)]
            const trailMaxAlpha = trailAlphas[Math.min(shadeIdx, 3)]

            const boresightRad = (absoluteFaceHeading(face.boresightAzimuth) * Math.PI) / 180
            const scanHalfRad = (face.scanAngle * Math.PI) / 180

            let primaryAlpha = 1
            let primaryAngle = boresightRad - scanHalfRad + 2 * scanHalfRad * phase
            let ghostAlpha = 0
            let ghostAngle = 0

            if (phase > 1 - boundaryZone) {
              const t = (phase - (1 - boundaryZone)) / boundaryZone
              primaryAlpha = 1 - t
              ghostAlpha = t
              ghostAngle = boresightRad - scanHalfRad
            } else if (phase < boundaryZone) {
              const t = phase / boundaryZone
              primaryAlpha = t
              ghostAlpha = 1 - t
              ghostAngle = boresightRad + scanHalfRad
            }

            // Ghost sweep at opposite boundary
            if (ghostAlpha > 0.01) {
              ctx.save()
              ctx.translate(cx, cy)
              ctx.rotate(ghostAngle - Math.PI / 2)
              ctx.strokeStyle = `rgba(${rgb},${sweepAlpha * ghostAlpha})`
              ctx.lineWidth = 1.2
              ctx.beginPath()
              ctx.moveTo(0, 0)
              ctx.lineTo(maxR, 0)
              ctx.stroke()
              ctx.restore()
            }

            // Primary sweep with trail
            ctx.save()
            ctx.translate(cx, cy)
            ctx.rotate(primaryAngle - Math.PI / 2)

            const trailRad = Math.PI / 7
            const layers = 48
            for (let i = 0; i < layers; i++) {
              const a = (-i / layers) * trailRad
              const b = (-(i + 1) / layers) * trailRad
              const alpha = trailMaxAlpha * Math.exp(-i / 14) * primaryAlpha
              if (alpha < 0.003) continue
              ctx.fillStyle = `rgba(${rgb},${alpha})`
              ctx.beginPath()
              ctx.moveTo(0, 0)
              ctx.arc(0, 0, maxR, b, a)
              ctx.closePath()
              ctx.fill()
            }

            ctx.strokeStyle = `rgba(${rgb},${sweepAlpha * primaryAlpha})`
            ctx.lineWidth = 1.3
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(maxR, 0)
            ctx.stroke()

            ctx.restore()
          })
        }
      }

      // --- Center dot ---
      ctx.beginPath()
      ctx.arc(cx, cy, 3, 0, Math.PI * 2)
      ctx.fillStyle = hasFault ? '#ef4444' : connected ? '#1E5A46' : '#999'
      ctx.fill()

      // --- Scan sector arc highlight (subtle) — one per active face ---
      device.faces.filter(f => f.active).forEach(face => {
        const boresightRad = (absoluteFaceHeading(face.boresightAzimuth) * Math.PI) / 180
        const scanHalfRad = (face.scanAngle * Math.PI) / 180
        ctx.save()
        ctx.translate(cx, cy)
        ctx.rotate(boresightRad - Math.PI / 2)
        ctx.beginPath()
        ctx.arc(0, 0, maxR, -scanHalfRad, scanHalfRad)
        ctx.closePath()
        ctx.fillStyle = connected ? 'rgba(30,90,70,0.015)' : 'rgba(160,160,160,0.025)'
        ctx.fill()
        ctx.restore()
      })

      animRef.current = requestAnimationFrame(drawPPI)
    }

    animRef.current = requestAnimationFrame(drawPPI)
    return () => cancelAnimationFrame(animRef.current)
  }, [device, mapInstance, mapClass, connected, acquiring])

  return <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none z-10" />
}

