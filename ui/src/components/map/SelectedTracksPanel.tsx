/**
 * @file SelectedTracksPanel.tsx
 * @brief 展示用户当前选中的航迹及其关键运动、识别信息。
 * @details 面板内容由实时航迹集合派生；航迹删除或过期后相应选择应随上层状态一起移除。
 */

import { useMemo } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import type { RadarTrack } from '../../lib/protocol'

function latestSelectedTracks(tracks: RadarTrack[]): RadarTrack[] {
  const map = new Map<string, RadarTrack>()
  for (const t of tracks) {
    if (!t.interaction?.selected || !t.deviceId) continue
    const trackId = t.trackId ?? t.batchId
    if (trackId == null) continue
    const key = `${t.deviceId}:${trackId}`
    const prev = map.get(key)
    if (!prev || (t.pointIndex ?? 0) >= (prev.pointIndex ?? 0))
      map.set(key, t)
  }
  return [...map.values()].sort((a, b) => (a.trackId ?? 0) - (b.trackId ?? 0))
}

export default function SelectedTracksPanel() {
  const { tracks, acquisition } = useCore()

  const selected = useMemo(() => latestSelectedTracks(tracks), [tracks])

  if (!acquisition || selected.length === 0) return null

  return (
    <div
      className="absolute left-4 bottom-4 z-30 glass-panel rounded-lg shadow-lg overflow-hidden"
      style={{ borderColor: 'rgba(255,255,255,0.5)', maxWidth: 220, maxHeight: '40vh' }}
    >
      <div
        className="px-3 py-1.5 text-[10px] font-semibold shrink-0"
        style={{
          color: '#064e3b',
          borderBottom: '1px solid rgba(229,231,226,0.5)',
          background: 'rgba(6,78,59,0.06)',
        }}
      >
        已选目标 · {selected.length}
      </div>
      <div className="overflow-y-auto" style={{ maxHeight: 'calc(40vh - 28px)' }}>
        {selected.map(t => (
          <div
            key={`${t.deviceId}:${t.trackId}`}
            className="px-3 py-2"
            style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}
          >
            <div className="flex items-center gap-1.5 mb-1">
              <span className="w-1.5 h-1.5 rounded-full shrink-0" style={{ background: '#ea580c' }} />
              <span className="text-[11px] font-semibold" style={{ color: '#064e3b' }}>
                目标 #{t.trackId}
              </span>
            </div>
            <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-0.5 text-[10px] leading-snug">
              <span style={{ color: '#707974' }}>距离</span>
              <span style={{ color: '#191c1b', fontFamily: "'JetBrains Mono', monospace" }}>{t.slantRange.toFixed(1)} m</span>
              <span style={{ color: '#707974' }}>速度</span>
              <span style={{ color: '#191c1b', fontFamily: "'JetBrains Mono', monospace" }}>{t.radialSpeed.toFixed(2)} m/s</span>
              <span style={{ color: '#707974' }}>高度</span>
              <span style={{ color: '#191c1b', fontFamily: "'JetBrains Mono', monospace" }}>{t.altitude.toFixed(1)} m</span>
              <span style={{ color: '#707974' }}>方位</span>
              <span style={{ color: '#191c1b', fontFamily: "'JetBrains Mono', monospace" }}>{t.azimuth.toFixed(2)}°</span>
              <span style={{ color: '#707974' }}>俯仰</span>
              <span style={{ color: '#191c1b', fontFamily: "'JetBrains Mono', monospace" }}>{t.elevation.toFixed(2)}°</span>
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}
