/**
 * @file useTrackData.ts
 * @brief 订阅核心 WebSocket 航迹流并向 React 组件提供当前显示集合。
 * @details Hook 应用“初始快照 + 增量更新/删除”，并在断线或版本缺口时重建状态，避免无限追加历史点。
 */

import { useState, useEffect } from 'react'
import { DeviceType, type DeviceDataRow, type RadarTrack } from '../lib/protocol'
import { useCore } from './useCoreApi'

export interface TrackDataResult {
  rows: DeviceDataRow[]
  deviceType: DeviceType
  isLive: boolean
  sourceLabel: string
}

export function useTrackData(deviceType: DeviceType, sourceId: string, sourceLabel: string): TrackDataResult {
  const { tracks, historyTracks, isHistoryView, acquisition } = useCore()
  const [rows, setRows] = useState<DeviceDataRow[]>([])

  useEffect(() => {
    if (deviceType !== DeviceType.RADAR) {
      setRows([])
      return
    }
    if (isHistoryView) {
      const historicalRows: RadarTrack[] = historyTracks
        .filter(point => point.deviceId === sourceId)
        .map(point => ({
          batchId: point.targetId,
          deviceId: point.deviceId,
          trackId: point.targetId,
          slantRange: point.range,
          azimuth: point.azimuth * 180 / Math.PI,
          elevation: point.elevation * 180 / Math.PI,
          altitude: point.altitude,
          radialSpeed: point.radialSpeed,
          amplitude: 0,
          snr: 0,
          extrapolateFlag: 0,
          targetType: point.targetId,
          signalId: 0,
          sceneNum: point.sceneNum,
          timestamp: point.tsMs,
        }))
      setRows(historicalRows)
      return
    }
    setRows(tracks.filter(track => track.deviceId === sourceId))
  }, [tracks, historyTracks, isHistoryView, deviceType, sourceId])

  const isLive = !isHistoryView && deviceType === DeviceType.RADAR && acquisition && rows.length > 0

  return { rows, deviceType, isLive, sourceLabel }
}
