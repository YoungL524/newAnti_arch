/**
 * @file DataTable.tsx
 * @brief 对设备数据执行列选择、筛选、排序和虚拟化表格展示。
 * @details 输入行由实时状态提供；组件只维护视图派生状态，避免复制或回写核心航迹数据。
 */

import { useState, useMemo, useRef, useEffect, useCallback } from 'react'
import { DeviceType } from '../../lib/protocol'
import type { DeviceDataRow, RadarTrack } from '../../lib/protocol'
import { DEVICE_COLUMNS, DEVICE_FORMATTERS, applyFilters } from '../../lib/columns'
import type { ColumnDef, FilterValue } from '../../lib/columns'
import DataTableFilterBar from './DataTableFilterBar'
import { useCore } from '../../hooks/useCoreApi'

interface Props {
  deviceType: DeviceType
  rows: DeviceDataRow[]
  isLive: boolean
}

const tableStyle = { borderSpacing: '0 1px' } as const
const stickyHeaderStyle = {
  color: '#9ca3af',
  background: 'rgba(255,255,255,0.55)',
  backdropFilter: 'blur(8px)',
} as const

function renderCell(key: string, row: DeviceDataRow, deviceType: DeviceType): string {
  const val = (row as any)[key]
  if (val == null) return '—'
  const fmts = DEVICE_FORMATTERS[deviceType]
  const fmt = fmts?.[key]
  return fmt ? fmt(val) : String(val)
}

export default function DataTable({ deviceType, rows, isLive }: Props) {
  const { isHistoryView, setTrackInteraction } = useCore()
  const [filters, setFilters] = useState<Record<string, FilterValue>>({})
  const [filterOpen, setFilterOpen] = useState(false)
  const [userScrolled, setUserScrolled] = useState(false)
  const bodyRef = useRef<HTMLDivElement>(null)
  const endRef = useRef<HTMLDivElement>(null)

  const columns: ColumnDef<DeviceDataRow>[] = useMemo(() => DEVICE_COLUMNS[deviceType] ?? [], [deviceType])

  const filteredRows = useMemo(
    () => applyFilters(rows, filters, columns),
    [rows, filters, columns],
  )

  const handleFilterChange = useCallback((key: string, value: FilterValue) => {
    setFilters((prev) => ({ ...prev, [key]: value }))
  }, [])

  const handleClearFilters = useCallback(() => setFilters({}), [])

  // auto-scroll to bottom unless user scrolled up
  useEffect(() => {
    if (!userScrolled) {
      endRef.current?.scrollIntoView({ behavior: 'smooth' })
    }
  }, [filteredRows, userScrolled])

  const handleScroll = useCallback(() => {
    const el = bodyRef.current
    if (!el) return
    const distFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight
    setUserScrolled(distFromBottom > 50)
  }, [])

  const filterCount = Object.values(filters).filter((v) => {
    if (v.type === 'range') return v.min != null || v.max != null
    if (v.type === 'text') return v.value.length > 0
    if (v.type === 'select') return v.value.length > 0
    return false
  }).length

  return (
    <div className="flex flex-col flex-1 overflow-hidden relative">
      {/* Toolbar */}
      <div className="flex items-center justify-between shrink-0 px-5 py-1"
        style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
        <div className="flex items-center gap-2">
          {isLive && (
            <span className="w-1.5 h-1.5 rounded-full animate-pulse"
              style={{ background: 'var(--primaryContainer)' }} />
          )}
          <span className="text-[10px] font-medium" style={{ color: '#9ca3af' }}>
            {isLive ? `实时 · ${filteredRows.length} 条` : `共 ${filteredRows.length} 条`}
          </span>
        </div>
        <button
          className="relative text-[11px] px-2 py-0.5 rounded border cursor-pointer transition-all font-medium"
          style={{
            background: filterOpen ? '#ffffff' : 'transparent',
            color: filterOpen ? 'var(--primaryContainer)' : '#9ca3af',
            borderColor: filterOpen ? 'rgba(6,78,59,0.05)' : 'transparent',
            fontFamily: "'Inter', sans-serif",
          }}
          onClick={() => setFilterOpen((v) => !v)}
        >
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8"
            style={{ display: 'inline', verticalAlign: -2, marginRight: 3 }}>
            <path d="M3 6h18M7 12h10M10 18h4" strokeLinecap="round" />
          </svg>
          筛选{filterCount > 0 ? ` (${filterCount})` : ''}
        </button>
      </div>

      {/* Filter bar */}
      {filterOpen && (
        <DataTableFilterBar
          columns={columns}
          filters={filters}
          onChange={handleFilterChange}
          onClear={handleClearFilters}
        />
      )}

      {/* Table */}
      <div ref={bodyRef} className="flex-1 overflow-y-auto" onScroll={handleScroll}>
        <table className="w-full text-left border-separate" style={tableStyle}>
          <thead className="text-[10px] font-bold uppercase tracking-wider sticky top-0 z-10"
            style={stickyHeaderStyle}>
            <tr>
              {columns.map((col) => (
                <th
                  key={col.key}
                  className={`pt-1.5 pb-1.5 font-medium ${col.width ?? ''}`}
                  style={{
                    textAlign: col.align ?? 'left',
                    paddingLeft: col.key === columns[0]?.key ? '1.25rem' : undefined,
                    paddingRight: col.key === columns[columns.length - 1]?.key ? '1.25rem' : undefined,
                  }}
                >
                  {col.header}
                </th>
              ))}
            </tr>
          </thead>
          <tbody className="text-[13px]" style={{ fontFamily: "'Inter', 'Microsoft YaHei', sans-serif" }}>
            {filteredRows.length === 0 ? (
              <tr>
                <td className="py-8 pl-5 text-center" colSpan={columns.length}
                  style={{ color: '#9ca3af' }}>
                  暂无航迹数据 &mdash; 启动监视任务后将自动填充
                </td>
              </tr>
            ) : (
              filteredRows.map((row, i) => (
                <tr
                  key={i}
                  className="group transition-colors hover:bg-stone-50"
                  style={{ borderTop: i > 0 ? '1px solid rgba(229,231,226,0.3)' : 'none' }}
                  onClick={() => {
                    if (isHistoryView || deviceType !== DeviceType.RADAR) return
                    const track = row as RadarTrack
                    const deviceId = track.deviceId
                    const trackId = track.trackId
                    if (typeof deviceId !== 'string' || typeof trackId !== 'number') return
                    const { interaction } = track
                    const selected = !(interaction?.selected ?? false)
                    const trackingEnabled = interaction?.trackingEnabled ?? false
                    setTrackInteraction(deviceId, trackId, selected, trackingEnabled)
                  }}
                >
                  {columns.map((col) => {
                    const isTs = col.key === 'timestamp'
                    const isId = col.key === 'targetId' || col.key === 'trackId'
                    const cell = renderCell(col.key, row, deviceType)
                    return (
                      <td
                        key={col.key}
                        className="py-1.5"
                        style={{
                          textAlign: col.align ?? 'left',
                          paddingLeft: col.key === columns[0]?.key ? '1.25rem' : undefined,
                          paddingRight: col.key === columns[columns.length - 1]?.key ? '1.25rem' : undefined,
                          fontFamily: isTs || isId ? "'JetBrains Mono', monospace" : undefined,
                          fontSize: isTs || isId ? 11 : undefined,
                          color: isTs ? '#9ca3af' : isId ? 'var(--primaryBright)' : 'var(--body)',
                          fontWeight: isId ? 600 : undefined,
                        }}
                      >
                        {isId ? `[${cell}]` : cell}
                      </td>
                    )
                  })}
                </tr>
              ))
            )}
          </tbody>
        </table>
        <div ref={endRef} />
      </div>

      {/* Jump-to-bottom */}
      {userScrolled && (
        <button
          className="absolute bottom-2 right-4 z-10 text-[10px] px-2.5 py-1 rounded-full border-none cursor-pointer shadow-sm"
          style={{
            background: '#ffffff',
            color: 'var(--primaryContainer)',
            fontFamily: "'Inter', sans-serif",
          }}
          onClick={() => {
            setUserScrolled(false)
            endRef.current?.scrollIntoView({ behavior: 'smooth' })
          }}
        >
          ↓ 回到底部
        </button>
      )}
    </div>
  )
}

