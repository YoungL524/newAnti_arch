/**
 * @file PPIOverlay.tsx
 * @brief 绘制雷达量程环、方位刻度和从原点向外传播的径向扫描波。
 * @details 单面阵在其方位扇区内绘制波前，四面全开时四段波前拼成同心圆；不再为每个阵面绘制旋转扫描线。
 * 阵面朝向直接采用设备安装后测得的绝对航向：正北为 0°、顺时针为正，不叠加机体 Yaw。
 */

import { useEffect, useRef } from 'react'
import type { MapViewport } from './mapViewport'
import type { RadarDevice } from './types'
import { absoluteFaceHeading } from '../../lib/radarPose'

/** @brief P 显覆盖层仅依赖的设备、地图和本地采集显示状态。 */
interface PPIOverlayProps {
  /** 当前雷达整机及阵面安装参数。 */
  device: RadarDevice
  /** 当前地图的借用投影接口；原生实例由 MapRadarView 持有，覆盖层不拥有其生命周期。 */
  mapInstance: MapViewport | null
  /** 仅影响配色的通信状态，不决定动画是否运行。 */
  connected: boolean
  /** 本地采集/PPI 扫描状态，是动画唯一启停条件。 */
  acquiring: boolean
  /** 当前设备存在持续故障时点亮中心故障色。 */
  hasFault?: boolean
}

/**
 * @brief 在地图 Canvas 上持续绘制静态 P 显和周期性的径向震荡波。
 * @note 动画只表达本地采集显示，不代表 DP、SP 或 FP 已执行控制命令。
 */
export default function PPIOverlay({ device, mapInstance, connected, acquiring, hasFault }: PPIOverlayProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const animRef = useRef<number>(0)
  const waveStartedAtRef = useRef<number>(0)

  useEffect(() => {
    if (acquiring) {
      waveStartedAtRef.current = performance.now()
    } else {
      waveStartedAtRef.current = 0
    }

    const drawPPI = () => {
      const canvas = canvasRef.current
      const m = mapInstance
      if (!canvas || !m) {
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

      const center = m.project(device.lng, device.lat)
      const cx = center.x
      const cy = center.y

      const latRad = (device.lat * Math.PI) / 180
      const metersPerDegLng = 111320 * Math.cos(latRad)
      const refPt = m.project(device.lng + 0.01, device.lat)
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
        const boresightRad = (absoluteFaceHeading(face.heading) * Math.PI) / 180
        const scanHalfRad = (Math.min(45, Math.max(0, Math.abs(face.scanAngle))) * Math.PI) / 180
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

      /** @brief 以较浅的灰绿色和 1.2 秒周期填充单峰径向渐变，保持波带两端无描边、自然消隐。 */
      if (acquiring) {
        const activeFaces = device.faces.filter(f => f.active)
        if (activeFaces.length > 0) {
          const now = performance.now()
          if (waveStartedAtRef.current === 0) waveStartedAtRef.current = now
          const phase = ((now - waveStartedAtRef.current) % 1200) / 1200
          const waveCenterR = maxR * phase
          const bandHalfWidth = maxR * 0.16
          const fade = Math.sin(phase * Math.PI)
          const waveColor = connected ? '45,95,80' : '150,150,150'

          activeFaces.forEach(face => {
            const boresightRad = (absoluteFaceHeading(face.heading) * Math.PI) / 180
            const scanHalfRad = (Math.min(45, Math.max(0, Math.abs(face.scanAngle))) * Math.PI) / 180
            ctx.save()
            ctx.translate(cx, cy)
            ctx.rotate(boresightRad - Math.PI / 2)

            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.arc(0, 0, maxR, -scanHalfRad, scanHalfRad)
            ctx.closePath()
            ctx.clip()

            const innerR = Math.max(0, waveCenterR - bandHalfWidth)
            const outerR = Math.min(maxR, waveCenterR + bandHalfWidth)
            if (outerR > innerR) {
              const energy = ctx.createRadialGradient(0, 0, innerR, 0, 0, outerR)
              energy.addColorStop(0, `rgba(${waveColor},0)`)
              energy.addColorStop(0.3, `rgba(${waveColor},${0.12 * fade})`)
              energy.addColorStop(0.5, `rgba(${waveColor},${0.28 * fade})`)
              energy.addColorStop(0.7, `rgba(${waveColor},${0.12 * fade})`)
              energy.addColorStop(1, `rgba(${waveColor},0)`)
              ctx.beginPath()
              ctx.arc(0, 0, outerR, -scanHalfRad, scanHalfRad)
              ctx.arc(0, 0, innerR, scanHalfRad, -scanHalfRad, true)
              ctx.closePath()
              ctx.fillStyle = energy
              ctx.fill()
            }
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
        const boresightRad = (absoluteFaceHeading(face.heading) * Math.PI) / 180
        const scanHalfRad = (Math.min(45, Math.max(0, Math.abs(face.scanAngle))) * Math.PI) / 180
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
  }, [device, mapInstance, connected, acquiring])

  return <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none z-10" />
}
