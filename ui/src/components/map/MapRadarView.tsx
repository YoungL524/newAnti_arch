import { useEffect, useRef, useState } from 'react'
import AMapLoader from '@amap/amap-jsapi-loader'

const API_KEY = import.meta.env.VITE_GAODE_API_KEY as string
const SECURITY_CODE = import.meta.env.VITE_GAODE_SECURITY_CODE as string | undefined

interface Target {
  id: string; lng: number; lat: number; color: string
}

const RADAR_CENTER: [number, number] = [118.653090, 31.935020]

const demoTargets: Target[] = [
  { id: 'T-001', lng: 118.656, lat: 31.938, color: '#9fc9a2' },
  { id: 'T-002', lng: 118.648, lat: 31.932, color: '#9fc9a2' },
  { id: 'T-003', lng: 118.660, lat: 31.936, color: '#dfa88f' },
  { id: 'T-004', lng: 118.645, lat: 31.933, color: '#9fbbe0' },
  { id: 'T-005', lng: 118.662, lat: 31.930, color: '#9fc9a2' },
  { id: 'T-006', lng: 118.650, lat: 31.940, color: '#dfa88f' },
  { id: 'T-007', lng: 118.655, lat: 31.928, color: '#9fbbe0' },
]

export default function MapRadarView() {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef = useRef<any>(null)
  const mapInstanceRef = useRef<any>(null)
  const [ready, setReady] = useState(false)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    if (!API_KEY) {
      setError('需要在 .env 中配置 VITE_GAODE_API_KEY')
      return
    }

    window._AMapSecurityConfig = {
      securityJsCode: SECURITY_CODE || '',
    }

    AMapLoader.load({
      key: API_KEY,
      version: '2.0',
      plugins: ['AMap.Scale', 'AMap.ToolBar'],
    })
      .then((AMap: any) => {
        mapRef.current = AMap

        const map = new AMap.Map(containerRef.current, {
          zoom: 14,
          center: RADAR_CENTER,
          mapStyle: 'amap://styles/whitesmoke',
          features: ['bg', 'road', 'building', 'point'],
          showIndoorMap: false,
        })
        mapInstanceRef.current = map

        demoTargets.forEach((t) => {
          const m = new AMap.Marker({
            position: [t.lng, t.lat],
            content: `<div style="width:8px;height:8px;background:${t.color};border-radius:50%;border:1px solid #fff;"></div>`,
            offset: new AMap.Pixel(-4, -4),
            zIndex: 50,
          })
          map.add(m)
        })

        setReady(true)
      })
      .catch((err: any) => {
        const msg = err?.message || String(err)
        console.error('[Amap] 加载失败:', err)
        setError(`高德地图加载失败\n\n${msg || JSON.stringify(err)}\n\n请确认：\n1. Key 服务平台为「Web端(JS API)」\n2. 白名单已添加 localhost`)
      })

    return () => {
      mapInstanceRef.current?.destroy()
      mapInstanceRef.current = null
    }
  }, [])

  return (
    <div className="absolute inset-0">
      <div ref={containerRef} className="absolute inset-0" />

      {!ready && !error && (
        <div className="absolute inset-0 flex items-center justify-center" style={{ background: 'var(--canvas)' }}>
          <div className="text-center">
            <p className="text-[14px] mb-3" style={{ color: 'var(--secondary)' }}>加载高德地图...</p>
            <div className="w-6 h-6 border-2 rounded-full animate-spin" style={{
              borderColor: 'var(--hairlineStrong)', borderTopColor: 'var(--primaryBright)', margin: '0 auto',
            }} />
          </div>
        </div>
      )}

      {error && (
        <div className="absolute inset-0 flex items-center justify-center" style={{ background: 'var(--canvas)' }}>
          <div className="text-center max-w-lg">
            <div className="text-[36px] mb-3">🗺️</div>
            <p className="text-[14px] mb-2 font-medium" style={{ color: 'var(--ink)' }}>地图加载失败</p>
            <pre className="text-[11px] mb-3 p-3 rounded text-left whitespace-pre-wrap max-h-48 overflow-auto"
              style={{ color: 'var(--error)', background: 'var(--surfaceLow)', border: '1px solid var(--hairline)' }}
            >{error}</pre>
            <p className="text-[11px]" style={{ color: 'var(--secondary)' }}>检查 .env 配置后刷新页面</p>
          </div>
        </div>
      )}

      {ready && (
        <>
          <div className="absolute top-1/3 left-1/2 -translate-x-1/2 -translate-y-1/2 group cursor-pointer z-10">
            <div className="relative">
              <div className="absolute inset-0 rounded-full animate-ping scale-[2.5]"
                style={{ background: 'rgba(6,78,59,0.1)' }} />
              <div className="w-8 h-8 bg-white rounded-lg flex items-center justify-center shadow-md border relative z-10 transition-transform group-hover:scale-110"
                style={{ borderColor: 'rgba(229,231,226,0.6)' }}>
                <span className="material-symbols-outlined !text-[16px]" style={{ color: 'var(--primaryContainer)' }}>sensors</span>
              </div>
              <div className="absolute top-10 left-1/2 -translate-x-1/2 px-2 py-0.5 rounded-md shadow border whitespace-nowrap"
                style={{
                  background: 'rgba(255,255,255,0.92)',
                  backdropFilter: 'blur(10px)',
                  borderColor: 'rgba(229,231,226,0.6)',
                }}>
                <span className="text-[10px] font-bold tracking-tight" style={{ color: 'var(--primaryContainer)' }}>NODE_ALPHA_01</span>
              </div>
            </div>
          </div>

          {/* Floating Map Controls */}
          <div className="absolute right-4 top-16 flex flex-col gap-2 z-10">
            <div className="glass-panel flex flex-col rounded-lg overflow-hidden shadow-lg"
              style={{ borderColor: 'rgba(255,255,255,0.4)' }}>
              <button
                className="p-2 border-none bg-transparent cursor-pointer transition-colors hover:bg-white/80"
                style={{ color: 'var(--onSurfaceVariant)', borderBottom: '1px solid rgba(229,231,226,0.3)' }}
                onClick={() => { const map = mapInstanceRef.current; if (map) map.zoomIn(); }}
              >
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                  <path d="M12 5v14M5 12h14" strokeLinecap="round" />
                </svg>
              </button>
              <button
                className="p-2 border-none bg-transparent cursor-pointer transition-colors hover:bg-white/80"
                style={{ color: 'var(--onSurfaceVariant)' }}
                onClick={() => { const map = mapInstanceRef.current; if (map) map.zoomOut(); }}
              >
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                  <path d="M5 12h14" strokeLinecap="round" />
                </svg>
              </button>
            </div>
            <button
              className="glass-panel p-2 rounded-lg shadow-lg flex items-center justify-center border-none cursor-pointer transition-colors hover:bg-white/80"
              style={{ color: 'var(--onSurfaceVariant)', borderColor: 'rgba(255,255,255,0.4)' }}
              onClick={() => {
                const map = mapInstanceRef.current
                const AMap = mapRef.current
                if (map && AMap) map.setCenter(new AMap.LngLat(RADAR_CENTER[0], RADAR_CENTER[1]))
              }}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <circle cx="12" cy="12" r="4" />
                <path d="M12 2v4M12 18v4M2 12h4M18 12h4" strokeLinecap="round" />
              </svg>
            </button>
            <button
              className="glass-panel p-2 rounded-lg shadow-lg flex items-center justify-center border-none cursor-pointer transition-colors hover:bg-white/80"
              style={{ color: 'var(--onSurfaceVariant)', borderColor: 'rgba(255,255,255,0.4)' }}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
            </button>
          </div>

          {/* Top bar label */}
          <div className="absolute top-4 left-3 h-10 flex items-center px-4 rounded z-10 shadow-sm"
            style={{ background: 'rgba(255,255,255,0.92)', backdropFilter: 'blur(10px)', border: '1px solid var(--hairline)' }}>
            <span className="text-[12px]" style={{ color: 'var(--secondary)', fontFamily: "'JetBrains Mono', monospace" }}>
              高德地图 · 雷达站定位
            </span>
            <span className="ml-3 flex items-center gap-1 text-[12px] font-medium" style={{ color: 'var(--targetGrep)' }}>
              <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--targetGrep)' }} /> LIVE
            </span>
          </div>

          {/* Legend */}
          <div className="absolute top-[60px] right-3 rounded p-3 z-10 shadow-sm flex gap-4"
            style={{ background: 'rgba(255,255,255,0.92)', backdropFilter: 'blur(10px)', border: '1px solid var(--hairline)' }}>
            <LegendDot color="var(--targetGrep)" label="跟踪" />
            <LegendDot color="var(--targetRead)" label="目标" />
            <LegendDot color="var(--targetThinking)" label="干扰" />
          </div>
        </>
      )}
    </div>
  )
}

function LegendDot({ color, label }: { color: string; label: string }) {
  return (
    <div className="flex items-center gap-1.5 text-[12px]" style={{ color: 'var(--secondary)' }}>
      <span className="w-2 h-2 rounded-full" style={{ background: color }} /> {label}
    </div>
  )
}
