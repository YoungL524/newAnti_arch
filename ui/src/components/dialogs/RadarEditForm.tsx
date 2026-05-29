import { useState } from 'react'
import type { RadarDevice, RadarBrand, RadarFace } from '../map/types'
import { BRAND_LABELS } from '../map/types'

interface Props {
  device: RadarDevice
  onSave: (updates: Partial<RadarDevice>) => void
  onCancel: () => void
}

const BRAND_OPTIONS: RadarBrand[] = ['cetc38', 'cetc14', 'leiying']

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

function nextFaceLabel(faces: RadarFace[]) {
  const used = new Set(faces.map(f => f.label))
  for (let i = 0; i < 26; i++) {
    const label = String.fromCharCode(65 + i)
    if (!used.has(label)) return label
  }
  return `F${faces.length + 1}`
}

export default function RadarEditForm({ device, onSave, onCancel }: Props) {
  const [form, setForm] = useState({
    name: device.name,
    brand: device.brand,
    lng: device.lng,
    lat: device.lat,
    altitude: device.altitude,
    maxRange: device.maxRange,
    faces: device.faces.map(f => ({ ...f })),
  })

  const update = <K extends keyof typeof form>(key: K, value: (typeof form)[K]) => {
    setForm(prev => ({ ...prev, [key]: value }))
  }

  const updateFace = (idx: number, field: keyof RadarFace, value: any) => {
    setForm(prev => ({
      ...prev,
      faces: prev.faces.map((f, i) => i === idx ? { ...f, [field]: value } : f),
    }))
  }

  const addFace = () => {
    setForm(prev => ({
      ...prev,
      faces: [...prev.faces, { label: nextFaceLabel(prev.faces), boresightAzimuth: 0, scanAngle: 45, active: true }],
    }))
  }

  const removeFace = (idx: number) => {
    if (form.faces.length <= 1) return
    setForm(prev => ({ ...prev, faces: prev.faces.filter((_, i) => i !== idx) }))
  }

  const valid = form.name.trim().length > 0

  const handleSave = () => {
    if (!valid) return
    onSave(form)
  }

  return (
    <>
      {/* Header */}
      <div className="flex items-center justify-between shrink-0 px-5 py-3"
        style={{ borderBottom: '1px solid var(--hairline)' }}>
        <div className="flex items-center gap-2">
          <span className="material-symbols-outlined" style={{ fontSize: 18, color: 'var(--primaryContainer)' }}>
            sensors
          </span>
          <span className="text-[13px] font-semibold" style={{ color: 'var(--ink)' }}>编辑雷达设备</span>
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

        {/* ═══ Brand ═══ */}
        <div className="flex items-center gap-1.5 mb-3">
          <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--primaryContainer)' }} />
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--primaryContainer)' }}>
            品牌 / 标识
          </span>
        </div>

        <div className="mb-3">
          <div style={labelStyle}>品牌</div>
          <select
            style={{ ...inputStyle, appearance: 'auto', cursor: 'pointer' }}
            value={form.brand}
            onChange={(e) => update('brand', e.target.value as RadarBrand)}
          >
            {BRAND_OPTIONS.map(b => <option key={b} value={b}>{BRAND_LABELS[b]}</option>)}
          </select>
        </div>

        {/* ═══ 6DOF Section ═══ */}
        <div className="flex items-center gap-1.5 mb-3 mt-4">
          <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--primaryContainer)' }} />
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--primaryContainer)' }}>
            位置 / 姿态 (6DOF)
          </span>
        </div>

        {/* Name */}
        <div className="mb-3">
          <div style={labelStyle}>设备名称</div>
          <input
            style={inputStyle}
            value={form.name}
            onChange={(e) => update('name', e.target.value)}
            placeholder="输入设备名称"
          />
        </div>

        {/* Lng / Lat */}
        <div className="grid grid-cols-2 gap-3 mb-3">
          <div>
            <div style={labelStyle}>经度 (°)</div>
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={form.lng}
              onChange={(e) => update('lng', Number(e.target.value))}
            />
          </div>
          <div>
            <div style={labelStyle}>纬度 (°)</div>
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={form.lat}
              onChange={(e) => update('lat', Number(e.target.value))}
            />
          </div>
        </div>

        {/* Altitude / MaxRange */}
        <div className="grid grid-cols-2 gap-3 mb-3">
          <div>
            <div style={labelStyle}>海拔 (m)</div>
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={form.altitude}
              onChange={(e) => update('altitude', Number(e.target.value))}
            />
          </div>
          <div>
            <div style={labelStyle}>距离 (km)</div>
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={form.maxRange}
              onChange={(e) => update('maxRange', Number(e.target.value))}
            />
          </div>
        </div>

        {/* ═══ Face Array ═══ */}
        <div className="flex items-center gap-1.5 mb-3 mt-4">
          <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--primaryContainer)' }} />
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--primaryContainer)' }}>
            阵面配置 &middot; {form.faces.length}面
          </span>
        </div>

        {/* Face table header */}
        <div className="grid grid-cols-[auto_1fr_1fr_auto] gap-2 mb-1 px-1">
          <span style={labelStyle}>阵面</span>
          <span style={labelStyle}>朝向 (°)</span>
          <span style={labelStyle}>扫描范围 (°)</span>
          <span />
        </div>

        {form.faces.map((face, fi) => (
          <div key={fi} className="grid grid-cols-[auto_1fr_1fr_auto] gap-2 mb-1.5 items-center">
            <span className="text-[11px] font-medium" style={{ color: 'var(--ink)', minWidth: 20 }}>{face.label}</span>
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={face.boresightAzimuth}
              onChange={(e) => updateFace(fi, 'boresightAzimuth', Number(e.target.value))}
            />
            <input
              style={inputStyle}
              type="number"
              step="any"
              value={face.scanAngle}
              onChange={(e) => updateFace(fi, 'scanAngle', Number(e.target.value))}
            />
            <button
              className="p-0.5 rounded border-none bg-transparent cursor-pointer hover:bg-red-50"
              style={{ color: 'var(--onSurfaceVariant)', lineHeight: 1, fontSize: 14 }}
              onClick={() => removeFace(fi)}
              title="删除阵面"
            >×</button>
          </div>
        ))}

        <div className="flex gap-2 mt-1">
          <button
            className="px-2 py-1 rounded text-[9px] font-semibold border cursor-pointer transition-colors"
            style={{ borderColor: 'var(--hairline)', background: 'transparent', color: 'var(--secondary)' }}
            onClick={addFace}
          >+ 添加阵面</button>
        </div>

        {/* ╌╌╌ Dashed divider ╌╌╌ */}
        <div className="my-4" style={{ borderTop: '1px dashed var(--hairline)' }} />

        {/* ═══ RF Section ═══ */}
        <div className="flex items-center gap-1.5 mb-3">
          <span className="text-[10px]" style={{ color: 'var(--onSurfaceVariant)' }}>◇</span>
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--onSurfaceVariant)', opacity: 0.7 }}>
            射频 / 高级参数
          </span>
        </div>

        <div
          className="rounded flex flex-col items-center justify-center text-center"
          style={{
            border: '1px dashed var(--hairline)',
            background: 'var(--surface)',
            padding: '16px 12px',
            opacity: 0.65,
          }}
        >
          <span className="text-[10px] mb-1" style={{ color: 'var(--onSurfaceVariant)' }}>
            频率切换 &middot; 天线模式 &middot; 波形控制 &middot; 射频衰减
          </span>
          <span className="text-[10px]" style={{ color: 'var(--secondary)' }}>
            高级射频参数配置将在后续版本开放
          </span>
        </div>
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
