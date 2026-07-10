import { useState, useEffect } from 'react'
import { DeviceType, type DeviceDataRow } from '../lib/protocol'
import { useCore } from './useCoreApi'

export interface TrackDataResult {
  rows: DeviceDataRow[]
  deviceType: DeviceType
  isLive: boolean
  sourceLabel: string
}

export function useTrackData(deviceType: DeviceType, sourceLabel: string): TrackDataResult {
  const { tracks, acquisition } = useCore()
  const [rows, setRows] = useState<DeviceDataRow[]>([])

  useEffect(() => {
    if (deviceType === DeviceType.RADAR && tracks.length > 0) {
      setRows(tracks as DeviceDataRow[])
    }
  }, [tracks, deviceType])

  // Reset when device type or source changes
  useEffect(() => {
    setRows([])
  }, [deviceType, sourceLabel])

  const isLive = deviceType === DeviceType.RADAR && acquisition && tracks.length > 0

  return { rows, deviceType, isLive, sourceLabel }
}
