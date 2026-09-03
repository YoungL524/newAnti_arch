/**
 * @file DeviceTypeSelector.tsx
 * @brief 让用户按设备数据类型切换底部表格的数据集合。
 * @details 组件输出协议层 DeviceType 值，不自行过滤航迹或保存设备记录。
 */

import { DeviceType } from '../../lib/protocol'

export interface SelectorOption {
  value: string
  label: string
  deviceType: DeviceType
}

interface Props {
  options: SelectorOption[]
  value: string
  onChange: (value: string, deviceType: DeviceType) => void
}

export default function DeviceTypeSelector({ options, value, onChange }: Props) {
  return (
    <select
      className="text-[11px] px-2.5 py-1 rounded border cursor-pointer outline-none"
      style={{
        background: '#ffffff',
        color: 'var(--primaryContainer)',
        borderColor: 'rgba(6,78,59,0.08)',
        fontFamily: "'Inter', sans-serif",
        fontWeight: 600,
      }}
      value={value}
      onChange={(e) => {
        const opt = options.find(o => o.value === e.target.value)
        if (opt) onChange(opt.value, opt.deviceType)
      }}
    >
      {options.map((opt) => (
        <option key={opt.value} value={opt.value}>{opt.label}</option>
      ))}
    </select>
  )
}

