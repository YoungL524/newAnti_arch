import { useEffect, useRef, useMemo } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import type { RadarTrack } from '../../lib/protocol'
import type { RadarDevice } from './types'

type TrackStyle = NonNullable<RadarTrack['style']>
type TrackInteraction = NonNullable<RadarTrack['interaction']>

interface TrailPoint {
  lng: number
  lat: number
  deviceId: string
  trackId: number
  style?: TrackStyle
  interaction?: TrackInteraction
}

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
  const { tracks, acquisition, setTrackInteraction } = useCore()

  const trailMap = useMemo(() => {
    const m = new Map<number, TrailPoint[]>()
    if (!acquisition) return m
    for (const t of tracks) {
      const deviceId = t.deviceId || device.id
      if (deviceId !== device.id) continue

      const trackId = t.trackId ?? t.batchId ?? 0
      const id = trackId
      if (!m.has(id)) m.set(id, [])
      const [lng, lat] = polarToLngLat(device.lng, device.lat, t.azimuth * Math.PI / 180, t.slantRange)
      const arr = m.get(id)!
      arr.push({ lng, lat, deviceId, trackId, style: t.style, interaction: t.interaction })
    }
    return m
  }, [tracks, acquisition, device])

  useEffect(() => {
    const AMap = mapClass; const map = mapInstance
    if (!AMap || !map || !acquisition || trailMap.size === 0) return

    const threshold2 = 14 * 14

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
      if (pts.length < 2) continue
      const sp = pts
        .map((pt) => {
          const p = map.lngLatToContainer(new AMap.LngLat(pt.lng, pt.lat))
          return p ? { p, pt } : null
        })
        .filter((v): v is { p: { x: number; y: number }; pt: TrailPoint } => {
          return !!v && v.p && v.p.x > -50 && v.p.x < cw + 50 && v.p.y > -50 && v.p.y < ch + 50
        })
      if (sp.length < 2) continue
      const n = sp.length

      const headMeta = sp[n - 1].pt
      const color = headMeta.style?.color || COLOR
      const opacity = typeof headMeta.style?.opacity === 'number' ? headMeta.style.opacity : 0.9
      const headR = typeof headMeta.style?.size === 'number' ? headMeta.style.size : HEAD_R

      ctx.save()
      ctx.globalAlpha = opacity
      ctx.beginPath()
      ctx.moveTo(sp[0].p.x, sp[0].p.y)
      for (let i = 1; i < n; i++) ctx.lineTo(sp[i].p.x, sp[i].p.y)
      ctx.strokeStyle = color
      ctx.lineWidth = LINE_WIDTH
      ctx.lineJoin = 'round'
      ctx.lineCap = 'round'
      ctx.stroke()

      ctx.beginPath()
      ctx.arc(sp[n - 1].p.x, sp[n - 1].p.y, headR, 0, Math.PI * 2)
      ctx.fillStyle = color
      ctx.fill()
      ctx.restore()
    }
  }, [trailMap, acquisition, device, mapInstance, mapClass])

  return <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none z-20" />
}
