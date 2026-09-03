/**
 * @file RadarEditForm.tsx
 * @brief 编辑雷达端点、厂家型号、经纬高和阵面参数。
 * @details 阵面绝对朝向是唯一航向来源；位置/姿态区不再维护重复 Yaw，俯仰和横滚仍描述雷达 6DOF 安装姿态。
 */

import { useEffect, useState } from 'react'
import type { RadarDevice, RadarBrand, RadarFace } from '../map/types'
import { BRAND_LABELS, BRAND_OPTIONS } from '../map/types'
import {
  type CommEndpoint,
  defaultEndpoint,
  fetchEndpoints,
} from '../../lib/endpointsApi'

interface Props {
  device: RadarDevice
  onSave: (updates: Partial<RadarDevice>, endpoint: CommEndpoint) => void
  onCancel: () => void
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

const monoInputStyle: React.CSSProperties = {
  ...inputStyle,
  fontFamily: "'JetBrains Mono', monospace",
  fontSize: 10,
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

function resolveEndpointForDevice(
  endpoints: CommEndpoint[],
  device: RadarDevice,
): CommEndpoint {
  const preferredId = device.endpointId || 'radar_cpu'
  const bound = endpoints.find(e => e.id === preferredId)
  if (bound) return { ...bound }
  if (endpoints.length === 1) return { ...endpoints[0] }
  const ep = defaultEndpoint(preferredId)
  if (!ep.description && device.name) ep.description = device.name
  return ep
}

function TargetFields({ label, target, onChange }: {
  label: string
  target: { address: string; port: number }
  onChange: (t: { address: string; port: number }) => void
}) {
  return (
    <div className="grid grid-cols-[56px_1fr_72px] gap-1.5 items-end">
      <span className="text-[9px] font-medium" style={{ color: 'var(--secondary)' }}>{label}</span>
      <div>
        <div style={labelStyle}>地址</div>
        <input style={monoInputStyle} value={target.address}
          onChange={e => onChange({ ...target, address: e.target.value })} />
      </div>
      <div>
        <div style={labelStyle}>端口</div>
        <input style={monoInputStyle} type="number" value={target.port}
          onChange={e => onChange({ ...target, port: Number(e.target.value) || 0 })} />
      </div>
    </div>
  )
}

export default function RadarEditForm({ device, onSave, onCancel }: Props) {
  const [form, setForm] = useState({
    name: device.name,
    brand: device.brand,
    lng: device.lng,
    lat: device.lat,
    altitude: device.altitude,
    pitch: device.pitch,
    roll: device.roll,
    maxRange: device.maxRange,
    faces: device.faces.map(f => ({ ...f })),
  })
  const [endpoint, setEndpoint] = useState<CommEndpoint | null>(null)
  const [endpointLoading, setEndpointLoading] = useState(true)
  const [endpointError, setEndpointError] = useState('')

  useEffect(() => {
    let cancelled = false
    setEndpointLoading(true)
    setEndpointError('')
    fetchEndpoints()
      .then(r => {
        if (cancelled) return
        if (!r.ok) {
          setEndpointError(r.error || '加载端点配置失败')
          setEndpoint(resolveEndpointForDevice([], device))
          return
        }
        setEndpoint(resolveEndpointForDevice(r.endpoints, device))
      })
      .catch(() => {
        if (!cancelled) {
          setEndpointError('加载端点配置失败')
          setEndpoint(resolveEndpointForDevice([], device))
        }
      })
      .finally(() => { if (!cancelled) setEndpointLoading(false) })
    return () => { cancelled = true }
  }, [device.id, device.endpointId, device.name])

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

  const setTarget = (key: string, val: { address: string; port: number }) => {
    setEndpoint(prev => prev ? { ...prev, targets: { ...prev.targets, [key]: val } } : prev)
  }

  const valid = form.name.trim().length > 0 && !!endpoint && endpoint.localPort > 0

  const handleSave = () => {
    if (!valid || !endpoint) return
    onSave(form, endpoint)
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

        {/* ═══ Device Identity (read-only) ═══ */}
        <div className="flex items-center gap-1.5 mb-3">
          <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--primaryContainer)' }} />
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--primaryContainer)' }}>
            设备身份
          </span>
        </div>

        <div className="mb-3">
          <div style={labelStyle}>后端设备 ID</div>
          <div className="text-[11px] px-2 py-1.5 rounded" style={{ background: 'var(--surfaceLow)', color: 'var(--secondary)', fontFamily: "'JetBrains Mono', monospace" }}>
            {device.id}
          </div>
        </div>
        {device.backendManaged && (
          <div className="text-[9px] mb-3" style={{ color: 'var(--secondary)' }}>数据来源: 后端台账</div>
        )}

        {/* ═══ Communication Endpoint ═══ */}
        <div className="flex items-center gap-1.5 mb-3 mt-4">
          <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--primaryContainer)' }} />
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--primaryContainer)' }}>
            通信端点
          </span>
        </div>

        {endpointLoading ? (
          <p className="text-[10px] mb-3" style={{ color: 'var(--secondary)' }}>加载端点配置...</p>
        ) : endpoint ? (
          <div className="rounded-lg p-3 mb-3 space-y-2"
            style={{ border: '1px solid var(--hairline)', background: 'rgba(255,255,255,0.6)' }}>
            {endpointError && (
              <p className="text-[9px]" style={{ color: '#991b1b' }}>{endpointError}（已使用默认模板）</p>
            )}
            <p className="text-[9px] leading-relaxed" style={{ color: 'var(--secondary)' }}>
              单设备模式：保存后写入 endpoints.json 并热重载，同时绑定到本设备。
            </p>

            <div className="grid grid-cols-2 gap-2">
              <div>
                <div style={labelStyle}>端点 ID</div>
                <input style={monoInputStyle} value={endpoint.id} readOnly />
              </div>
              <div>
                <div style={labelStyle}>协议</div>
                <select style={{ ...monoInputStyle, appearance: 'auto' }} value={endpoint.protocol}
                  onChange={e => setEndpoint({ ...endpoint, protocol: e.target.value as 'udp' | 'tcp' })}>
                  <option value="udp">UDP</option>
                  <option value="tcp">TCP（暂未实现）</option>
                </select>
              </div>
            </div>

            <div>
              <div style={labelStyle}>说明</div>
              <input style={inputStyle} value={endpoint.description}
                onChange={e => setEndpoint({ ...endpoint, description: e.target.value })} />
            </div>

            <div className="grid grid-cols-[1fr_72px] gap-2">
              <div>
                <div style={labelStyle}>本机监听地址</div>
                <input style={monoInputStyle} value={endpoint.localAddress}
                  onChange={e => setEndpoint({ ...endpoint, localAddress: e.target.value })} />
              </div>
              <div>
                <div style={labelStyle}>本机端口</div>
                <input style={monoInputStyle} type="number" value={endpoint.localPort}
                  onChange={e => setEndpoint({ ...endpoint, localPort: Number(e.target.value) || 0 })} />
              </div>
            </div>

            <div className="space-y-1.5 pt-1" style={{ borderTop: '1px dashed var(--hairline)' }}>
              <div className="text-[9px] font-semibold" style={{ color: 'var(--primaryContainer)' }}>对端目标</div>
              <TargetFields label="DPH" target={endpoint.targets.dph ?? { address: '127.0.0.1', port: 0 }}
                onChange={t => setTarget('dph', t)} />
              <TargetFields label="workmode" target={endpoint.targets.workmode ?? { address: '127.0.0.1', port: 0 }}
                onChange={t => setTarget('workmode', t)} />
            </div>
          </div>
        ) : null}

        {/* ═══ Brand ═══ */}
        <div className="flex items-center gap-1.5 mb-3">
          <span className="w-1.5 h-1.5 rounded-full" style={{ background: 'var(--primaryContainer)' }} />
          <span className="text-[10px] font-semibold tracking-wide" style={{ color: 'var(--primaryContainer)' }}>
            厂家 / 型号
          </span>
        </div>

        <div className="mb-3">
          <div style={labelStyle}>厂家/型号</div>
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

        {/* Pitch / Roll; absolute heading is configured per face below. */}
        <div className="grid grid-cols-2 gap-3 mb-3">
          <div>
            <div style={labelStyle}>俯仰 Pitch (°)</div>
            <input style={inputStyle} type="number" step="any" min="-90" max="90"
              value={form.pitch} onChange={(e) => update('pitch', Number(e.target.value))} />
          </div>
          <div>
            <div style={labelStyle}>横滚 Roll (°)</div>
            <input style={inputStyle} type="number" step="any" min="-180" max="180"
              value={form.roll} onChange={(e) => update('roll', Number(e.target.value))} />
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
          <span style={labelStyle}>绝对朝向 (°)</span>
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
          disabled={!valid || endpointLoading}
          onClick={handleSave}
        >
          保存修改
        </button>
      </div>
    </>
  )
}

