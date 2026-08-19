/**
 * @file TrackLayer.tsx
 * @brief 把当前实时航迹集合映射为可选择的地图点、标签和短尾迹。
 * @details 图层按稳定航迹键增删改地图要素；输入是 WebSocket 归并后的有界当前态，不在组件内累计全量历史。
 */

import { useEffect, useRef, useMemo, useState, useCallback } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import type { RadarTrack } from '../../lib/protocol'
import type { RadarDevice } from './types'
import { projectRadarTrack } from '../../lib/radarPose'

type TrackStyle = NonNullable<RadarTrack['style']>
type TrackInteraction = NonNullable<RadarTrack['interaction']>

interface TrailPoint {
  lng: number
  lat: number
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

interface Props { device: RadarDevice; mapInstance: any; mapClass: any }

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

function drawTrack(ctx: CanvasRenderingContext2D, sp: { p: ScreenPoint; pt: TrailPoint }[], selected: boolean) {
  if (sp.length === 0) return
  const accent = activeColor(sp[sp.length - 1].pt.interaction)
  const headR = selected ? HEAD_R + 0.4 : HEAD_R

  const grayPart = sp.filter(s => s.pt.pointIndex < GRAY_HEAD_COUNT)
  const greenPart = sp.filter(s => s.pt.pointIndex >= GRAY_HEAD_COUNT)

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

export default function TrackLayer({ device, mapInstance, mapClass }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const rafRef = useRef(0)
  const [repaintTick, setRepaintTick] = useState(0)
  const { tracks, acquisition, setTrackInteraction } = useCore()

  const scheduleRepaint = useCallback(() => {
    if (rafRef.current) return
    rafRef.current = requestAnimationFrame(() => {
      rafRef.current = 0
      setRepaintTick(t => t + 1)
    })
  }, [])

  const trailMap = useMemo(() => {
    const m = new Map<number, TrailPoint[]>()
    if (!acquisition) return m
    for (const t of tracks) {
      const deviceId = t.deviceId || device.id
      if (deviceId !== device.id) continue

      const trackId = t.trackId ?? t.batchId ?? 0
      if (!m.has(trackId)) m.set(trackId, [])
      const projected = projectRadarTrack(device, t.azimuth, t.elevation, t.slantRange)
      m.get(trackId)!.push({
        lng: projected.lng, lat: projected.lat, deviceId, trackId,
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
  }, [tracks, acquisition, device])

  useEffect(() => {
    const map = mapInstance
    if (!map) return
    map.on('mapmove', scheduleRepaint)
    map.on('zoomchange', scheduleRepaint)
    map.on('resize', scheduleRepaint)
    return () => {
      map.off('mapmove', scheduleRepaint)
      map.off('zoomchange', scheduleRepaint)
      map.off('resize', scheduleRepaint)
      if (rafRef.current) cancelAnimationFrame(rafRef.current)
    }
  }, [mapInstance, scheduleRepaint])

  useEffect(() => {
    const AMap = mapClass; const map = mapInstance
    if (!AMap || !map || !acquisition || trailMap.size === 0) return

    const threshold2 = 12 * 12

    const onClick = (e: any) => {
      if (e && e.__trackInteractionHandled) return
      const x = e?.pixel?.x
      const y = e?.pixel?.y
      if (typeof x !== 'number' || typeof y !== 'number') return

      let best: { deviceId: string; trackId: number; interaction?: TrackInteraction; d2: number } | null = null

      for (const [, pts] of trailMap) {
        const headPt = pts[pts.length - 1]
        const p = map.lngLatToContainer(new AMap.LngLat(headPt.lng, headPt.lat))
        if (!p) continue
        const dx = p.x - x
        const dy = p.y - y
        const d2 = dx * dx + dy * dy
        if (d2 <= threshold2 && (!best || d2 < best.d2)) {
          best = { deviceId: headPt.deviceId, trackId: headPt.trackId, interaction: headPt.interaction, d2 }
        }
      }

      if (!best) return
      if (e) e.__trackInteractionHandled = true
      const selected = !(best.interaction?.selected ?? false)
      const trackingEnabled = best.interaction?.trackingEnabled ?? false
      setTrackInteraction(best.deviceId, best.trackId, selected, trackingEnabled)
    }

    map.on('click', onClick)
    return () => { map.off('click', onClick) }
  }, [mapInstance, mapClass, acquisition, trailMap, setTrackInteraction])

  useEffect(() => {
    const canvas = canvasRef.current
    const AMap = mapClass; const map = mapInstance
    if (!canvas || !AMap || !map) return
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
          const p = map.lngLatToContainer(new AMap.LngLat(pt.lng, pt.lat))
          return p ? { p: { x: p.x, y: p.y }, pt } : null
        })
        .filter((v): v is { p: ScreenPoint; pt: TrailPoint } => {
          return !!v && v.p.x > -50 && v.p.x < cw + 50 && v.p.y > -50 && v.p.y < ch + 50
        })
      if (sp.length === 0) continue

      drawTrack(ctx, sp, selected)
    }
  }, [trailMap, acquisition, device, mapInstance, mapClass, repaintTick])

  return <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none z-20" />
}
