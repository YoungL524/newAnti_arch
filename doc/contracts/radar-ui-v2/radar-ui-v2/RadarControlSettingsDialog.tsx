/**
 * @file RadarControlSettingsDialog.tsx
 * @brief 在设备高级区域编辑当前任务的完整 RadarControlSettings 草稿。
 * @details 上游是任务持久化快照，下游只在用户点击“应用”时一次性提交完整参数；输入过程只改本地草稿，
 * 不发送 HTTP，不生成协议默认值。控件、值域与模式依赖以
 * doc/contracts/DCMP界面控制参数映射契约.md §5～§8 为准。
 */

import './RadarSettings.css'

import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type CSSProperties,
} from 'react'
import type {
  RadarCommonSettings,
  RadarControlSettings,
  RadarFaceSettings,
  RadarFrameSettings,
  Task,
} from '../../lib/taskTypes'
import type { RadarDevice } from '../map/types'

/** @brief 一次“应用”提交的界面结果，供对话框决定关闭还是保留草稿。 */
export interface RadarSettingsApplyResult {
  ok: boolean
  error?: string
}

interface Props {
  /** 当前活动任务；其 controlSettings 作为草稿初值。 */
  task: Task
  /** 设备台账；仅用于只读部署信息与阵面安装姿态，可缺项。 */
  radarDevices: RadarDevice[]
  /** 提交完整草稿到后端；成功返回 ok，失败返回可展示错误。 */
  onApply: (settings: RadarControlSettings) => Promise<RadarSettingsApplyResult>
  /** 放弃未提交草稿并关闭对话框。 */
  onClose: () => void
  /** 任务阶段变化或后端更新中时阻止提交已有草稿。 */
  disabled?: boolean
  /** 当前左侧运行参数分类，由设备编辑页持有以便跨设备页切换。 */
  activeTab: 'common' | 'frame' | 'face'
  /** 切换运行参数分类但保留完整草稿。 */
  onTabChange: (tab: 'common' | 'frame' | 'face') => void
  /** 返回指定设备配置页，不关闭整个雷达配置工作区。 */
  onNavigateDevice: (section: 'basic' | 'connection' | 'deployment') => void
}

const inputStyle: CSSProperties = {
  width: '100%',
  padding: '8px 11px',
  border: '1px solid #dce3e0',
  borderRadius: 8,
  background: '#fff',
  color: 'var(--ink)',
  fontSize: 13,
  fontFamily: "'Inter', 'Microsoft YaHei', sans-serif",
  height: 38,
  boxSizing: 'border-box',
  minWidth: 0,
}

const labelStyle: CSSProperties = {
  fontSize: 12,
  color: '#52615b',
  marginBottom: 7,
  fontWeight: 500,
}

/**
 * @brief 字段校验上报接口：子控件把“当前显示文本是否可以提交”告诉对话框。
 * @details 用于阻止提交与界面显示不一致的参数值，而不在输入过程中静默改写或丢弃用户文本。
 * @note 用 Context 承载同一回调，避免为十余个字段逐个透传。
 */
const ValidityContext = createContext<((key: string, valid: boolean) => void) | null>(null)

/**
 * @brief 渲染一行只读部署信息。
 * @param label 中文标签。
 * @param value 已格式化的值文本；缺数据时由调用方传入占位符。
 */
function ReadonlyRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="rs-readonly-row">
      <span style={{ color: 'var(--secondary)' }}>{label}</span>
      <span style={{ color: 'var(--ink)' }}>{value}</span>
    </div>
  )
}

/**
 * @brief 十进制输入保留编辑中的原文；仅合法值写入草稿，未完成的输入阻止应用。
 * @param label 中文标签。
 * @param value 当前数值，可为角度、计数或衰减量。
 * @param onCommit 数值变化回调，范围校验由后端与合同共同保证。
 * @param min 可选最小值，仅用于界面提示。
 * @param max 可选最大值，仅用于界面提示。
 * @param step 输入步进，默认 1。
 * @param disabled true 表示当前模式下该字段不生效，但仍保留草稿值，切换模式不清零。
 */
function NumberField({
  label,
  value,
  onCommit,
  min,
  max,
  step = 1,
  disabled = false,
}: {
  label: string
  value: number
  onCommit: (value: number) => void
  min?: number
  max?: number
  step?: number
  disabled?: boolean
}) {
  const reportValidity = useContext(ValidityContext)
  const [text, setText] = useState(() => String(value))
  const [invalid, setInvalid] = useState(false)
  useEffect(() => {
    setText(String(value))
    setInvalid(false)
    reportValidity?.(label, true)
  }, [value, label, reportValidity])

  /**
   * @brief 保留空白、负号等编辑中间态；范围或类型不合法时不提交旧值并阻止整表应用。
   * @param raw 输入框当前可见文本。
   */
  const commit = (raw: string) => {
    setText(raw)
    const parsed = /^-?(?:\d+(?:\.\d*)?|\.\d+)$/.test(raw.trim()) ? Number(raw) : Number.NaN
    const valid =
      Number.isFinite(parsed) &&
      (step < 1 || Number.isInteger(parsed)) &&
      (min === undefined || parsed >= min) &&
      (max === undefined || parsed <= max)
    setInvalid(!valid)
    reportValidity?.(label, valid)
    if (valid) onCommit(parsed)
  }

  return (
    <label className="rs-field block">
      <div style={labelStyle}>{label}</div>
      <input
        type="text"
        inputMode={step < 1 ? 'decimal' : 'numeric'}
        style={{
          ...inputStyle,
          opacity: disabled ? 0.5 : 1,
          borderColor: invalid ? '#dc2626' : 'var(--hairline)',
        }}
        value={text}
        aria-invalid={invalid || undefined}
        disabled={disabled}
        onChange={(event) => commit(event.target.value)}
      />
      {invalid && (
        <div className="rs-field-note" style={{ color: '#dc2626' }}>
          请输入范围内的有效数值
        </div>
      )}
    </label>
  )
}

/**
 * @brief 十六进制原始控制字输入。
 * @param label 中文标签。
 * @param value 当前控制字数值；显示时按无符号十六进制。
 * @param onCommit 解析成功后的提交回调。
 * @param signed true 表示按补码写入 int32 字段，false 表示按 uint32 原样写入。
 * @param disabled true 表示当前模式下该字段不生效，但仍保留草稿值，切换模式不清零。
 */
function HexField({
  label,
  value,
  onCommit,
  signed = false,
  disabled = false,
}: {
  label: string
  value: number
  onCommit: (value: number) => void
  signed?: boolean
  disabled?: boolean
}) {
  const reportValidity = useContext(ValidityContext)
  const [text, setText] = useState(() => (value >>> 0).toString(16).toUpperCase())
  const [invalid, setInvalid] = useState(false)
  useEffect(() => {
    setText((value >>> 0).toString(16).toUpperCase())
  }, [value])

  /**
   * @brief 保留用户的原始输入文本，仅在可解析时写入草稿；不可解析时标红并阻止提交。
   * @param raw 输入框当前文本，允许为空或含前导 `0x`。
   */
  const commit = (raw: string) => {
    setText(raw)
    const trimmed = raw.trim().replace(/^0x/i, '')
    const parsed = /^[0-9a-fA-F]+$/.test(trimmed) ? Number.parseInt(trimmed, 16) : Number.NaN
    const valid = Number.isFinite(parsed) && parsed <= 0xffffffff
    setInvalid(!valid)
    reportValidity?.(label, valid)
    if (!valid) return
    onCommit(signed ? parsed | 0 : parsed >>> 0)
  }

  return (
    <label className="rs-field block">
      <div style={labelStyle}>{label}</div>
      <div className="rs-hex">
        <span className="text-[11px]" style={{ color: 'var(--secondary)' }}>
          0x
        </span>
        <input
          style={{
            ...inputStyle,
            fontFamily: 'monospace',
            opacity: disabled ? 0.5 : 1,
            borderColor: invalid ? '#dc2626' : 'var(--hairline)',
          }}
          value={text}
          aria-invalid={invalid || undefined}
          disabled={disabled}
          onChange={(event) => commit(event.target.value)}
        />
      </div>
      {invalid && (
        <div className="rs-field-note" style={{ color: '#dc2626' }}>
          请输入十六进制控制字
        </div>
      )}
    </label>
  )
}

/**
 * @brief 布尔开关；直接切换草稿值，不清零同组其它字段。
 * @param label 中文标签。
 * @param checked 当前状态。
 * @param onToggle 切换回调。
 * @param hint 可选补充说明。
 * @param disabled true 表示当前模式下该字段不生效，但仍保留草稿值，切换模式不清零。
 */
function SwitchField({
  label,
  checked,
  onToggle,
  hint,
  disabled = false,
}: {
  label: string
  checked: boolean
  onToggle: () => void
  hint?: string
  disabled?: boolean
}) {
  return (
    <div className="rs-switch-row" data-disabled={disabled || undefined}>
      <div>
        <div style={{ fontSize: 13, color: 'var(--ink)' }}>{label}</div>
        {hint && (
          <div className="text-[11px]" style={{ color: 'var(--secondary)' }}>
            {hint}
          </div>
        )}
      </div>
      <button
        type="button"
        onClick={onToggle}
        disabled={disabled}
        className="w-11 h-6 rounded-full border-none cursor-pointer disabled:cursor-not-allowed disabled:opacity-40 shrink-0 flex items-center px-0.5 transition-colors"
        style={{
          background: checked ? 'var(--primaryContainer)' : '#c8cdd0',
        }}
        aria-label={`${label}：${checked ? '开' : '关'}`}
        role="switch"
        aria-checked={checked}
      >
        <span
          className="w-5 h-5 rounded-full bg-white shadow-sm transition-transform"
          style={{ transform: checked ? 'translateX(20px)' : 'translateX(0)' }}
        />
      </button>
    </div>
  )
}

/**
 * @brief 有界枚举下拉；option.value 是协议原始控制字，显示名为中文方案。
 * @param label 中文标签。
 * @param value 当前控制字。
 * @param options 允许的登记值集合。
 * @param onCommit 选择后的提交回调。
 * @param disabled true 表示当前模式下该字段不生效，但仍保留原值。
 * @param hint 可选补充说明。
 */
function SelectField({
  label,
  value,
  options,
  onCommit,
  disabled = false,
  hint,
}: {
  label: string
  value: number
  options: { value: number; label: string }[]
  onCommit: (value: number) => void
  disabled?: boolean
  hint?: string
}) {
  return (
    <label className="rs-field block">
      <div style={labelStyle}>{label}</div>
      <select
        style={{ ...inputStyle, opacity: disabled ? 0.5 : 1 }}
        value={value}
        disabled={disabled}
        onChange={(event) => onCommit(Number(event.target.value))}
      >
        {options.map((option) => (
          <option key={option.value} value={option.value}>
            {option.label}
          </option>
        ))}
      </select>
      {hint && (
        <div className="rs-field-note" style={{ color: 'var(--secondary)' }}>
          {hint}
        </div>
      )}
    </label>
  )
}

/**
 * @brief 组装参数面板，并在打开时从任务快照深拷贝一份可放弃的本地草稿。
 * @param task 当前活动任务。
 * @param radarDevices 设备台账，用于只读部署信息。
 * @param onApply 提交回调。
 * @param onClose 取消/成功后的关闭回调。
 * @param disabled 任务不再可编辑时阻止应用当前草稿。
 * @param activeTab 当前运行参数分类。
 * @param onTabChange 切换运行参数分类。
 * @param onNavigateDevice 切换回设备台账分类。
 * @return 嵌入设备高级区域的任务参数编辑器。
 */
export default function RadarControlSettingsDialog({
  task,
  radarDevices,
  onApply,
  onClose,
  disabled = false,
  activeTab: tab,
  onTabChange: setTab,
  onNavigateDevice,
}: Props) {
  const [draft, setDraft] = useState<RadarControlSettings>(
    () => JSON.parse(JSON.stringify(task.controlSettings)) as RadarControlSettings,
  )
  const [faceIndex, setFaceIndex] = useState(0)
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')
  const [invalidFields, setInvalidFields] = useState<Record<string, boolean>>({})

  /**
   * @brief 汇总各字段的当前可提交状态；任一场非法时禁止“应用”，避免提交与界面显示不一致的值。
   * @param key 字段中文标签，同一对话框内唯一。
   * @param valid 字段当前显示文本是否可以提交。
   */
  const reportValidity = useCallback((key: string, valid: boolean) => {
    setInvalidFields((prev) => {
      if (valid) {
        if (!prev[key]) return prev
        const next = { ...prev }
        delete next[key]
        return next
      }
      return prev[key] ? prev : { ...prev, [key]: true }
    })
  }, [])
  useEffect(() => {
    setInvalidFields({})
  }, [tab, faceIndex])
  const hasInvalidFields = Object.keys(invalidFields).length > 0

  const faceCount = task.radarArrayType === 'fourFace' ? 4 : 1
  const device = useMemo(
    () => radarDevices.find((item) => task.deviceIds.includes(item.id)) ?? null,
    [radarDevices, task.deviceIds],
  )

  const patchCommon = (patch: Partial<RadarCommonSettings>) =>
    setDraft((prev) => ({ ...prev, common: { ...prev.common, ...patch } }))
  const patchFrame = (patch: Partial<RadarFrameSettings>) =>
    setDraft((prev) => ({ ...prev, frame: { ...prev.frame, ...patch } }))
  const patchFace = (index: number, patch: Partial<RadarFaceSettings>) =>
    setDraft((prev) => ({
      ...prev,
      faces: prev.faces.map((face, current) =>
        current === index ? { ...face, ...patch } : face,
      ) as unknown as RadarControlSettings['faces'],
    }))
  const copyFaceToAll = () =>
    setDraft((prev) => ({
      ...prev,
      faces: prev.faces.map(() => ({ ...prev.faces[faceIndex] })) as unknown as RadarControlSettings['faces'],
    }))

  /**
   * @brief 提交完整草稿；失败保留草稿并展示后端错误，成功由上层关闭对话框。
   */
  const handleApply = async () => {
    if (submitting || hasInvalidFields || disabled) return
    setSubmitting(true)
    setError('')
    try {
      const result = await onApply(draft)
      if (!result.ok) {
        setError(result.error || '参数应用失败')
        return
      }
      onClose()
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : '参数应用失败')
    } finally {
      setSubmitting(false)
    }
  }

  const common = draft.common
  const frame = draft.frame
  const face = draft.faces[faceIndex]
  const isContinuous = common.frameWorkMode === 0x00
  const isSingleFrame = common.frameWorkMode === 0x02
  const isCustomWaveform = frame.waveformCode === 0xaa
  const installFace = device?.faces[faceIndex] ?? null

  /** @brief 左侧导航只切换当前参数分组，完整草稿在切换期间保留。 */
  const tabButton = (key: 'common' | 'frame' | 'face', text: string, icon: string) => (
    <button
      key={key}
      type="button"
      className="w-full flex items-center gap-3 px-4 py-3 rounded-lg text-[13px] text-left border-none cursor-pointer"
      style={{
        background: tab === key ? 'var(--emeraldSubtle)' : 'transparent',
        color: tab === key ? 'var(--primaryContainer)' : 'var(--onSurfaceVariant)',
        fontWeight: tab === key ? 700 : 500,
      }}
      onClick={() => setTab(key)}
    >
      <span className="material-symbols-outlined" style={{ fontSize: 19 }}>
        {icon}
      </span>
      {text}
    </button>
  )

  return (
    <ValidityContext.Provider value={reportValidity}>
      <div className="flex flex-col bg-white" style={{ width: '100%', height: '100%', overflow: 'hidden' }}>
        <div
          className="flex items-center justify-between shrink-0 px-8 py-5"
          style={{ borderBottom: '1px solid var(--hairline)' }}
        >
          <div className="flex items-center gap-5">
            <span className="text-[20px] font-bold" style={{ color: 'var(--ink)' }}>
              雷达配置
            </span>
            <span
              className="pl-5 text-[14px]"
              style={{ color: 'var(--secondary)', borderLeft: '1px solid var(--hairline)' }}
            >
              {device?.name ?? task.name}
            </span>
          </div>
          <button
            type="button"
            className="material-symbols-outlined border-none bg-transparent cursor-pointer"
            style={{ color: 'var(--onSurfaceVariant)' }}
            onClick={onClose}
            title="返回设备编辑"
          >
            close
          </button>
        </div>
        <div className="flex flex-1 min-h-0">
          <aside
            className="w-[226px] shrink-0 flex flex-col px-4 py-7"
            style={{ borderRight: '1px solid var(--hairline)', background: 'var(--surface)' }}
          >
            <div className="px-4 mb-3 text-[12px] font-semibold" style={{ color: 'var(--secondary)' }}>
              设备配置
            </div>
            {(
              [
                ['basic', '基本信息', 'description'],
                ['connection', '通信连接', 'link'],
                ['deployment', '部署与阵面', 'grid_view'],
              ] as const
            ).map(([key, label, icon]) => (
              <button
                key={key}
                type="button"
                onClick={() => onNavigateDevice(key)}
                className="w-full flex items-center gap-3 px-4 py-3 rounded-lg text-[13px] text-left border-none cursor-pointer"
                style={{ color: 'var(--onSurfaceVariant)', background: 'transparent' }}
              >
                <span className="material-symbols-outlined" style={{ fontSize: 19 }}>
                  {icon}
                </span>
                {label}
              </button>
            ))}
            <div className="mx-3 my-5" style={{ borderTop: '1px solid var(--hairline)' }} />
            <div className="px-4 mb-3 text-[12px] font-semibold" style={{ color: 'var(--secondary)' }}>
              运行参数
            </div>
            {tabButton('common', '通用参数', 'settings')}
            {tabButton('frame', '帧参数', 'tune')}
            {tabButton('face', '阵面参数', 'view_quilt')}
            <div
              className="mt-auto px-4 pt-5 text-[11px]"
              style={{ color: 'var(--secondary)', borderTop: '1px solid var(--hairline)' }}
            >
              当前任务
              <br />
              <span className="text-[13px] font-medium" style={{ color: 'var(--ink)' }}>
                {task.name}
              </span>
            </div>
          </aside>
          <div className="rs-content flex-1 overflow-y-auto">
            <div className="rs-page-heading">
              <h2 className="text-[21px] font-bold mb-1" style={{ color: 'var(--ink)' }}>
                {tab === 'common' ? '通用参数' : tab === 'frame' ? '帧参数' : '阵面参数'}
              </h2>
              <p className="text-[12px] mb-7" style={{ color: 'var(--secondary)' }}>
                {tab === 'common'
                  ? '设置工作模式、检测门限与记录选项。'
                  : tab === 'frame'
                    ? '按工作模式配置波束、波形与采样参数。'
                    : '独立配置各阵面的射频与供电参数。'}
              </p>
            </div>
            {tab === 'common' && (
              <div className="rs-sections">
                <section className="rs-section ">
                  <div className="rs-section-heading">
                    <h3>工作模式</h3>
                    <p>运行方式与探测范围</p>
                  </div>
                  <div className="rs-grid">
                    <SelectField
                      label="阵面组合模式"
                      value={common.combinationMode}
                      options={[
                        { value: 0x00, label: '常规' },
                        { value: 0x01, label: '合成' },
                      ]}
                      onCommit={(value) => patchCommon({ combinationMode: value })}
                      hint="当前 DP 版本可能不响应"
                    />
                    <SelectField
                      label="帧工作模式"
                      value={common.frameWorkMode}
                      options={[
                        { value: 0x00, label: '景幅连续' },
                        { value: 0x02, label: '单帧测试' },
                      ]}
                      onCommit={(value) => patchCommon({ frameWorkMode: value })}
                      hint="0x01 由 DP 自主调用"
                    />
                    <SelectField
                      label="系统主模式"
                      value={common.systemWorkMode}
                      options={[
                        { value: 0x00, label: 'Air' },
                        { value: 0x01, label: 'Ground' },
                        { value: 0x02, label: 'Sea' },
                        { value: 0x03, label: 'Weather' },
                        { value: 0x04, label: 'MultiFunc' },
                      ]}
                      onCommit={(value) => patchCommon({ systemWorkMode: value })}
                      hint="当前 DP 版本可能不响应"
                    />
                    <SelectField
                      label="探测量程"
                      value={common.rangeMode}
                      options={[
                        { value: 0x00, label: '2 km' },
                        { value: 0x01, label: '5 km' },
                      ]}
                      onCommit={(value) => patchCommon({ rangeMode: value })}
                    />
                  </div>
                </section>
                <section className="rs-section ">
                  <div className="rs-section-heading">
                    <h3>检测与抑制</h3>
                    <p>STC、CFAR 与速度门限</p>
                  </div>
                  <div className="rs-grid">
                    <SwitchField
                      label="STC 开关"
                      checked={common.stcEnabled}
                      onToggle={() => patchCommon({ stcEnabled: !common.stcEnabled })}
                    />
                    <SelectField
                      label="STC 方案"
                      value={common.stcMode}
                      options={[
                        { value: 0x00, label: '方案一' },
                        { value: 0x01, label: '方案二' },
                        { value: 0x10, label: '方案三' },
                        { value: 0x11, label: '方案四' },
                      ]}
                      onCommit={(value) => patchCommon({ stcMode: value })}
                      disabled={!common.stcEnabled}
                      hint="仅 STC 开启时设备响应"
                    />
                    <SelectField
                      label="CFAR 方案"
                      value={common.cfarMode}
                      options={[
                        { value: 0x00, label: '方案一' },
                        { value: 0x01, label: '方案二' },
                      ]}
                      onCommit={(value) => patchCommon({ cfarMode: value })}
                    />
                    <HexField
                      label="速度门限"
                      value={common.velocityThreshold}
                      signed
                      onCommit={(value) => patchCommon({ velocityThreshold: value })}
                    />
                    <HexField
                      label="CFAR 门限1"
                      value={common.cfarThreshold1}
                      signed
                      onCommit={(value) => patchCommon({ cfarThreshold1: value })}
                    />
                    <HexField
                      label="CFAR 门限2"
                      value={common.cfarThreshold2}
                      signed
                      onCommit={(value) => patchCommon({ cfarThreshold2: value })}
                    />
                  </div>
                </section>
                <section className="rs-section ">
                  <div className="rs-section-heading">
                    <h3>辅助功能</h3>
                    <p>激励、航迹起批与数据记录</p>
                  </div>
                  <div className="rs-grid">
                    <SwitchField
                      label="激励信号关闭"
                      checked={common.excitationDisabled}
                      onToggle={() => patchCommon({ excitationDisabled: !common.excitationDisabled })}
                      hint="开启表示关闭激励"
                    />
                    <NumberField
                      label="起批点数"
                      value={common.trackInitiationPointCount}
                      min={0}
                      onCommit={(value) =>
                        patchCommon({ trackInitiationPointCount: Math.max(0, Math.trunc(value)) })
                      }
                    />
                    <SwitchField
                      label="AD 记录"
                      checked={common.adRecordEnabled}
                      onToggle={() => patchCommon({ adRecordEnabled: !common.adRecordEnabled })}
                    />
                    <SwitchField
                      label="点迹记录"
                      checked={common.pointRecordEnabled}
                      onToggle={() => patchCommon({ pointRecordEnabled: !common.pointRecordEnabled })}
                    />
                    <SwitchField
                      label="凝聚记录"
                      checked={common.clusterRecordEnabled}
                      onToggle={() => patchCommon({ clusterRecordEnabled: !common.clusterRecordEnabled })}
                    />
                    <SwitchField
                      label="关联记录"
                      checked={common.associationRecordEnabled}
                      onToggle={() =>
                        patchCommon({ associationRecordEnabled: !common.associationRecordEnabled })
                      }
                    />
                  </div>
                </section>
              </div>
            )}

            {tab === 'frame' && (
              <div className="rs-sections">
                <div className="rs-mode-note">
                  <span className="rs-status-dot" />
                  <span>
                    当前工作模式：
                    <strong>{isContinuous ? '景幅连续' : isSingleFrame ? '单帧测试' : '其他模式'}</strong> ·
                    不适用的参数保留原值
                  </span>
                </div>
                <section className="rs-section ">
                  <div className="rs-section-heading">
                    <h3>景幅连续</h3>
                    <p>景幅连续模式下可编辑</p>
                  </div>
                  <div className="rs-grid">
                    <HexField
                      label="景幅波形方案"
                      value={frame.screenWaveMode}
                      onCommit={(value) => patchFrame({ screenWaveMode: value })}
                      disabled={!isContinuous}
                    />
                    <SwitchField
                      label="景幅跟踪"
                      checked={frame.screenTrackEnabled}
                      onToggle={() => patchFrame({ screenTrackEnabled: !frame.screenTrackEnabled })}
                      disabled={!isContinuous}
                      hint={isContinuous ? undefined : '仅景幅连续模式可编辑'}
                    />
                  </div>
                </section>
                <section className="rs-section ">
                  <div className="rs-section-heading">
                    <h3>单帧测试</h3>
                    <p>波束指向与中频设置</p>
                  </div>
                  <div className="rs-grid">
                    <NumberField
                      label="波束方位角（°）"
                      value={frame.beamAzimuthDeg}
                      min={-45}
                      max={45}
                      step={0.1}
                      onCommit={(value) => patchFrame({ beamAzimuthDeg: value })}
                      disabled={!isSingleFrame}
                    />
                    <NumberField
                      label="波束俯仰角（°）"
                      value={frame.beamElevationDeg}
                      min={-45}
                      max={45}
                      step={0.1}
                      onCommit={(value) => patchFrame({ beamElevationDeg: value })}
                      disabled={!isSingleFrame}
                    />
                    <SelectField
                      label="中频带宽"
                      value={frame.bandwidthMode}
                      options={[
                        { value: 0x00, label: '带宽1' },
                        { value: 0x01, label: '带宽2' },
                        { value: 0x02, label: '带宽3' },
                      ]}
                      onCommit={(value) => patchFrame({ bandwidthMode: value })}
                      disabled={!isSingleFrame}
                      hint="仅单帧测试可编辑"
                    />
                    <SwitchField
                      label="BIT 自检"
                      checked={frame.bitEnabled}
                      onToggle={() => patchFrame({ bitEnabled: !frame.bitEnabled })}
                      disabled={!isSingleFrame}
                      hint="仅单帧测试；开启后 FPGA 可强制只接收"
                    />
                    <HexField
                      label="波形模式字"
                      value={frame.waveformCode}
                      onCommit={(value) => patchFrame({ waveformCode: value })}
                      disabled={!isSingleFrame}
                    />
                  </div>
                </section>
                <details
                  className="rs-section rs-disclosure"
                  open={isSingleFrame && isCustomWaveform ? true : undefined}
                >
                  <summary>
                    <span>自定义波形</span>
                    <span className="rs-summary-note">单帧测试 · 波形模式字 0xAA</span>
                  </summary>
                  <div className="rs-grid">
                    <SelectField
                      label="调制形式"
                      value={frame.modulationMode}
                      options={[
                        { value: 0x00, label: '线性调频' },
                        { value: 0x01, label: '普通脉冲' },
                      ]}
                      onCommit={(value) => patchFrame({ modulationMode: value })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                      hint="仅自定义波形（0xAA）可编辑"
                    />
                    <HexField
                      label="调制带宽原始字"
                      value={frame.modulationBandwidthWord}
                      onCommit={(value) => patchFrame({ modulationBandwidthWord: value })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <HexField
                      label="脉宽原始字"
                      value={frame.pulseWidthWord}
                      onCommit={(value) => patchFrame({ pulseWidthWord: value })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <HexField
                      label="重复周期原始字"
                      value={frame.repetitionPeriodWord}
                      onCommit={(value) => patchFrame({ repetitionPeriodWord: value })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <NumberField
                      label="帧间等待脉冲数"
                      value={frame.frameWaitPulseCount}
                      min={0}
                      onCommit={(value) =>
                        patchFrame({ frameWaitPulseCount: Math.max(0, Math.trunc(value)) })
                      }
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <NumberField
                      label="有效脉冲数"
                      value={frame.effectivePulseCount}
                      min={0}
                      onCommit={(value) =>
                        patchFrame({ effectivePulseCount: Math.max(0, Math.trunc(value)) })
                      }
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <HexField
                      label="采样起点原始字"
                      value={frame.sampleStartWord}
                      onCommit={(value) => patchFrame({ sampleStartWord: value })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <HexField
                      label="采样终点/长度原始字"
                      value={frame.sampleEndOrLengthWord}
                      onCommit={(value) => patchFrame({ sampleEndOrLengthWord: value })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                    />
                    <SwitchField
                      label="写入自定义波形"
                      checked={frame.waveformWriteEnabled}
                      onToggle={() => patchFrame({ waveformWriteEnabled: !frame.waveformWriteEnabled })}
                      disabled={!isSingleFrame || !isCustomWaveform}
                      hint="仅自定义波形可编辑"
                    />
                    <HexField
                      label="波形写入位置"
                      value={frame.waveformWritePosition}
                      onCommit={(value) => patchFrame({ waveformWritePosition: value })}
                      disabled={!isSingleFrame || !isCustomWaveform || !frame.waveformWriteEnabled}
                    />
                  </div>
                </details>
              </div>
            )}

            {tab === 'face' && (
              <div key={faceIndex} className="rs-sections">
                <div className="rs-face-tabs">
                  {Array.from({ length: faceCount }, (_, index) => (
                    <button
                      key={index}
                      type="button"
                      className="px-3 py-1 rounded text-[11px] border cursor-pointer"
                      style={{
                        borderColor: faceIndex === index ? 'var(--primaryContainer)' : 'var(--hairline)',
                        background: faceIndex === index ? 'var(--primaryContainer)' : '#fff',
                        color: faceIndex === index ? '#fff' : 'var(--secondary)',
                      }}
                      onClick={() => setFaceIndex(index)}
                    >
                      {index + 1}# 阵面
                    </button>
                  ))}
                  {faceCount > 1 && (
                    <button
                      type="button"
                      className="ml-auto px-3 py-1 rounded text-[11px] border cursor-pointer"
                      style={{ borderColor: 'var(--hairline)', color: 'var(--secondary)' }}
                      onClick={copyFaceToAll}
                    >
                      复制参数到全部阵面
                    </button>
                  )}
                </div>

                <section className="rs-section">
                  <div className="rs-section-heading">
                    <h3>射频设置</h3>
                    <p>频段、天线模式与通道衰减</p>
                  </div>
                  <div className="rs-grid">
                    <SelectField
                      label="工作频段"
                      value={face.frequencyBand}
                      options={[
                        { value: 0x00, label: 'X' },
                        { value: 0x01, label: 'Ku' },
                      ]}
                      onCommit={(value) => patchFace(faceIndex, { frequencyBand: value })}
                    />
                    <NumberField
                      label="频点编号"
                      value={face.frequencyIndex}
                      min={0}
                      max={39}
                      onCommit={(value) =>
                        patchFace(faceIndex, { frequencyIndex: Math.max(0, Math.trunc(value)) })
                      }
                    />
                    <SelectField
                      label="天线模式"
                      value={face.antennaMode}
                      options={[
                        { value: 0x00, label: '待机' },
                        { value: 0x01, label: '只接收' },
                        { value: 0x10, label: '只发射' },
                        { value: 0x11, label: '收发均加电' },
                      ]}
                      onCommit={(value) => patchFace(faceIndex, { antennaMode: value })}
                    />
                    <NumberField
                      label="射频衰减（dB）"
                      value={face.rfAttenuationDb}
                      min={0}
                      max={31}
                      onCommit={(value) =>
                        patchFace(faceIndex, { rfAttenuationDb: Math.max(0, Math.trunc(value)) })
                      }
                    />
                    <NumberField
                      label="中频衰减（dB）"
                      value={face.ifAttenuationDb}
                      min={0}
                      max={31}
                      onCommit={(value) =>
                        patchFace(faceIndex, { ifAttenuationDb: Math.max(0, Math.trunc(value)) })
                      }
                    />
                  </div>
                </section>
                <section className="rs-section">
                  <div className="rs-section-heading">
                    <h3>供电控制</h3>
                    <p>分别控制风扇与 TR 组件电源</p>
                  </div>
                  <div className="rs-grid">
                    <SwitchField
                      label="风扇1 供电"
                      checked={face.fan1Powered}
                      onToggle={() => patchFace(faceIndex, { fan1Powered: !face.fan1Powered })}
                    />
                    <SwitchField
                      label="风扇2 供电"
                      checked={face.fan2Powered}
                      onToggle={() => patchFace(faceIndex, { fan2Powered: !face.fan2Powered })}
                    />
                    <SwitchField
                      label="TR 组件供电"
                      checked={face.trModulePowered}
                      onToggle={() => patchFace(faceIndex, { trModulePowered: !face.trModulePowered })}
                    />
                  </div>
                </section>
                <section className="rs-reference">
                  <div className="text-[11px] font-medium mb-1" style={{ color: 'var(--ink)' }}>
                    只读部署信息
                  </div>
                  {device ? (
                    <>
                      <ReadonlyRow label="设备" value={device.name} />
                      <ReadonlyRow label="经度 / 纬度" value={`${device.lng} / ${device.lat}`} />
                      <ReadonlyRow label="海拔（m）" value={String(device.altitude)} />
                      <ReadonlyRow
                        label={`${faceIndex + 1}# 安装航向 / 俯仰 / 横滚（°）`}
                        value={
                          installFace
                            ? `${installFace.heading} / ${installFace.pitch} / ${installFace.roll}`
                            : '—'
                        }
                      />
                    </>
                  ) : (
                    <div className="text-[11px]" style={{ color: 'var(--secondary)' }}>
                      设备台账不可用
                    </div>
                  )}
                </section>

                <div className="rs-reset">
                  <div className="flex items-center justify-between">
                    <div>
                      <div className="text-[11px] font-medium" style={{ color: 'var(--ink)' }}>
                        RF 复位
                      </div>
                      <div className="text-[9px]" style={{ color: 'var(--secondary)' }}>
                        暂不可用
                      </div>
                    </div>
                    <button
                      type="button"
                      disabled
                      className="px-3 py-1 rounded text-[11px] border cursor-not-allowed opacity-40"
                      style={{ borderColor: 'var(--hairline)', color: 'var(--secondary)' }}
                    >
                      复位
                    </button>
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>

        <div
          className="rs-footer flex items-center justify-between gap-2 shrink-0 px-8 py-4"
          style={{ borderTop: '1px solid var(--hairline)' }}
        >
          <div
            className="text-[12px]"
            style={{ color: hasInvalidFields || error ? '#991b1b' : 'var(--secondary)' }}
          >
            {hasInvalidFields ? '存在未完成的参数输入，请修正后再应用' : error || '应用到当前任务'}
          </div>
          <div className="flex items-center gap-2">
            <button
              className="px-5 py-2 rounded text-[13px] font-medium border cursor-pointer"
              style={{ background: '#fff', color: 'var(--onSurfaceVariant)', borderColor: 'var(--hairline)' }}
              onClick={onClose}
              disabled={submitting}
            >
              取消
            </button>
            <button
              className="px-5 py-2 rounded text-[13px] font-medium border-none cursor-pointer disabled:opacity-50"
              style={{ background: 'var(--primaryContainer)', color: '#fff' }}
              onClick={handleApply}
              disabled={submitting || hasInvalidFields || disabled}
            >
              {submitting ? '应用中…' : '应用参数'}
            </button>
          </div>
        </div>
      </div>
    </ValidityContext.Provider>
  )
}
