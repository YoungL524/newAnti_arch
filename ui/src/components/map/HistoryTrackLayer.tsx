/**
 * @file HistoryTrackLayer.tsx
 * @brief 按任务查询并绘制可分页回放的历史航迹线与点。
 * @details 历史数据来自数据库查询，图层生命周期独立于实时 TrackLayer，切换任务时会清理旧地图要素。
 */

import { useEffect, useRef, useMemo, useState, useCallback } from 'react'
import type { TrackHistoryPoint } from '../../lib/protocol'
import type { RadarDevice } from './types'
import { projectRadarTrack } from '../../lib/radarPose'

interface ScreenPoint { x: number; y: number }

const HISTORY_LINE = 'rgba(148, 163, 184, 0.35)'
const HISTORY_DOT = '#94a3b8'
const DOT_R = 1.8

interface Props {
  device: RadarDevice
  points: TrackHistoryPoint[]
  mapInstance: any
  mapClass: any
}

function strokePolyline(ctx: CanvasRenderingContext2D, points: ScreenPoint[], color: string, width: number, alpha: number) {
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

export default function HistoryTrackLayer({ device, points, mapInstance, mapClass }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const rafRef = useRef(0)
  const [repaintTick, setRepaintTick] = useState(0)

  const scheduleRepaint = useCallback(() => {
    if (rafRef.current) return
    rafRef.current = requestAnimationFrame(() => {
      rafRef.current = 0
      setRepaintTick(t => t + 1)
    })
  }, [])

  const trailMap = useMemo(() => {
    const m = new Map<number, TrackHistoryPoint[]>()
    for (const pt of points) {
      if (pt.deviceId !== device.id) continue
      if (!m.has(pt.targetId)) m.set(pt.targetId, [])
      m.get(pt.targetId)!.push(pt)
    }
    for (const arr of m.values()) {
      arr.sort((a, b) => a.tsMs - b.tsMs)
    }
    return m
  }, [points, device.id])

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
    const canvas = canvasRef.current
    const AMap = mapClass
    const map = mapInstance
    if (!canvas || !AMap || !map) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    const { clientWidth: cw, clientHeight: ch } = map.getContainer()
    if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch }
    ctx.clearRect(0, 0, cw, ch)
    if (trailMap.size === 0) return

    for (const [, pts] of trailMap) {
      const sp = pts
        .map((pt) => {
          const projected = projectRadarTrack(
            device,
            pt.faceIndex ?? -1,
            pt.azimuth * 180 / Math.PI,
            pt.elevation * 180 / Math.PI,
            pt.range,
          )
          if (!projected) return null
          const p = map.lngLatToContainer(new AMap.LngLat(projected.lng, projected.lat))
          return p ? { x: p.x, y: p.y } : null
        })
        .filter((v): v is ScreenPoint => {
          return !!v && v.x > -50 && v.x < cw + 50 && v.y > -50 && v.y < ch + 50
        })
      if (sp.length >= 2) strokePolyline(ctx, sp, HISTORY_LINE, 1, 0.55)
      for (const p of sp) {
        ctx.save()
        ctx.globalAlpha = 0.45
        ctx.beginPath()
        ctx.arc(p.x, p.y, DOT_R, 0, Math.PI * 2)
        ctx.fillStyle = HISTORY_DOT
        ctx.fill()
        ctx.restore()
      }
    }
  }, [trailMap, device, mapInstance, mapClass, repaintTick])

  if (points.length === 0) return null

  return (
    <canvas
      ref={canvasRef}
      className="absolute inset-0 pointer-events-none z-[15]"
      aria-hidden
    />
  )
}
