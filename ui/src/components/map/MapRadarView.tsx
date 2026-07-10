import { useEffect, useRef, useState } from 'react'
import AMapLoader from '@amap/amap-jsapi-loader'
import PPIOverlay from './PPIOverlay'
import TrackLayer from './TrackLayer'
import HistoryTrackLayer from './HistoryTrackLayer'
import SelectedTracksPanel from './SelectedTracksPanel'
import { type RadarDevice, RADAR_CENTER } from './types'
import { useCore } from '../../hooks/useCoreApi'

const API_KEY = import.meta.env.VITE_GAODE_API_KEY as string
const SECURITY_CODE = import.meta.env.VITE_GAODE_SECURITY_CODE as string | undefined

// AMap security config must be set before any load() call
;(window as any)._AMapSecurityConfig = {
  securityJsCode: SECURITY_CODE || '',
}

function markNode(AMap: any, device: RadarDevice) {
  return new AMap.Marker({
    position: [device.lng, device.lat],
    content: '<div style="position:relative;cursor:pointer;">' +
      '<div style="position:absolute;top:4px;left:4px;width:24px;height:24px;border-radius:6px;background:rgba(6,78,59,0.25);animation:marker-ping 1.5s ease-in-out infinite;"></div>' +
      '<div style="width:32px;height:32px;background:#fff;border-radius:8px;display:flex;align-items:center;justify-content:center;box-shadow:0 2px 8px rgba(0,0,0,0.08);border:1px solid rgba(229,231,226,0.6);position:relative;">' +
      '<span class="material-symbols-outlined" style="font-size:18px;color:#064e3b;font-variation-settings:\'FILL\' 0,\'wght\' 300,\'GRAD\' 0,\'opsz\' 20;">sensors</span>' +
      '</div>' +
      '<div style="position:absolute;top:40px;left:50%;transform:translateX(-50%);padding:2px 10px;border-radius:6px;border:1px solid rgba(229,231,226,0.6);background:rgba(255,255,255,0.92);white-space:nowrap;box-shadow:0 2px 4px rgba(0,0,0,0.04);">' +
      `<span style="font-size:10px;font-weight:700;color:#064e3b;font-family:Inter,sans-serif;letter-spacing:-0.01em;">${device.name}</span>` +
      '</div>' +
      '</div>',
    offset: new AMap.Pixel(-16, -40),
    zIndex: 60,
  })
}

export default function MapRadarView({ devices }: { devices: RadarDevice[] }) {
  const { acquisition, devices: deviceStates, checkedDeviceIds, historyTracks, liveTracksNotice } = useCore()
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef = useRef<any>(null)
  const mapInstanceRef = useRef<any>(null)
  const nodeMarkersRef = useRef<any[]>([])
  const [ready, setReady] = useState(false)
  const [error, setError] = useState<string | null>(null)

  // Sync map markers with devices
  useEffect(() => {
    const AMap = mapRef.current
    const map = mapInstanceRef.current
    if (!AMap || !map) return

    nodeMarkersRef.current.forEach(m => map.remove(m))
    const newMarkers = devices.map(d => {
      const m = markNode(AMap, d)
      map.add(m)
      return m
    })
    nodeMarkersRef.current = newMarkers
  }, [devices, ready])

  // Init AMap
  useEffect(() => {
    if (!API_KEY) {
      setError('需要在 .env 中配置 VITE_GAODE_API_KEY')
      return
    }

    AMapLoader.load({
      key: API_KEY,
      version: '2.0',
      plugins: ['AMap.Scale', 'AMap.ToolBar'],
    })
      .then((AMap: any) => {
        mapRef.current = AMap

        try {
          const map = new AMap.Map(containerRef.current, {
            zoom: 14,
            center: RADAR_CENTER,
            mapStyle: 'amap://styles/whitesmoke',
            features: ['bg', 'road', 'building', 'point'],
            showIndoorMap: false,
            showLogo: false,
          })
          mapInstanceRef.current = map
          setReady(true)
        } catch (e: any) {
          console.error('[Amap] 地图实例化失败:', e)
          setError(`高德地图实例化失败\n${e?.message || String(e)}\n\n请检查：\n1. 安全密钥与 Key 是否绑定（高德开放平台 → 应用管理）\n2. 域名白名单是否包含 localhost\n3. 可尝试删除安全密钥，改用「域名白名单」方式`)
        }
      })
      .catch((err: any) => {
        const msg = err?.message || String(err)
        console.error('[Amap] 加载失败:', err)
        setError(`高德地图加载失败\n${msg || JSON.stringify(err)}\n\n请检查：\n1. Key「${API_KEY.slice(0, 8)}…」服务平台为「Web端(JS API)」\n2. 安全密钥与 Key 绑定且域名白名单包含 localhost\n3. 网络可访问 amap.com`)
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
            <button
              className="px-3 py-1 rounded-lg text-white text-[11px] font-medium border-none cursor-pointer"
              style={{ background: 'var(--primaryContainer)' }}
              onClick={() => window.location.reload()}
            >
              重试
            </button>
            <p className="text-[11px] mt-2" style={{ color: 'var(--secondary)' }}>检查 .env 配置后点击重试</p>
          </div>
        </div>
      )}

      {ready && (
        <>
          {devices.length > 0 && checkedDeviceIds.length === 0 && (
            <div
              className="absolute left-1/2 top-20 z-30 -translate-x-1/2 px-3 py-1.5 rounded-lg text-[11px] shadow-md"
              style={{ background: 'rgba(255,255,255,0.95)', color: '#92400e', border: '1px solid rgba(251,191,36,0.5)' }}
            >
              请在左侧勾选要显示的雷达设备
            </div>
          )}
          {liveTracksNotice && (
            <div
              className="absolute left-1/2 top-32 z-30 -translate-x-1/2 px-3 py-1.5 rounded-lg text-[11px] shadow-md"
              style={{ background: 'rgba(255,255,255,0.95)', color: 'var(--secondary)', border: '1px solid var(--hairline)' }}
            >
              {liveTracksNotice}
            </div>
          )}
          {/* PPI overlays — 仅勾选的设备扫描 */}
          {devices.map(d => {
            const checked = checkedDeviceIds.includes(d.id)
            const hasFault = deviceStates.some(ds => ds.state === 'Fault')
            return (
              <PPIOverlay
                key={d.id}
                device={d}
                mapInstance={mapInstanceRef.current}
                mapClass={mapRef.current}
                connected={true}
                acquiring={checked && acquisition}
                hasFault={hasFault}
              />
            )
          })}
          {/* 历史航迹层（淡色，与实时层分离） */}
          {devices.filter(d => checkedDeviceIds.includes(d.id)).map(d => (
            <HistoryTrackLayer
              key={`history-${d.id}`}
              device={d}
              points={historyTracks}
              mapInstance={mapInstanceRef.current}
              mapClass={mapRef.current}
            />
          ))}
          {/* 实时航迹层 */}
          {devices.filter(d => checkedDeviceIds.includes(d.id)).map(d => (
            <TrackLayer
              key={`track-${d.id}`}
              device={d}
              mapInstance={mapInstanceRef.current}
              mapClass={mapRef.current}
            />
          ))}
          <SelectedTracksPanel />

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
        </>
      )}
    </div>
  )
}
