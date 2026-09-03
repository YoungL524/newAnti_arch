/**
 * @file TaskCreateDialog.tsx
 * @brief 收集任务名称、操作员、描述和设备绑定并创建任务。
 * @details 对话框维护未提交草稿并执行必填校验；核心创建成功前不会把草稿加入任务列表。
 */

import { useEffect, useState } from 'react'
import { createPortal } from 'react-dom'
import type { Task } from '../../lib/taskTypes'
import type { RadarDevice } from '../map/types'

export interface TaskCreateResult {
  ok: boolean
  error?: string
}

interface Props {
  radarDevices: RadarDevice[]
  onCreate: (data: Omit<Task, 'id' | 'createdAt' | 'status'>) => Promise<TaskCreateResult>
  onCancel: () => void
}

const inputStyle: React.CSSProperties = {
  width: '100%',
  padding: '6px 10px',
  border: '1px solid var(--hairline)',
  borderRadius: 6,
  background: '#fff',
  color: 'var(--ink)',
  fontSize: 12,
  fontFamily: "'Inter', 'Microsoft YaHei', sans-serif",
  outline: 'none',
}

const labelStyle: React.CSSProperties = {
  fontSize: 10,
  color: 'var(--secondary)',
  marginBottom: 4,
  fontWeight: 500,
}

export default function TaskCreateDialog({ radarDevices, onCreate, onCancel }: Props) {
  const controllableDeviceIds = radarDevices
    .filter(device => device.backendManaged && device.endpointId)
    .map(device => device.id)
  const [name, setName] = useState('')
  const [operator, setOperator] = useState('')
  const [desc, setDesc] = useState('')
  const [deviceIds, setDeviceIds] = useState<string[]>(controllableDeviceIds)
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  useEffect(() => {
    setDeviceIds(prev => {
      if (prev.length > 0) return prev.filter(id => controllableDeviceIds.includes(id))
      return controllableDeviceIds
    })
  }, [controllableDeviceIds.join('|')])

  const valid = name.trim().length > 0 && deviceIds.length > 0 && !submitting

  const toggleDevice = (id: string) => {
    setError('')
    setDeviceIds(prev => prev.includes(id) ? prev.filter(item => item !== id) : [...prev, id])
  }

  const handleCreate = async () => {
    if (!valid) return
    setSubmitting(true)
    setError('')
    try {
      const result = await onCreate({
        name: name.trim(),
        description: desc.trim(),
        operator: operator.trim(),
        deviceIds,
      })
      if (!result.ok) setError(result.error || '任务创建失败')
    } catch (e: any) {
      setError(e?.message || '任务创建失败')
    } finally {
      setSubmitting(false)
    }
  }

  return createPortal(
    <div
      className="fixed inset-0 z-50 flex items-center justify-center"
      style={{ background: 'rgba(0,0,0,0.12)' }}
      onClick={onCancel}
    >
      <div
        className="glass-panel flex flex-col"
        style={{ width: 380, overflow: 'hidden' }}
        onClick={(e) => e.stopPropagation()}
        onKeyDown={(e) => { if (e.key === 'Escape') onCancel() }}
      >
        {/* Header */}
        <div className="flex items-center justify-between px-5 py-3"
          style={{ borderBottom: '1px solid var(--hairline)' }}>
          <div className="flex items-center gap-2">
            <span className="material-symbols-outlined" style={{ fontSize: 18, color: 'var(--primaryContainer)' }}>
              add_mission
            </span>
            <span className="text-[13px] font-semibold" style={{ color: 'var(--ink)' }}>新建任务</span>
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
        <div className="px-5 py-4 space-y-3">
          <div>
            <div style={labelStyle}>任务名称</div>
            <input
              style={inputStyle}
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="例如：城东区域警戒"
              autoFocus
            />
          </div>

          <div>
            <div style={labelStyle}>操作员</div>
            <input
              style={inputStyle}
              value={operator}
              onChange={(e) => setOperator(e.target.value)}
              placeholder="操作员姓名"
            />
          </div>

          <div>
            <div style={labelStyle}>任务描述</div>
            <input
              style={inputStyle}
              value={desc}
              onChange={(e) => setDesc(e.target.value)}
              placeholder="描述本次任务目的..."
            />
          </div>

          <div>
            <div style={labelStyle}>绑定雷达</div>
            <div className="space-y-1.5">
              {radarDevices.length === 0 ? (
                <div className="text-[11px]" style={{ color: 'var(--secondary)' }}>
                  暂无后端设备，请先在设备台账中添加雷达
                </div>
              ) : radarDevices.map(device => {
                const controllable = Boolean(device.backendManaged && device.endpointId)
                const checked = deviceIds.includes(device.id)
                return (
                  <label
                    key={device.id}
                    className="flex items-center justify-between gap-3 rounded px-2 py-1.5"
                    style={{
                      border: '1px solid var(--hairline)',
                      opacity: controllable ? 1 : 0.55,
                      cursor: controllable ? 'pointer' : 'not-allowed',
                    }}
                  >
                    <span className="flex items-center gap-2 min-w-0">
                      <input
                        type="checkbox"
                        checked={checked}
                        disabled={!controllable || submitting}
                        onChange={() => toggleDevice(device.id)}
                      />
                      <span className="truncate text-[11px]" style={{ color: 'var(--ink)' }}>
                        {device.name}
                      </span>
                    </span>
                    <span className="text-[10px] shrink-0" style={{ color: 'var(--secondary)' }}>
                      {device.endpointId || '未绑定端点'}
                    </span>
                  </label>
                )
              })}
            </div>
          </div>

          {error && (
            <div className="rounded px-3 py-2 text-[11px]"
              style={{ background: 'rgba(185, 28, 28, 0.08)', color: '#991b1b' }}>
              {error}
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="flex items-center justify-end gap-2 px-5 py-3"
          style={{ borderTop: '1px solid var(--hairline)' }}>
          <button
            className="px-4 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-colors"
            style={{ background: 'transparent', color: 'var(--onSurfaceVariant)' }}
            onClick={onCancel}
            disabled={submitting}
          >
            取消
          </button>
          <button
            className="px-4 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-all"
            style={{
              background: valid ? 'var(--primaryContainer)' : 'rgba(6,78,59,0.15)',
              color: '#fff',
              opacity: valid ? 1 : 0.5,
            }}
            disabled={!valid}
            onClick={handleCreate}
          >
            {submitting ? '创建中...' : '确认创建'}
          </button>
        </div>
      </div>
    </div>,
    document.body,
  )
}

