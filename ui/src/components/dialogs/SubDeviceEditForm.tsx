import { useState } from 'react'
import type { SubDevice, RadarDevice, RadarBrand } from '../map/types'
import { BRAND_LABELS } from '../map/types'

interface Props {
  device: SubDevice
  radarDevices: RadarDevice[]
  onSave: (updates: Partial<SubDevice>) => void
  onCancel: () => void
}

const DEVICE_META: Record<SubDevice['kind'], { icon: string; title: string }> = {
  opto: { icon: 'videocam', title: '编辑光学设备' },
  level: { icon: 'balance', title: '编辑水平仪' },
  gps: { icon: 'satellite_alt', title: '编辑 GPS 设备' },
}

const inputStyle: React.CSSProperties = {
  width: '100%',
  padding: '4px 8px',
  border: '1px solid var(--hairline)',
  borderRadius: 4,
  background: '#fff',
  color: 'var(--ink)',
  fontSize: 11,
  fontFamily: "'Inter', 'Microsoft YaHei', sans-serif",
  outline: 'none',
}

const labelStyle: React.CSSProperties = {
  fontSize: 10,
  color: 'var(--secondary)',
  marginBottom: 4,
  fontWeight: 500,
}

export default function SubDeviceEditForm({ device, radarDevices, onSave, onCancel }: Props) {
  const meta = DEVICE_META[device.kind]

  const BRAND_OPTIONS: RadarBrand[] = ['cetc38', 'cetc14', 'leiying']

  const [form, setForm] = useState({
    name: device.name,
    brand: device.brand,
    linkedRadarId: device.linkedRadarId ?? (device.kind !== 'gps' ? (radarDevices[0]?.id ?? '') : undefined),
    mountOffsetAzimuth: device.mountOffsetAzimuth ?? 0,
    coordinateSystem: device.coordinateSystem ?? 'WGS-84',
  })

  const update = <K extends keyof typeof form>(key: K, value: (typeof form)[K]) => {
    setForm(prev => ({ ...prev, [key]: value }))
  }

  const valid = form.name.trim().length > 0

  const handleSave = () => {
    if (!valid) return
    const base: Partial<SubDevice> = { name: form.name, brand: form.brand }
    if (device.kind === 'opto') {
      onSave({
        ...base,
        kind: 'opto',
        linkedRadarId: form.linkedRadarId,
        mountOffsetAzimuth: form.mountOffsetAzimuth,
      })
    } else if (device.kind === 'level') {
      onSave({
        ...base,
        kind: 'level',
        linkedRadarId: form.linkedRadarId,
      })
    } else {
      onSave({
        ...base,
        kind: 'gps',
        coordinateSystem: form.coordinateSystem,
      })
    }
  }

  return (
    <>
      {/* Header */}
      <div className="flex items-center justify-between shrink-0 px-5 py-3"
        style={{ borderBottom: '1px solid var(--hairline)' }}>
        <div className="flex items-center gap-2">
          <span className="material-symbols-outlined" style={{ fontSize: 18, color: 'var(--primaryContainer)' }}>
            {meta.icon}
          </span>
          <span className="text-[13px] font-semibold" style={{ color: 'var(--ink)' }}>{meta.title}</span>
        </div>
        <button
          className="p-0.5 border-none bg-transparent cursor-pointer transition-colors hover:bg-black/5 rounded"
          style={{ color: 'var(--onSurfaceVariant)', lineHeight: 1 }}
          onClick={onCancel}
        >
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
            <path d="M18 6L6 18M6 6l12 12" strokeLinecap="round" />
          </svg>
        </button>
      </div>

      {/* Body */}
      <div className="flex-1 overflow-y-auto px-5 py-4">
        <div style={labelStyle}>品牌</div>
        <select
          style={{ ...inputStyle, appearance: 'auto', cursor: 'pointer' }}
          value={form.brand}
          onChange={(e) => update('brand', e.target.value as RadarBrand)}
        >
          {BRAND_OPTIONS.map(b => <option key={b} value={b}>{BRAND_LABELS[b]}</option>)}
        </select>

        <div className="mt-3"><div style={labelStyle}>设备名称</div></div>
        <input
          style={inputStyle}
          value={form.name}
          onChange={(e) => update('name', e.target.value)}
          placeholder="输入设备名称"
        />

        {(device.kind === 'opto' || device.kind === 'level') && (
          <div className="mt-3">
            <div style={labelStyle}>关联雷达</div>
            <select
              style={{ ...inputStyle, appearance: 'auto', cursor: 'pointer' }}
              value={form.linkedRadarId ?? ''}
              onChange={(e) => update('linkedRadarId', e.target.value || undefined)}
            >
              {radarDevices.length === 0 && <option value="">无可用雷达</option>}
              {radarDevices.map((r) => (
                <option key={r.id} value={r.id}>{r.name}</option>
              ))}
            </select>
          </div>
        )}

        {device.kind === 'opto' && (
          <div className="mt-3">
            <div style={labelStyle}>安装偏角 (°)</div>
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={form.mountOffsetAzimuth}
              onChange={(e) => update('mountOffsetAzimuth', Number(e.target.value))}
            />
          </div>
        )}

        {device.kind === 'gps' && (
          <div className="mt-3">
            <div style={labelStyle}>坐标系</div>
            <input
              style={inputStyle}
              value={form.coordinateSystem ?? ''}
              onChange={(e) => update('coordinateSystem', e.target.value)}
              placeholder="WGS-84"
            />
          </div>
        )}
      </div>

      {/* Footer */}
      <div className="flex items-center justify-end gap-2 shrink-0 px-5 py-3"
        style={{ borderTop: '1px solid var(--hairline)' }}>
        <button
          className="px-3 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-colors"
          style={{ background: 'transparent', color: 'var(--onSurfaceVariant)' }}
          onClick={onCancel}
        >
          取消
        </button>
        <button
          className="px-3 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-all"
          style={{
            background: valid ? 'var(--primaryContainer)' : 'rgba(6,78,59,0.15)',
            color: '#fff',
            opacity: valid ? 1 : 0.5,
          }}
          disabled={!valid}
          onClick={handleSave}
        >
          保存修改
        </button>
      </div>
    </>
  )
}
