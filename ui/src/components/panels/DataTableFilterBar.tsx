/**
 * @file DataTableFilterBar.tsx
 * @brief 根据列定义生成表格筛选控件并回传规范化过滤条件。
 * @details 筛选栏不拥有数据行；列键和取值语义必须与 columns.ts 的列定义保持一致。
 */

import type { ColumnDef, FilterValue } from '../../lib/columns'
import type { DeviceDataRow } from '../../lib/protocol'

interface Props {
  columns: ColumnDef<DeviceDataRow>[]
  filters: Record<string, FilterValue>
  onChange: (key: string, value: FilterValue) => void
  onClear: () => void
}

function activeCount(filters: Record<string, FilterValue>): number {
  return Object.values(filters).filter((v) => {
    if (v.type === 'range') return v.min != null || v.max != null
    if (v.type === 'text') return v.value.length > 0
    if (v.type === 'select') return v.value.length > 0
    return false
  }).length
}

const inputStyle = {
  background: '#ffffff',
  color: 'var(--body)',
  border: '1px solid rgba(191,201,195,0.4)',
  borderRadius: 4,
  outline: 'none',
  fontFamily: "'JetBrains Mono', monospace",
  fontSize: 10,
} as const

function RangeInput({ value, onChange }: { value: FilterValue & { type: 'range' }; onChange: (v: FilterValue) => void }) {
  return (
    <span className="inline-flex items-center gap-1">
      <span className="text-[10px]" style={{ color: '#9ca3af' }}>Min</span>
      <input
        type="number"
        placeholder="—"
        className="w-[58px] px-1.5 py-0.5"
        style={inputStyle}
        value={value.min ?? ''}
        onChange={(e) => onChange({ ...value, min: e.target.value ? Number(e.target.value) : undefined })}
      />
      <span className="text-[10px]" style={{ color: '#9ca3af' }}>Max</span>
      <input
        type="number"
        placeholder="—"
        className="w-[58px] px-1.5 py-0.5"
        style={inputStyle}
        value={value.max ?? ''}
        onChange={(e) => onChange({ ...value, max: e.target.value ? Number(e.target.value) : undefined })}
      />
    </span>
  )
}

function FilterRow({ column, value, onChange }: { column: ColumnDef<DeviceDataRow>; value: FilterValue; onChange: (v: FilterValue) => void }) {
  if (!column.filterable || !column.filterType) return null
  return (
    <div className="flex items-center gap-1.5 shrink-0">
      <span className="text-[10px] font-medium" style={{ color: '#6b7280' }}>{column.header}</span>
      {column.filterType === 'range' && (
        <RangeInput value={value as FilterValue & { type: 'range' }} onChange={onChange} />
      )}
      {column.filterType === 'text' && (
        <input
          type="text"
          placeholder="搜索…"
          className="w-[80px] px-1.5 py-0.5"
          style={inputStyle}
          value={(value as FilterValue & { type: 'text' }).value}
          onChange={(e) => onChange({ type: 'text', value: e.target.value })}
        />
      )}
      {column.filterType === 'select' && (
        <select
          className="text-[10px] px-1 py-0.5 rounded"
          style={inputStyle}
          value={(value as FilterValue & { type: 'select' }).value}
          onChange={(e) => onChange({ type: 'select', value: e.target.value })}
        >
          <option value="">全部</option>
          {(column as any).options?.map((o: string) => (
            <option key={o} value={o}>{o}</option>
          )) ?? (
            <>
              <option value="drone">drone</option>
              <option value="bird">bird</option>
              <option value="clutter">clutter</option>
              <option value="unknown">unknown</option>
            </>
          )}
        </select>
      )}
    </div>
  )
}

export default function DataTableFilterBar({ columns, filters, onChange, onClear }: Props) {
  const count = activeCount(filters)
  const filterableCols = columns.filter((c) => c.filterable)

  return (
    <div
      className="flex items-center gap-3 px-5 py-1.5 overflow-x-auto shrink-0"
      style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}
    >
      {filterableCols.map((col) => (
        <FilterRow
          key={col.key}
          column={col}
          value={filters[col.key] ?? { type: col.filterType ?? 'text', value: '' } as FilterValue}
          onChange={(v) => onChange(col.key, v)}
        />
      ))}
      {count > 0 && (
        <button
          className="text-[10px] px-2 py-0.5 rounded border-none cursor-pointer shrink-0"
          style={{ color: 'var(--error)', background: 'rgba(239,68,68,0.06)' }}
          onClick={onClear}
        >
          清除 ({count})
        </button>
      )}
    </div>
  )
}
