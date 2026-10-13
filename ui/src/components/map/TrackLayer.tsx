/**
 * @file TrackLayer.tsx
 * @brief 把当前采集会话的旧路径形状和近期原始航迹点绘制到地图画布。
 * @details 上游 CoreProvider 已区分旧路径与近期点；本层按完整会话键投影并绘制，不再持有第二份历史。
 */

import { useEffect, useRef, useMemo, useState, useCallback } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import type { RadarTrack } from '../../lib/protocol'
import type { MapViewport } from './mapViewport'
import type { RadarDevice } from './types'
import { projectRadarTrack } from '../../lib/radarPose'

type TrackStyle = NonNullable<RadarTrack['style']>
type TrackInteraction = NonNullable<RadarTrack['interaction']>

interface TrailPoint {
  lng: number
  lat: number
  compacted: boolean ///< 旧路径只参与线形绘制，最近原始点继续显示渐变点与箭头。
  deviceId: string
  trackId: number
  slantRange: number
  azimuth: number
  elevation: number
  altitude: number
  radialSpeed: number
  pointIndex: number
  style?: TrackStyle
  interaction?: TrackInteraction
}

interface ScreenPoint { x: number; y: number }

const TRACK_COLOR = '#059669'
const SELECTED_COLOR = '#ea580c'          // 暖橙，浅底地图上比亮黄更醒目
const GRAY_COLOR = '#9ca3af'
const GRAY_LINE = 'rgba(156, 163, 175, 0.55)'

const GRAY_HEAD_COUNT = 3
const HEAD_R = 3.2
const LINE_W = 1
const TAIL_R = LINE_W / 2              // 最小点半径 = 线宽一半（直径与线等宽）
const GRAY_R = TAIL_R
const GRADIENT_SPAN = 10               // 仅最近 N 个绿点参与尺寸渐变，更早点保持最小
const GRADIENT_POWER = 2.6             // >1 使尾部更快缩小，接近 Qt 比例
const ARROW_LEN = 5

/** @brief 复用当前地图投影绘制单设备航迹；地图与业务状态均由上层拥有。 */
interface Props {
  device: RadarDevice ///< 当前设备安装参数，供原有姿态转换使用。
  mapInstance: MapViewport | null ///< 借用地图；为空时不绘制，不创建独立实例。
}

function isSelected(interaction?: TrackInteraction): boolean {
  return !!(interaction?.selected || interaction?.trackingEnabled)
}

function activeColor(interaction?: TrackInteraction): string {
  return isSelected(interaction) ? SELECTED_COLOR : TRACK_COLOR
}

function greenRadius(i: number, greenCount: number, headR: number): number {
  if (greenCount <= 1) return headR
  const tailR = TAIL_R
  const gradStart = Math.max(0, greenCount - GRADIENT_SPAN)
  if (i < gradStart) return tailR
  const localCount = greenCount - gradStart
  if (localCount <= 1) return headR
  const t = (i - gradStart) / (localCount - 1)
  const eased = Math.pow(t, GRADIENT_POWER)
  return tailR + (headR - tailR) * eased
}

function greenAlpha(i: number, greenCount: number): number {
  if (greenCount <= 1) return 1
  const gradStart = Math.max(0, greenCount - GRADIENT_SPAN)
  if (i < gradStart) return 0.45
  const localCount = greenCount - gradStart
  if (localCount <= 1) return 1
  const t = (i - gradStart) / (localCount - 1)
  return 0.45 + 0.55 * Math.pow(t, 1.8)
}

function movementAngle(sp: { p: ScreenPoint }[], fallbackAzimuth: number): number {
  if (sp.length >= 2) {
    const a = sp[sp.length - 2].p
    const b = sp[sp.length - 1].p
    return Math.atan2(b.y - a.y, b.x - a.x)
  }
  return (fallbackAzimuth - 90) * Math.PI / 180
}

function drawChevronArrow(
  ctx: CanvasRenderingContext2D,
  x: number, y: number,
  angle: number,
  color: string,
  len = ARROW_LEN,
) {
  ctx.save()
  ctx.translate(x, y)
  ctx.rotate(angle)
  ctx.strokeStyle = color
  ctx.lineWidth = 1.6
  ctx.lineCap = 'round'
  ctx.lineJoin = 'round'
  ctx.beginPath()
  ctx.moveTo(-len * 0.35, -len * 0.5)
  ctx.lineTo(len * 0.55, 0)
  ctx.lineTo(-len * 0.35, len * 0.5)
  ctx.stroke()
  ctx.restore()
}

function strokePolyline(
  ctx: CanvasRenderingContext2D,
  points: ScreenPoint[],
  color: string,
  width: number,
  alpha: number,
) {
  if (points.length < 2) return
  ctx.save()
  ctx.strokeStyle = color
  ctx.lineWidth = width
  ctx.globalAlpha = alpha
  ctx.lineJoin = 'round'
  ctx.lineCap = 'round'
  ctx.beginPath()
  ctx.moveTo(points[0].x, points[0].y)
  for (let i = 1; i < points.length; i++) ctx.lineTo(points[i].x, points[i].y)
  ctx.stroke()
  ctx.restore()
}

function fillDot(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, color: string, alpha = 1) {
  ctx.save()
  ctx.globalAlpha = alpha
  ctx.beginPath()
  ctx.arc(x, y, r, 0, Math.PI * 2)
  ctx.fillStyle = color
  ctx.fill()
  ctx.restore()
}

/**
 * @brief 绘制当前会话的旧路径轮廓和近期原始点；旧路径不叠加渐变圆点。
 * @param ctx 地图覆盖层本帧画布。
 * @param sp 已投影到当前地图像素坐标且按点序号排序的同一航迹。
 * @param selected 当前目标是否被选择。
 */
function drawTrack(ctx: CanvasRenderingContext2D, sp: { p: ScreenPoint; pt: TrailPoint }[], selected: boolean) {
  if (sp.length === 0) return
  const accent = activeColor(sp[sp.length - 1].pt.interaction)
  const headR = selected ? HEAD_R + 0.4 : HEAD_R

  const older = sp.filter(s => s.pt.compacted)
  const recent = sp.filter(s => !s.pt.compacted)
  const olderPixels: ScreenPoint[] = []
  for (const point of older) {
    const previous = olderPixels[olderPixels.length - 1]
    if (!previous || (point.p.x - previous.x) ** 2 + (point.p.y - previous.y) ** 2 >= 1) {
      olderPixels.push(point.p)
    }
  }
  if (older.length > 0 && olderPixels[olderPixels.length - 1] !== older[older.length - 1].p) {
    olderPixels.push(older[older.length - 1].p)
  }
  if (recent.length > 0 && olderPixels.length > 0) olderPixels.push(recent[0].p)
  strokePolyline(ctx, olderPixels, accent, LINE_W, 0.65)

  const grayPart = recent.filter(s => s.pt.pointIndex < GRAY_HEAD_COUNT)
  const greenPart = recent.filter(s => s.pt.pointIndex >= GRAY_HEAD_COUNT)

  if (grayPart.length >= 2) {
    strokePolyline(ctx, grayPart.map(s => s.p), GRAY_LINE, LINE_W, 0.7)
  }
  for (const s of grayPart) {
    fillDot(ctx, s.p.x, s.p.y, GRAY_R, GRAY_COLOR, 0.75)
  }

  if (greenPart.length >= 2) {
    strokePolyline(ctx, greenPart.map(s => s.p), accent, LINE_W, 0.88)
  }
  for (let i = 0; i < greenPart.length; i++) {
    const r = greenRadius(i, greenPart.length, headR)
    fillDot(ctx, greenPart[i].p.x, greenPart[i].p.y, r, accent, greenAlpha(i, greenPart.length))
  }

  if (greenPart.length > 0) {
    const angle = movementAngle(greenPart, greenPart[greenPart.length - 1].pt.azimuth)
    const head = greenPart[greenPart.length - 1].p
    const tipX = head.x + Math.cos(angle) * (headR + 2.5)
    const tipY = head.y + Math.sin(angle) * (headR + 2.5)
    drawChevronArrow(ctx, tipX, tipY, angle, accent)
  } else if (grayPart.length > 0) {
    const angle = movementAngle(grayPart, grayPart[grayPart.length - 1].pt.azimuth)
    const head = grayPart[grayPart.length - 1].p
    drawChevronArrow(ctx, head.x, head.y, angle, GRAY_COLOR, ARROW_LEN - 1)
  }

  if (selected) {
    const head = sp[sp.length - 1].p
    ctx.save()
    ctx.beginPath()
    ctx.arc(head.x, head.y, headR + 2.5, 0, Math.PI * 2)
    ctx.strokeStyle = accent
    ctx.lineWidth = 1.2
    ctx.globalAlpha = 0.45
    ctx.stroke()
    ctx.restore()
  }
}

/** @brief 从既有 CoreProvider 派生航迹绘制点，并在当前地图容器中处理选择，不复制完整历史。 */
export default function TrackLayer({ device, mapInstance }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const rafRef = useRef(0)
  const [repaintTick, setRepaintTick] = useState(0)
  const { tracks, mapOldTracks, acquisition, setTrackInteraction } = useCore()

  const scheduleRepaint = useCallback(() => {
    if (rafRef.current) return
    rafRef.current = requestAnimationFrame(() => {
      rafRef.current = 0
      setRepaintTick(t => t + 1)
    })
  }, [])

  const trailMap = useMemo(() => {
    const m = new Map<string, TrailPoint[]>()
    if (!acquisition) return m
    for (const t of [...mapOldTracks, ...tracks]) {
      const deviceId = t.deviceId || device.id
      if (deviceId !== device.id) continue

      const trackId = t.trackId ?? t.batchId ?? 0
      const key = `${t.acquisitionId ?? ''}:${deviceId}:${t.faceIndex ?? -1}:${trackId}`
      if (!m.has(key)) m.set(key, [])
      const projected = projectRadarTrack(device, t.faceIndex ?? -1, t.azimuth, t.elevation, t.slantRange)
      if (!projected) continue
      m.get(key)!.push({
        lng: projected.lng, lat: projected.lat, compacted: t.compacted ?? false, deviceId, trackId,
        slantRange: t.slantRange,
        azimuth: projected.bearingDeg,
        elevation: t.elevation,
        altitude: t.altitude,
        radialSpeed: t.radialSpeed,
        pointIndex: t.pointIndex ?? 0,
        style: t.style,
        interaction: t.interaction,
      })
    }
    for (const arr of m.values()) {
      arr.sort((a, b) => a.pointIndex - b.pointIndex)
    }
    return m
  }, [tracks, mapOldTracks, acquisition, device])

  useEffect(() => {
    const map = mapInstance
    if (!map) return
    const unsubscribe = map.watchView(scheduleRepaint)
    return () => {
      unsubscribe()
      if (rafRef.current) cancelAnimationFrame(rafRef.current)
    }
  }, [mapInstance, scheduleRepaint])

  useEffect(() => {
    const map = mapInstance
    if (!map || !acquisition || trailMap.size === 0) return

    const threshold2 = 12 * 12

    /** @brief 使用地图容器像素命中航迹头；多个设备共享同次点击处理标记，不改采集状态。 */
    const onClick: Parameters<MapViewport['watchClick']>[0] = e => {
      if (e && e.handled) return
      const x = e.x
      const y = e.y
      if (typeof x !== 'number' || typeof y !== 'number') return

      let best: { deviceId: string; trackId: number; interaction?: TrackInteraction; d2: number } | null = null

      for (const [, pts] of trailMap) {
        const headPt = pts[pts.length - 1]
        const p = map.project(headPt.lng, headPt.lat)
        if (!p) continue
        const dx = p.x - x
        const dy = p.y - y
        const d2 = dx * dx + dy * dy
        if (d2 <= threshold2 && (!best || d2 < best.d2)) {
          best = { deviceId: headPt.deviceId, trackId: headPt.trackId, interaction: headPt.interaction, d2 }
        }
      }

      if (!best) return
      if (e) e.handled = true
      const selected = !(best.interaction?.selected ?? false)
      const trackingEnabled = best.interaction?.trackingEnabled ?? false
      setTrackInteraction(best.deviceId, best.trackId, selected, trackingEnabled)
    }

    return map.watchClick(onClick)
  }, [mapInstance, acquisition, trailMap, setTrackInteraction])

  useEffect(() => {
    const canvas = canvasRef.current
    const map = mapInstance
    if (!canvas || !map) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    const { clientWidth: cw, clientHeight: ch } = map.getContainer()
    if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch }
    ctx.clearRect(0, 0, cw, ch)
    if (!acquisition || trailMap.size === 0) return

    for (const [, pts] of trailMap) {
      if (pts.length === 0) continue
      const headMeta = pts[pts.length - 1]
      const selected = isSelected(headMeta.interaction)

      const sp = pts
        .map((pt) => {
          const p = map.project(pt.lng, pt.lat)
          return p ? { p: { x: p.x, y: p.y }, pt } : null
        })
        .filter((v): v is { p: ScreenPoint; pt: TrailPoint } => {
          return !!v && v.p.x > -50 && v.p.x < cw + 50 && v.p.y > -50 && v.p.y < ch + 50
        })
      if (sp.length === 0) continue

      drawTrack(ctx, sp, selected)
    }
  }, [trailMap, acquisition, device, mapInstance, repaintTick])

  return <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none z-20" />
}
