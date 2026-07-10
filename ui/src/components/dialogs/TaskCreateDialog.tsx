import { useState } from 'react'
import { createPortal } from 'react-dom'
import type { Task } from '../../lib/taskTypes'

interface Props {
  onCreate: (data: Omit<Task, 'id' | 'createdAt' | 'status'>) => void | Promise<void>
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

export default function TaskCreateDialog({ onCreate, onCancel }: Props) {
  const [name, setName] = useState('')
  const [operator, setOperator] = useState('')
  const [desc, setDesc] = useState('')

  const valid = name.trim().length > 0

  const handleCreate = () => {
    if (!valid) return
    onCreate({ name: name.trim(), description: desc.trim(), operator: operator.trim(), deviceIds: [] })
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

        </div>

        {/* Footer */}
        <div className="flex items-center justify-end gap-2 px-5 py-3"
          style={{ borderTop: '1px solid var(--hairline)' }}>
          <button
            className="px-4 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-colors"
            style={{ background: 'transparent', color: 'var(--onSurfaceVariant)' }}
            onClick={onCancel}
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
            确认创建
          </button>
        </div>
      </div>
    </div>,
    document.body,
  )
}
