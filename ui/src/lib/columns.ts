/**
 * @file columns.ts
 * @brief 定义各类设备数据表的列、取值器、单位和筛选元数据。
 * @details 列定义把 protocol.ts 的原始字段转换为展示值，是 DataTable 与筛选栏共享的唯一列语义来源。
 */

import { DeviceType, type RadarTrack, type LevelSensorData, type OptoelectronicData, type GpsData, type DeviceDataRow } from './protocol'

export type FilterType = 'range' | 'text' | 'select'

export interface ColumnDef<T = DeviceDataRow> {
  key: string
  header: string
  width?: string
  align?: 'left' | 'right' | 'center'
  filterable?: boolean
  filterType?: FilterType
  accessor?: (row: T) => unknown
}

export type FilterValue =
  | { type: 'range'; min?: number; max?: number }
  | { type: 'text'; value: string }
  | { type: 'select'; value: string }

// ── Formatters ────────────────────────────────────────────

function fmtNum(decimals: number) {
  return (v: number) => v.toFixed(decimals)
}

function formatTimestamp(ts: number) {
  const d = new Date(ts)
  return d.toLocaleTimeString('en-GB', { hour12: false }) + '.' +
    String(d.getMilliseconds()).padStart(3, '0')
}

export function formatWorkStatus(v: string) {
  const map: Record<string, string> = { idle: '空闲', searching: '搜索', tracking: '跟踪' }
  return map[v] ?? v
}

// ── Column definitions ────────────────────────────────────

export const RADAR_COLUMNS: ColumnDef<RadarTrack>[] = [
  { key: 'timestamp',       header: '时间戳',          width: 'w-[120px]' },
  { key: 'batchId',         header: '批号',            width: 'w-[55px]',  align: 'center', filterable: true, filterType: 'text' },
  { key: 'slantRange',      header: '斜距(m)',         width: 'w-[85px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'azimuth',         header: '方位角(°)',       width: 'w-[85px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'elevation',       header: '俯仰角(°)',       width: 'w-[85px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'altitude',        header: '目标高度(m)',      width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'radialSpeed',     header: '径向速度(m/s)',    width: 'w-[100px]', align: 'right',  filterable: true, filterType: 'range' },
  { key: 'amplitude',       header: '幅度(dB)',        width: 'w-[80px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'snr',             header: '信噪比(dB)',      width: 'w-[80px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'extrapolateFlag', header: '外推标志',         width: 'w-[65px]',  align: 'center' },
  { key: 'targetType',      header: '目标类型',         width: 'w-[65px]',  align: 'center' },
  { key: 'signalId',        header: '信号识别标识',     width: 'w-[80px]',  align: 'center', filterable: true, filterType: 'text' },
  { key: 'sceneNum',        header: '景幅',            width: 'w-[55px]',  align: 'center' },
]

export const LEVEL_SENSOR_COLUMNS: ColumnDef<LevelSensorData>[] = [
  { key: 'timestamp', header: '时间戳',    width: 'w-[120px]' },
  { key: 'pitch',     header: '俯仰角(°)', width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'roll',      header: '横滚角(°)', width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'heading',   header: '航向角(°)', width: 'w-[90px]',  align: 'right' },
]

export const GPS_COLUMNS: ColumnDef<GpsData>[] = [
  { key: 'timestamp',       header: '时间戳',    width: 'w-[120px]' },
  { key: 'longitude',       header: '经度',      width: 'w-[110px]', align: 'right',  filterable: true, filterType: 'range' },
  { key: 'latitude',        header: '纬度',      width: 'w-[100px]', align: 'right',  filterable: true, filterType: 'range' },
  { key: 'altitude',        header: '海拔(m)',   width: 'w-[85px]',  align: 'right' },
  { key: 'coordinateSystem',header: '坐标系',    width: 'w-[75px]',  align: 'center' },
]

export const OPTOELECTRONIC_COLUMNS: ColumnDef<OptoelectronicData>[] = [
  { key: 'timestamp',          header: '时间戳',        width: 'w-[120px]' },
  { key: 'turntableAzimuth',   header: '转台方位(°)',   width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'turntableElevation', header: '转台俯仰(°)',   width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'targetAzimuth',      header: '目标方位(°)',   width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'targetElevation',    header: '目标俯仰(°)',   width: 'w-[90px]',  align: 'right',  filterable: true, filterType: 'range' },
  { key: 'workStatus',         header: '工作状态',       width: 'w-[70px]',  align: 'center', filterable: true, filterType: 'select' },
]

// ── Column registry ───────────────────────────────────────

export const DEVICE_COLUMNS: Record<DeviceType, ColumnDef<any>[]> = {
  [DeviceType.UNKNOWN]: [],
  [DeviceType.RADAR]: RADAR_COLUMNS,
  [DeviceType.LEVEL_SENSOR]: LEVEL_SENSOR_COLUMNS,
  [DeviceType.OPTOELECTRONIC]: OPTOELECTRONIC_COLUMNS,
  [DeviceType.GPS]: GPS_COLUMNS,
}

// ── Cell formatters per key ────────────────────────────────

const radarFormatters: Record<string, (v: any) => string> = {
  timestamp:       (v: number) => formatTimestamp(v),
  slantRange:      fmtNum(1),
  azimuth:         fmtNum(1),
  elevation:       fmtNum(1),
  altitude:        fmtNum(1),
  radialSpeed:     fmtNum(1),
  amplitude:       fmtNum(1),
  snr:             fmtNum(1),
  extrapolateFlag: (v: number) => v === 0 ? '实测' : '外推',
}

const levelFormatters: Record<string, (v: any) => string> = {
  timestamp: (v: number) => formatTimestamp(v),
  pitch:     fmtNum(2),
  roll:      fmtNum(2),
  heading:   fmtNum(2),
}

const gpsFormatters: Record<string, (v: any) => string> = {
  timestamp: (v: number) => formatTimestamp(v),
  longitude: fmtNum(6),
  latitude:  fmtNum(6),
  altitude:  fmtNum(1),
}

const optoFormatters: Record<string, (v: any) => string> = {
  timestamp:          (v: number) => formatTimestamp(v),
  turntableAzimuth:   fmtNum(1),
  turntableElevation: fmtNum(1),
  targetAzimuth:      fmtNum(1),
  targetElevation:    fmtNum(1),
  workStatus:         formatWorkStatus,
}

export const DEVICE_FORMATTERS: Record<DeviceType, Record<string, (v: any) => string>> = {
  [DeviceType.UNKNOWN]: {},
  [DeviceType.RADAR]: radarFormatters,
  [DeviceType.LEVEL_SENSOR]: levelFormatters,
  [DeviceType.OPTOELECTRONIC]: optoFormatters,
  [DeviceType.GPS]: gpsFormatters,
}

// ── Filter application ────────────────────────────────────

export function applyFilters<T extends DeviceDataRow>(
  rows: T[],
  filters: Record<string, FilterValue>,
  _columns: ColumnDef<T>[],
): T[] {
  const active = Object.entries(filters).filter(([, v]) => {
    if (v.type === 'range') return v.min != null || v.max != null
    if (v.type === 'text') return v.value.length > 0
    if (v.type === 'select') return v.value.length > 0
    return false
  })
  if (active.length === 0) return rows
  return rows.filter(row =>
    active.every(([key, filter]) => {
      const val = (row as any)[key]
      if (filter.type === 'range') {
        if (filter.min != null && val < filter.min) return false
        if (filter.max != null && val > filter.max) return false
        return true
      }
      if (filter.type === 'text') {
        return String(val).toLowerCase().includes(filter.value.toLowerCase())
      }
      if (filter.type === 'select') {
        return String(val) === filter.value
      }
      return true
    }),
  )
}
