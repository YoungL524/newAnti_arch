import { useEffect, useRef, useMemo } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import type { RadarDevice } from './types'

const COLOR = 'rgba(30,90,70,0.9)'
const LINE_WIDTH = 2.2
const HEAD_R = 4

interface Props { device: RadarDevice; mapInstance: any; mapClass: any }

function polarToLngLat(lng: number, lat: number, azRad: number, rng: number) {
  const lr = (lat * Math.PI) / 180
  const mpl = 111320 * Math.cos(lr)
  return [lng + rng * Math.sin(azRad) / mpl, lat + rng * Math.cos(azRad) / 111320] as const
}

export default function TrackLayer({ device, mapInstance, mapClass }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const { tracks, acquisition } = useCore()

  const trailMap = useMemo(() => {
    const m = new Map<number, [number, number][]>()
    if (!acquisition) return m
    for (const t of tracks) {
      const id = t.batchId || 0
      if (!m.has(id)) m.set(id, [])
      const [lng, lat] = polarToLngLat(device.lng, device.lat, t.azimuth * Math.PI / 180, t.slantRange)
      const arr = m.get(id)!
      arr.push([lng, lat])
    }
    return m
  }, [tracks, acquisition, device])

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
      if (pts.length < 2) continue
      const sp = pts.map(([lng, lat]) => map.lngLatToContainer(new AMap.LngLat(lng, lat)))
                     .filter((p: any) => p && p.x > -50 && p.x < cw + 50 && p.y > -50 && p.y < ch + 50)
      if (sp.length < 2) continue
      const n = sp.length

      // ── 航迹线 ──
      ctx.beginPath()
      ctx.moveTo(sp[0].x, sp[0].y)
      for (let i = 1; i < n; i++) ctx.lineTo(sp[i].x, sp[i].y)
      ctx.strokeStyle = COLOR
      ctx.lineWidth = LINE_WIDTH
      ctx.lineJoin = 'round'
      ctx.lineCap = 'round'
      ctx.stroke()

      // ── 头部标记点 ──
      const head = sp[n - 1]
      ctx.beginPath()
      ctx.arc(head.x, head.y, HEAD_R, 0, Math.PI * 2)
      ctx.fillStyle = COLOR
      ctx.fill()
    }
  }, [trailMap, acquisition, device, mapInstance, mapClass])

  return <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none z-20" />
}
