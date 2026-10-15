/**
 * @file MapRadarView.tsx
 * @brief 在地图上组合雷达阵面、实时航迹、历史轨迹和交互覆盖层。
 * @details 本组件只拥有当前激活的地图：在线使用原 AMap 远山黛，离线使用 Leaflet 本地瓦片。
 * 子图层只接收规范化设备与航迹数据，不直接请求核心 API，不新增航迹缓存或接收链。
 */

import { useEffect, useMemo, useRef, useState } from 'react'
import * as L from 'leaflet'
import AMapLoader from '@amap/amap-jsapi-loader'
import { amapViewport, leafletViewport, type MapViewport } from './mapViewport'
import 'leaflet/dist/leaflet.css'
import PPIOverlay from './PPIOverlay'
import TrackLayer from './TrackLayer'
import HistoryTrackLayer from './HistoryTrackLayer'
import TrackReplayControls from './TrackReplayControls'
import SelectedTracksPanel from './SelectedTracksPanel'
import { type RadarDevice, RADAR_CENTER } from './types'
import { useCore } from '../../hooks/useCoreApi'
import { applyLiveRadarPose } from '../../lib/liveRadarPose'

/** @brief 设备名称仅作为文本节点加入标记，避免台账内容被解释为 HTML。 */
function markerContent(device: RadarDevice) {
  const content = document.createElement('div')
  content.className = 'rounded-lg bg-white/90 px-2 py-1 text-[10px] shadow-md whitespace-nowrap'
  const icon = document.createElement('span')
  icon.className = 'material-symbols-outlined !text-[18px]'
  icon.textContent = 'sensors'
  content.append(icon, document.createTextNode(` ${device.name}`))
  return content
}

/** @brief 地图只接收最终设备视图和原有采集动作，不拥有航迹历史或 Command。 */
interface Props {
  devices: RadarDevice[] ///< 台账设备与安装参数，上层拥有。
  idleActionLabel?: string ///< 空闲时显示的采集动作名称。
  idleActionPending?: boolean ///< 上层控制请求是否仍在处理。
  idleActionError?: string ///< 原控制链返回的本地失败信息。
  onIdleAction?: () => void ///< 上层唯一采集动作入口，可不提供。
}

/** @brief 在线恢复 AMap 远山黛，离线保留本地瓦片；切换地图实例时保留视图位置，不重建业务状态。 */
export default function MapRadarView({ devices, idleActionLabel, idleActionPending = false, idleActionError = '', onIdleAction }: Props) {
  const {
    acquisition, devices: deviceStates, checkedDeviceIds, historyTracks,
    replay, isHistoryView, liveTracksNotice, historyViewNotice, deviceHeartbeat,
  } = useCore()
  const shownDevices = useMemo(() =>
    isHistoryView ? devices : devices.map(device => applyLiveRadarPose(device, deviceHeartbeat?.payload.radarPose)),
  [devices, deviceHeartbeat, isHistoryView])
  const containerRef = useRef<HTMLDivElement>(null)
  const leafletRef = useRef<L.Map | null>(null)
  const onlineRef = useRef<{ map: any; AMap: any } | null>(null)
  /** @brief 标记移除由地图 owner 在销毁实例前执行，避免旧 SDK 标记残留。 */
  const clearMarkersRef = useRef<(() => void) | null>(null)
  /** @brief 仅保存中心经纬度（度）与缩放级别；不保存任何航迹或业务状态。 */
  const savedView = useRef<{ lng: number; lat: number; zoom: number }>({ lng: RADAR_CENTER[0], lat: RADAR_CENTER[1], zoom: 14 })
  const initialFitAllowed = useRef(true)
  const [mode, setMode] = useState<'offline' | 'online'>('offline')
  /** @brief 离线来源只决定本地底图，不影响在线 SDK 或采集状态。 */
  const [offlineSource, setOfflineSource] = useState<'mbtiles' | 'xyz'>('mbtiles')
  const [offlineInfo, setOfflineInfo] = useState<{
    name: string; bounds: [number, number, number, number]; minzoom: number; maxzoom: number
  } | null>(null)
  const [viewport, setViewport] = useState<MapViewport | null>(null)
  const [ready, setReady] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [mbtilesError, setMbtilesError] = useState<string | null>(null)

  /** @brief 元数据只属于离线来源；迟到的请求结果不能移动在线地图或覆盖其错误状态。 */
  useEffect(() => {
    const controller = new AbortController()
    void fetch('/map/metadata', { signal: controller.signal })
      .then(async response => {
        if (!response.ok) throw new Error(await response.text())
        return response.json()
      })
      .then(info => { if (!controller.signal.aborted) setOfflineInfo(info) })
      .catch(reason => { if (!controller.signal.aborted) setMbtilesError(`MBTiles 未加载：${String(reason)}`) })
    return () => controller.abort()
  }, [])

  /** @brief 一次只运行当前地图引擎；原生 AMap 保留远山黛，离线不加载 SDK；异步加载取消后不创建地图。 */
  useEffect(() => {
    if (!containerRef.current) return
    setReady(false)
    setViewport(null)
    setError(null)
    const { lng, lat, zoom } = savedView.current
    if (mode === 'offline') {
      const map = L.map(containerRef.current, { zoomControl: false, zoomAnimation: false,
        fadeAnimation: false, minZoom: 0, maxZoom: 20 }).setView([lat, lng], zoom)
      leafletRef.current = map
      map.on('movestart', () => { initialFitAllowed.current = false })
      L.control.scale({ imperial: false }).addTo(map)
      const resize = new ResizeObserver(() => map.invalidateSize())
      resize.observe(containerRef.current)
      setViewport(leafletViewport(map))
      setReady(true)
      return () => {
        const center = map.getCenter()
        savedView.current = { lng: center.lng, lat: center.lat, zoom: map.getZoom() }
        clearMarkersRef.current?.()
        clearMarkersRef.current = null
        resize.disconnect()
        map.remove()
        leafletRef.current = null
      }
    }
    const key = import.meta.env.VITE_GAODE_API_KEY as string | undefined
    if (!key) { setError('在线地图缺少高德 Key，请配置后重新构建 UI；离线地图仍可使用'); return }
    ;(window as any)._AMapSecurityConfig = {
      securityJsCode: import.meta.env.VITE_GAODE_SECURITY_CODE || '',
    }
    let cancelled = false
    let map: any = null
    const timeout = window.setTimeout(() => {
      cancelled = true
      setError('在线地图加载超时，请检查网络，或切换离线地图')
    }, 15000)
    void AMapLoader.load({ key, version: '2.0', plugins: ['AMap.Scale', 'AMap.ToolBar'] })
      .then(AMap => {
        if (cancelled || !containerRef.current) return
        map = new AMap.Map(containerRef.current, {
          zoom: Math.max(3, Math.min(20, zoom)), center: [lng, lat], viewMode: '2D',
          mapStyle: 'amap://styles/whitesmoke', features: ['bg', 'road', 'building', 'point'],
          showIndoorMap: false, showLogo: false, resizeEnable: true,
        })
        onlineRef.current = { map, AMap }
        window.clearTimeout(timeout)
        setViewport(amapViewport(map, AMap))
        setReady(true)
      })
      .catch(() => {
        window.clearTimeout(timeout)
        if (!cancelled) setError('高德在线地图加载失败，请检查网络和 Key 配置，或切换离线地图')
      })
    return () => {
      cancelled = true
      window.clearTimeout(timeout)
      if (map) {
        const center = map.getCenter()
        savedView.current = { lng: center.getLng(), lat: center.getLat(), zoom: map.getZoom() }
        clearMarkersRef.current?.()
        clearMarkersRef.current = null
        map.destroy()
      }
      onlineRef.current = null
    }
  }, [mode])

  /** @brief 标记只随当前实例和最终设备视图更新；名称使用 DOM 文本，SDK 之间不传递原生标记。 */
  useEffect(() => {
    clearMarkersRef.current?.()
    clearMarkersRef.current = null
    if (!ready || !viewport) return
    const leaflet = leafletRef.current
    const online = onlineRef.current
    if (mode === 'offline' && leaflet) {
      const markers = shownDevices.map(device => L.marker([device.lat, device.lng], { interactive: false,
        icon: L.divIcon({ html: markerContent(device), className: 'radar-map-marker', iconSize: [32, 32], iconAnchor: [16, 16] }),
      }).addTo(leaflet))
      clearMarkersRef.current = () => { markers.forEach(marker => marker.remove()) }
    } else if (mode === 'online' && online) {
      const markers = shownDevices.map(device => new online.AMap.Marker({ position: [device.lng, device.lat],
        content: markerContent(device), offset: new online.AMap.Pixel(-16, -16), zIndex: 60 }))
      online.map.add(markers)
      clearMarkersRef.current = () => { online.map.remove(markers) }
    }
  }, [shownDevices, ready, viewport, mode])

  /** @brief 仅离线替换本地栅格图层；在线样式完全交给原 AMap SDK，不使用普通 XYZ 瓦片或颜色滤镜。 */
  useEffect(() => {
    const map = leafletRef.current
    const mbtiles = offlineSource === 'mbtiles'
    if (mode !== 'offline' || !map || !ready || !viewport) return
    if (mbtiles && !offlineInfo) { setError(null); return }
    const options: L.TileLayerOptions = { maxZoom: 20, noWrap: true, attribution: 'QGIS 离线地图' }
    map.setMaxZoom(20)
    map.setMinZoom(mbtiles ? offlineInfo!.minzoom : 0)
    if (mbtiles) {
      const [west, south, east, north] = offlineInfo!.bounds
      options.bounds = [[south, west], [north, east]]
      options.minNativeZoom = offlineInfo!.minzoom
      options.maxNativeZoom = offlineInfo!.maxzoom
      if (initialFitAllowed.current) {
        map.fitBounds([[south, west], [north, east]], { padding: [30, 30], maxZoom: offlineInfo!.maxzoom })
        initialFitAllowed.current = false
      }
    }
    const layer = L.tileLayer(mbtiles ? '/map/tiles/{z}/{x}/{y}' : '/map/xyz/{z}/{x}/{y}', options).addTo(map)
    /** @brief 缺本地瓦片只提示，不自动联网或阻断航迹画布。 */
    const failed = () => setError(mbtiles ? '地图包内缺少当前瓦片；航迹显示继续工作' : '当前区域没有可用的 z/x/y 瓦片；航迹显示继续工作')
    layer.on('tileerror', failed)
    setError(null)
    return () => { layer.off('tileerror', failed); layer.remove() }
  }, [mode, offlineSource, offlineInfo, ready, viewport])

  return (
    <div className="absolute inset-0 isolate">
      <div key={mode} ref={containerRef} className="absolute inset-0 z-0" />

      {!ready && <div className="absolute inset-0 flex items-center justify-center">{mode === 'online' ? '正在加载在线地图...' : '正在打开本地地图...'}</div>}
      <div className="absolute left-4 top-4 z-30 rounded-lg bg-white/90 px-3 py-2 text-[11px] shadow">
        <div className="flex items-center gap-2 flex-wrap" role="group" aria-label="地图来源">
          <span style={{ color: 'var(--secondary)' }}>地图</span>
          <button type="button" aria-pressed={mode === 'online'} onClick={() => { initialFitAllowed.current = false; setMode('online') }}
            className="px-3 py-1 rounded border-none cursor-pointer"
            style={{ background: mode === 'online' ? 'var(--primaryContainer)' : 'var(--surfaceLow)', color: mode === 'online' ? '#fff' : 'var(--ink)' }}>
            在线地图
          </button>
          <button type="button" aria-pressed={mode === 'offline'} onClick={() => { initialFitAllowed.current = false; setMode('offline') }}
            className="px-3 py-1 rounded border-none cursor-pointer"
            style={{ background: mode === 'offline' ? 'var(--primaryContainer)' : 'var(--surfaceLow)', color: mode === 'offline' ? '#fff' : 'var(--ink)' }}>
            离线地图
          </button>
          {mode === 'offline' && <label className="flex items-center gap-1">
            <span>格式</span>
            <select value={offlineSource} className="rounded px-2 py-1 border cursor-pointer" aria-label="离线地图格式"
              style={{ background: 'var(--surfaceCard)', color: 'var(--ink)', borderColor: 'var(--hairline)' }}
              onChange={event => { initialFitAllowed.current = false; setOfflineSource(event.target.value as 'mbtiles' | 'xyz') }}>
              <option value="mbtiles">MBTiles</option>
              <option value="xyz">z/x/y 瓦片</option>
            </select>
          </label>}
        </div>
        {mode === 'offline' && <div className="mt-1" style={{ color: 'var(--secondary)' }}>
          {offlineSource === 'mbtiles' ? `${offlineInfo?.name || 'MBTiles 未加载'}（范围外无底图）` : 'z/x/y 本地瓦片'}
        </div>}
        {mode === 'offline' && offlineSource === 'mbtiles' && !offlineInfo
          ? <div style={{ color: mbtilesError ? 'var(--error)' : 'var(--secondary)' }}>{mbtilesError || '正在读取地图信息...'}</div>
          : error && <div style={{ color: 'var(--error)' }}>{error}</div>}
      </div>

      {isHistoryView && <TrackReplayControls />}
      {ready && viewport && (
        <>
          {!isHistoryView && devices.length > 0 && checkedDeviceIds.length === 0 && (
            <div
              className="absolute left-1/2 top-20 z-30 -translate-x-1/2 px-3 py-1.5 rounded-lg text-[11px] shadow-md"
              style={{ background: 'rgba(255,255,255,0.95)', color: '#92400e', border: '1px solid rgba(251,191,36,0.5)' }}
            >
              请在左侧勾选要显示的雷达设备
            </div>
          )}
          {historyViewNotice && (
            <div
              className="absolute left-1/2 top-20 z-30 -translate-x-1/2 px-3 py-1.5 rounded-lg text-[11px] shadow-md"
              style={{ background: 'rgba(255,255,255,0.95)', color: '#b45309', border: '1px solid rgba(251,191,36,0.5)' }}
            >
              {historyViewNotice}
            </div>
          )}
          {liveTracksNotice && !isHistoryView && (
            <div
              className="absolute left-1/2 top-20 z-30 -translate-x-1/2 px-3 py-2 rounded-lg text-[11px] shadow-md flex items-center gap-3"
              style={{ background: 'rgba(255,255,255,0.95)', color: 'var(--secondary)', border: '1px solid var(--hairline)' }}
            >
              <span>{idleActionError || liveTracksNotice}</span>
              {onIdleAction && idleActionLabel && (
                <button
                  className="px-2.5 py-1 rounded border-none text-[10px] font-semibold text-white cursor-pointer disabled:opacity-50"
                  style={{ background: idleActionError ? '#991b1b' : 'var(--primaryContainer)' }}
                  onClick={onIdleAction}
                  disabled={idleActionPending}
                >
                  {idleActionPending ? '启动中...' : idleActionLabel}
                </button>
              )}
            </div>
          )}
          {isHistoryView && !replay?.loading && (replay?.error || replay?.pageCount === 0) && (
            <div
              className="absolute left-1/2 top-32 z-30 -translate-x-1/2 px-3 py-1.5 rounded-lg text-[11px] shadow-md"
              style={{ background: 'rgba(255,255,255,0.95)', color: replay?.error ? 'var(--error)' : 'var(--secondary)', border: '1px solid var(--hairline)' }}
            >
              {replay?.error || '所选范围没有已提交的归档航迹'}
            </div>
          )}
          {/* PPI overlays — 仅勾选的设备扫描 */}
          {shownDevices.map(d => {
            const checked = checkedDeviceIds.includes(d.id)
            const hasFault = deviceStates.some(ds => ds.state === 'Fault')
            return (
              <PPIOverlay
                key={d.id}
                device={d}
                mapInstance={viewport}
                connected={true}
                acquiring={checked && acquisition && !isHistoryView}
                hasFault={hasFault}
              />
            )
          })}
          {/* 历史航迹层（淡色，与实时层分离） */}
          {isHistoryView && (
            <HistoryTrackLayer
              hiddenDeviceIds={devices.filter(d => !checkedDeviceIds.includes(d.id)).map(d => d.id)}
              points={historyTracks}
              mapInstance={viewport}
            />
          )}
          {/* 实时航迹层 */}
          {!isHistoryView && shownDevices.filter(d => checkedDeviceIds.includes(d.id)).map(d => (
            <TrackLayer
              key={`track-${d.id}`}
              device={d}
              mapInstance={viewport}
            />
          ))}
          {!isHistoryView && <SelectedTracksPanel />}

          {/* Floating Map Controls */}
          <div className="absolute right-4 top-16 flex flex-col gap-2 z-10">
            <div className="glass-panel flex flex-col rounded-lg overflow-hidden shadow-lg"
              style={{ borderColor: 'rgba(255,255,255,0.4)' }}>
              <button
                className="p-2 border-none bg-transparent cursor-pointer transition-colors hover:bg-white/80"
                style={{ color: 'var(--onSurfaceVariant)', borderBottom: '1px solid rgba(229,231,226,0.3)' }}
                onClick={() => { if (mode === 'offline') leafletRef.current?.zoomIn(); else onlineRef.current?.map.zoomIn(); }}
              >
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                  <path d="M12 5v14M5 12h14" strokeLinecap="round" />
                </svg>
              </button>
              <button
                className="p-2 border-none bg-transparent cursor-pointer transition-colors hover:bg-white/80"
                style={{ color: 'var(--onSurfaceVariant)' }}
                onClick={() => { if (mode === 'offline') leafletRef.current?.zoomOut(); else onlineRef.current?.map.zoomOut(); }}
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
                const device = shownDevices.find(item => checkedDeviceIds.includes(item.id))
                if (mode === 'online') {
                  onlineRef.current?.map.setCenter(device ? [device.lng, device.lat] : RADAR_CENTER)
                } else {
                  const map = leafletRef.current
                  if (!map) return
                  if (device) map.panTo([device.lat, device.lng])
                  else if (offlineInfo) {
                    const [west, south, east, north] = offlineInfo.bounds
                    map.fitBounds([[south, west], [north, east]])
                  }
                }
              }}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <circle cx="12" cy="12" r="4" />
                <path d="M12 2v4M12 18v4M2 12h4M18 12h4" strokeLinecap="round" />
              </svg>
            </button>
          </div>
        </>
      )}
    </div>
  )
}
