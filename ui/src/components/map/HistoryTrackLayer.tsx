/**
 * @file HistoryTrackLayer.tsx
 * @brief 绘制回放控制器提供的当前页可见轨迹，不查询或缓存历史。
 * @details 坐标按采集时安装 revision 计算；本层只做投影，按会话/设备/阵面/航迹隔离并在消亡处断线。
 */

import { useEffect, useRef, useMemo, useState, useCallback } from 'react'
import type { TrackHistoryPoint } from '../../lib/protocol'
import type { MapViewport } from './mapViewport'
import { drawTrack } from './TrackLayer'

/** @brief 仅绘制上层已经提供的历史结果，不新增历史查询接口。 */
interface Props {
  hiddenDeviceIds: readonly string[] ///< 当前台账中明确取消显示的设备；已删除设备仍可按归档位置绘制。
  points: TrackHistoryPoint[] ///< 上层拥有的历史结果，本层不持久缓存副本。
  mapInstance: MapViewport | null ///< 借用当前地图，不拥有其生命周期。
}

/** @brief 只绘制当前页，不把设备最新台账或实时位姿套到历史观测上。 */
export default function HistoryTrackLayer({ hiddenDeviceIds, points, mapInstance }: Props) {
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
    const active = new Map<string, TrackHistoryPoint[]>()
    const trails: TrackHistoryPoint[][] = []
    for (const pt of points) {
      if (hiddenDeviceIds.includes(pt.deviceId)) continue
      const key = `${pt.acquisitionId}:${pt.deviceId}:${pt.faceIndex}:${pt.targetId}`
      if (pt.packFlag === 0) { active.delete(key); continue }
      let trail = active.get(key)
      if (!trail) { trail = []; active.set(key, trail); trails.push(trail) }
      trail.push(pt)
    }
    return { trails, active }
  }, [points, hiddenDeviceIds])

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
    const canvas = canvasRef.current
    const map = mapInstance
    if (!canvas || !map) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    const { clientWidth: cw, clientHeight: ch } = map.getContainer()
    if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch }
    ctx.clearRect(0, 0, cw, ch)
    if (trailMap.trails.length === 0) return

    for (const pts of trailMap.trails) {
      const tail = pts[pts.length - 1]
      const key = `${tail.acquisitionId}:${tail.deviceId}:${tail.faceIndex}:${tail.targetId}`
      const active = trailMap.active.get(key) === pts
      /** @brief 序号只表示本段绘制顺序；复用实时画笔，不补造历史识别、选择或航向状态。 */
      const sp = pts.flatMap((pt, pointIndex) => {
        const p = map.project(pt.longitude, pt.latitude)
        return p ? [{ p, pt: { compacted: false, pointIndex, azimuth: null } }] : []
      })
      drawTrack(ctx, sp, false, !active)
    }
  }, [trailMap, mapInstance, repaintTick])

  if (points.length === 0) return null

  return (
    <canvas
      ref={canvasRef}
      className="absolute inset-0 pointer-events-none z-[15]"
      aria-hidden
    />
  )
}
