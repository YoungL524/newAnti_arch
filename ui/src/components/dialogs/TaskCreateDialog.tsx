/**
 * @file TaskCreateDialog.tsx
 * @brief 收集任务名称、操作员、描述和设备绑定并创建任务。
 * @details 对话框维护未提交草稿并执行必填校验；核心创建成功前不会把草稿加入任务列表。
 */

import { useEffect, useState } from 'react'
import { createPortal } from 'react-dom'
import type { Task } from '../../lib/taskTypes'
import type { TaskRadarArrayType } from '../../lib/taskTypes'
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

/**
 * @brief 根据设备物理阵面生成新任务的默认启用位图。
 * @param device 设备台账中的完整雷达配置。
 * @return 单面阵为0x01，四面阵默认启用全部可用阵面。
 */
function defaultFaceMask(device: RadarDevice): number {
  const mask = device.faces.reduce((value, face, index) =>
    face.active ? value | (1 << index) : value, 0)
  return mask || 0x01
}

/**
 * @brief 按“任务形态→兼容设备→任务阵面”的顺序收集任务草稿。
 * @param radarDevices 设备台账中可供匹配的雷达完整配置。
 * @param onCreate 将已校验任务意图提交给任务Owner的回调。
 * @param onCancel 放弃当前未提交草稿的回调。
 * @return 不持有后端状态的任务创建对话框。
 */
export default function TaskCreateDialog({ radarDevices, onCreate, onCancel }: Props) {
  const [radarArrayType, setRadarArrayType] = useState<TaskRadarArrayType>('singleFace')
  const compatibleDevices = radarDevices.filter(device =>
    (radarArrayType === 'fourFace' ? device.faces.length === 4 : device.faces.length === 1))
  const controllableDeviceIds = compatibleDevices
    .filter(device => device.backendManaged && device.endpointId)
    .map(device => device.id)
  const [name, setName] = useState('')
  const [operator, setOperator] = useState('')
  const [desc, setDesc] = useState('')
  const [deviceIds, setDeviceIds] = useState<string[]>([])
  const [deviceFaceMasks, setDeviceFaceMasks] = useState<Record<string, number>>(() =>
    Object.fromEntries(radarDevices.map(device => [device.id, defaultFaceMask(device)])))
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  /**
   * @brief 切换任务雷达形态并丢弃上一形态尚未提交的任务草稿。
   * @param nextType 用户刚选择的单面阵或四面阵任务形态。
   * @details 两种形态代表两项独立任务意图，名称、操作员、描述和错误不能跨形态沿用；设备绑定由后续 effect
   * 根据新形态重新匹配后端台账。
   */
  const changeRadarArrayType = (nextType: TaskRadarArrayType) => {
    if (nextType === radarArrayType) return
    setRadarArrayType(nextType)
    setName('')
    setOperator('')
    setDesc('')
    setError('')
  }

  useEffect(() => {
    setDeviceIds(prev => {
      const retained = prev.filter(id => controllableDeviceIds.includes(id))
      return retained.length > 0 ? retained : controllableDeviceIds
    })
  }, [radarArrayType, controllableDeviceIds.join('|')])

  useEffect(() => {
    setDeviceFaceMasks(previous => Object.fromEntries(radarDevices.map(device => [
      device.id,
      previous[device.id] ?? defaultFaceMask(device),
    ])))
  }, [radarDevices])

  const valid = name.trim().length > 0 && deviceIds.length > 0 && !submitting

  const toggleDevice = (id: string) => {
    setError('')
    setDeviceIds(prev => prev.includes(id) ? prev.filter(item => item !== id) : [...prev, id])
  }

  /**
   * @brief 切换四面阵设备在本次任务中的一个阵面，禁止形成空阵面集合。
   * @param deviceId 设备台账稳定UUID。
   * @param faceIndex 阵面索引，范围0～3。
   */
  const toggleFace = (deviceId: string, faceIndex: number) => {
    setDeviceFaceMasks(previous => {
      const nextMask = (previous[deviceId] ?? 0x0f) ^ (1 << faceIndex)
      if ((nextMask & 0x0f) === 0) return previous
      return { ...previous, [deviceId]: nextMask & 0x0f }
    })
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
        radarArrayType,
        deviceIds,
        deviceFaceMasks: Object.fromEntries(deviceIds.map(id => [id, deviceFaceMasks[id] ?? 0x01])),
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
            <div style={labelStyle}>任务雷达形态</div>
            <div className="grid grid-cols-2 gap-2">
              {([
                ['singleFace', '单面阵任务'],
                ['fourFace', '四面阵任务'],
              ] as const).map(([value, label]) => (
                <button key={value} type="button"
                  className="py-1.5 rounded text-[11px] border cursor-pointer"
                  style={{
                    borderColor: radarArrayType === value ? 'var(--primaryContainer)' : 'var(--hairline)',
                    color: radarArrayType === value ? '#fff' : 'var(--secondary)',
                    background: radarArrayType === value ? 'var(--primaryContainer)' : '#fff',
                  }}
                  disabled={submitting}
                  onClick={() => changeRadarArrayType(value)}
                >{label}</button>
              ))}
            </div>
          </div>

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
              {compatibleDevices.length === 0 ? (
                <div className="text-[11px]" style={{ color: 'var(--secondary)' }}>
                  设备台账中没有匹配的{radarArrayType === 'fourFace' ? '四面阵' : '单面阵'}雷达
                </div>
              ) : compatibleDevices.map(device => {
                const controllable = Boolean(device.backendManaged && device.endpointId)
                const checked = deviceIds.includes(device.id)
                return (
                  <div
                    key={device.id}
                    className="flex flex-wrap items-center justify-between gap-2 rounded px-2 py-1.5"
                    style={{
                      border: '1px solid var(--hairline)',
                      opacity: controllable ? 1 : 0.55,
                      cursor: controllable ? 'pointer' : 'not-allowed',
                    }}
                  >
                    <label className="flex items-center gap-2 min-w-0 flex-1">
                      <input
                        type="checkbox"
                        checked={checked}
                        disabled={!controllable || submitting}
                        onChange={() => toggleDevice(device.id)}
                      />
                      <span className="truncate text-[11px]" style={{ color: 'var(--ink)' }}>
                        {device.name}
                      </span>
                    </label>
                    <span className="text-[10px] shrink-0" style={{ color: 'var(--secondary)' }}>
                      {device.endpointId || '未绑定端点'}
                    </span>
                    {checked && device.faces.length === 4 && (
                      <div className="flex basis-full gap-1 ml-6">
                        {device.faces.map((face, faceIndex) => {
                          const selected = face.active
                            && ((deviceFaceMasks[device.id] ?? 0x0f) & (1 << faceIndex)) !== 0
                          return (
                            <button key={faceIndex} type="button"
                              className="px-2 py-0.5 rounded text-[9px] border cursor-pointer"
                              style={{
                                borderColor: selected ? 'var(--primaryContainer)' : 'var(--hairline)',
                                color: selected ? '#fff' : 'var(--secondary)',
                                background: selected ? 'var(--primaryContainer)' : 'transparent',
                              }}
                              disabled={!face.active || submitting}
                              onClick={() => toggleFace(device.id, faceIndex)}
                            >{faceIndex + 1}#</button>
                          )
                        })}
                      </div>
                    )}
                  </div>
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
