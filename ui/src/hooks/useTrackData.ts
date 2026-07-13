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
