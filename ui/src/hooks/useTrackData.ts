import { useState, useEffect, useRef } from 'react'
import { DeviceType, type RadarTrack, type LevelSensorData, type OptoelectronicData, type GpsData, type DeviceDataRow } from '../lib/protocol'

function now() { return Date.now() }

let nextBatchId = 100
function genBatchId() { return ++nextBatchId }

let nextSignalId = 5000
function genSignalId() { return ++nextSignalId }

function genRadarRow(label: string): RadarTrack {
  return {
    batchId: genBatchId(),
    slantRange: 200 + Math.random() * 4800,
    azimuth: Math.random() * 360,
    elevation: (Math.random() - 0.5) * 40,
    altitude: 20 + Math.random() * 500,
    radialSpeed: (Math.random() - 0.5) * 60,
    amplitude: -30 + Math.random() * 40,
    snr: 5 + Math.random() * 25,
    extrapolateFlag: Math.random() > 0.85 ? 1 : 0,
    targetType: Math.floor(Math.random() * 5) + 1,
    signalId: genSignalId(),
    sceneNum: Math.floor(Math.random() * 4) + 1,
    timestamp: now(),
  }
}

function genLevelRow(): LevelSensorData {
  return {
    pitch: (Math.random() - 0.5) * 2,
    roll: (Math.random() - 0.5) * 2,
    heading: 30 + Math.random() * 5,
    timestamp: now(),
  }
}

function genGpsRow(): GpsData {
  return {
    longitude: 118.65 + Math.random() * 0.01,
    latitude: 31.93 + Math.random() * 0.01,
    altitude: 5 + Math.random() * 20,
    coordinateSystem: 'WGS-84',
    timestamp: now(),
  }
}

const workStatuses: Array<'idle' | 'searching' | 'tracking'> = ['idle', 'searching', 'tracking']

function genOptoRow(): OptoelectronicData {
  return {
    turntableAzimuth: (Math.random() - 0.5) * 180,
    turntableElevation: (Math.random() - 0.5) * 45,
    targetAzimuth: Math.random() * 360,
    targetElevation: (Math.random() - 0.5) * 30,
    workStatus: workStatuses[Math.floor(Math.random() * workStatuses.length)],
    timestamp: now(),
  }
}

function generateBatch(deviceType: DeviceType): DeviceDataRow[] {
  switch (deviceType) {
    case DeviceType.RADAR: {
      const count = 5 + Math.floor(Math.random() * 11)
      return Array.from({ length: count }, () => genRadarRow('radar'))
    }
    case DeviceType.LEVEL_SENSOR:
      return [genLevelRow()]
    case DeviceType.GPS:
      return [genGpsRow()]
    case DeviceType.OPTOELECTRONIC:
      return Array.from({ length: 3 + Math.floor(Math.random() * 6) }, () => genOptoRow())
    default:
      return []
  }
}

function mergeRows(prev: DeviceDataRow[], incoming: DeviceDataRow[], deviceType: DeviceType): DeviceDataRow[] {
  if (deviceType === DeviceType.RADAR) {
    return [...prev, ...incoming].slice(-200)
  }
  if (deviceType === DeviceType.OPTOELECTRONIC) {
    return incoming.slice(-200)
  }
  // level sensor & GPS: keep all recent
  return [...prev, ...incoming].slice(-200)
}

export interface TrackDataResult {
  rows: DeviceDataRow[]
  deviceType: DeviceType
  isLive: boolean
  sourceLabel: string
}

export function useTrackData(deviceType: DeviceType, sourceLabel: string): TrackDataResult {
  const [rows, setRows] = useState<DeviceDataRow[]>([])
  const [isLive, setIsLive] = useState(false)
  const queueRef = useRef<DeviceDataRow[]>([])

  // Reset when deviceType or sourceLabel changes
  useEffect(() => {
    setRows([])
    setIsLive(false)
    queueRef.current = []
  }, [deviceType, sourceLabel])

  // Data generation at device-native rate
  useEffect(() => {
    const rate = deviceType === DeviceType.OPTOELECTRONIC ? 100 : 1000

    const id = setInterval(() => {
      const batch = generateBatch(deviceType)
      queueRef.current.push(...batch)
    }, rate)

    return () => clearInterval(id)
  }, [deviceType])

  // Flush queue at 1s
  useEffect(() => {
    const id = setInterval(() => {
      if (queueRef.current.length > 0) {
        const batch = queueRef.current
        queueRef.current = []
        setRows(prev => mergeRows(prev, batch, deviceType))
        setIsLive(true)
      }
    }, 1000)

    return () => clearInterval(id)
  }, [deviceType])

  return { rows, deviceType, isLive, sourceLabel }
}
