import { useState } from 'react'

/* ── Data ── */

interface SectionData {
  title: string
  fields: FieldData[]
}

interface FieldData {
  label: string
  type: 'text' | 'slider' | 'select'
  value: string | number
  options?: string[]
  min?: number
  max?: number
  unit?: string
}

const basicSection: SectionData = {
  title: '基础属性',
  fields: [
    { label: '设备名称', type: 'text', value: '雷达站-A01' },
    { label: '设备类型', type: 'text', value: '相控阵雷达' },
    { label: '序列号', type: 'text', value: 'RD-2024-0872' },
    { label: '固件版本', type: 'text', value: 'v3.2.1-beta' },
  ],
}

const paramSection: SectionData = {
  title: '工作参数',
  fields: [
    { label: '发射功率', type: 'slider', value: 75, min: 0, max: 100, unit: '%' },
    { label: '增益', type: 'slider', value: 42, min: 0, max: 60, unit: 'dB' },
    { label: '阈值', type: 'slider', value: 28, min: 0, max: 100, unit: '%' },
    { label: 'CFAR类型', type: 'select', value: 'CA-CFAR', options: ['CA-CFAR', 'GO-CFAR', 'SO-CFAR', 'OS-CFAR'] },
    { label: '检测阈率', type: 'slider', value: 3.5, min: 0, max: 10, unit: '×10⁻⁴' },
  ],
}

const displaySection: SectionData = {
  title: '显示设置',
  fields: [
    { label: '亮度', type: 'slider', value: 65, min: 0, max: 100, unit: '%' },
    { label: '对比度', type: 'slider', value: 50, min: 0, max: 100, unit: '%' },
    { label: '余辉', type: 'select', value: '中', options: ['关', '低', '中', '高'] },
  ],
}

const otherSection: SectionData = {
  title: '其他设置',
  fields: [
    { label: '数据记录', type: 'select', value: '开启', options: ['开启', '关闭'] },
    { label: '网络模式', type: 'select', value: 'TCP', options: ['TCP', 'UDP', '串口'] },
  ],
}

const sections = [basicSection, paramSection, displaySection, otherSection]

/* ── Resources ── */

interface Resource {
  label: string
  value: number
  color: string
}

const resources: Resource[] = [
  { label: 'CPU', value: 32, color: '#3b82f6' },
  { label: 'MEM', value: 58, color: '#10b981' },
  { label: 'DISK', value: 42, color: '#f59e0b' },
  { label: 'NET', value: 17, color: '#8b5cf6' },
]

/* ── Component ── */

export default function PropertiesPanel() {
  const [activeTab, setActiveTab] = useState(0)

  return (
    <div
      className="w-[280px] min-w-[280px] flex flex-col overflow-hidden"
      style={{ background: '#1f2937', borderLeft: '1px solid #374151' }}
    >
      {/* Tabs */}
      <div
        className="flex items-end h-[36px] min-h-[36px] px-3 gap-4"
        style={{ borderBottom: '1px solid #374151' }}
      >
        {['属性', '标注'].map((tab, i) => (
          <button
            key={tab}
            className="relative h-full text-[11px] cursor-pointer border-none bg-transparent pb-[6px] transition-colors"
            style={{
              color: activeTab === i ? '#f3f4f6' : '#9ca3af',
              fontWeight: activeTab === i ? 500 : 400,
            }}
            onClick={() => setActiveTab(i)}
          >
            {tab}
            {activeTab === i && (
              <div
                className="absolute bottom-0 left-0 right-0 h-[2px]"
                style={{ background: '#3b82f6' }}
              />
            )}
          </button>
        ))}
      </div>

      {/* Content */}
      <div className="flex-1 overflow-y-auto">
        {/* Accordion sections */}
        {sections.map((section) => (
          <AccordionSection key={section.title} title={section.title}
            defaultOpen={section === basicSection || section === paramSection}>
            {section.fields.map((field) => (
              <FieldRow key={field.label} field={field} />
            ))}
          </AccordionSection>
        ))}

        {/* System Resources */}
        <div className="px-3 py-3">
          <div className="text-[11px] font-medium mb-2.5" style={{ color: '#f3f4f6' }}>
            系统资源
          </div>
          {resources.map((r) => (
            <ResourceBar key={r.label} resource={r} />
          ))}
        </div>
      </div>
    </div>
  )
}

/* ── Sub-components ── */

function AccordionSection({
  title,
  defaultOpen,
  children,
}: {
  title: string
  defaultOpen?: boolean
  children: React.ReactNode
}) {
  const [open, setOpen] = useState(defaultOpen ?? false)

  return (
    <div style={{ borderBottom: '1px solid rgba(55,65,81,0.5)' }}>
      {/* Header */}
      <button
        className="flex items-center w-full h-[30px] px-3 gap-2 cursor-pointer border-none
                   hover:bg-[rgba(255,255,255,0.03)] transition-colors"
        style={{ background: 'transparent' }}
        onClick={() => setOpen(!open)}
      >
        <span className="text-[9px] transition-transform" style={{
          color: '#9ca3af',
          transform: open ? 'rotate(90deg)' : 'rotate(0deg)',
        }}>
          ▸
        </span>
        <span className="text-[11px] font-medium" style={{ color: '#f3f4f6' }}>{title}</span>
      </button>

      {/* Content */}
      {open && <div className="px-3 pb-3 pt-1">{children}</div>}
    </div>
  )
}

function FieldRow({ field }: { field: FieldData }) {
  const [value, setValue] = useState(field.value)

  const id = `field-${field.label}`

  const renderControl = () => {
    switch (field.type) {
      case 'text':
        return (
          <input
            id={id}
            className="w-full bg-transparent border-none outline-none text-[11px] text-right"
            style={{ color: '#d1d5db' }}
            value={value as string}
            onChange={(e) => setValue(e.target.value)}
          />
        )
      case 'select':
        return (
          <select
            id={id}
            className="bg-transparent border-none outline-none text-[11px] text-right cursor-pointer
                       appearance-none"
            style={{ color: '#d1d5db' }}
            value={value as string}
            onChange={(e) => setValue(e.target.value)}
          >
            {(field.options || []).map((opt) => (
              <option key={opt} value={opt}>{opt}</option>
            ))}
          </select>
        )
      case 'slider':
        return (
          <div className="flex items-center gap-2 flex-1 max-w-[140px]">
            <input
              type="range"
              id={id}
              min={field.min ?? 0}
              max={field.max ?? 100}
              step="any"
              value={value as number}
              onChange={(e) => setValue(parseFloat(e.target.value))}
              className="flex-1"
            />
            <span className="text-[10px] min-w-[32px] text-right" style={{ color: '#9ca3af' }}>
              {typeof value === 'number' ? value.toFixed(field.value === 3.5 ? 1 : 0) : value}{field.unit || ''}
            </span>
          </div>
        )
    }
  }

  return (
    <div className="flex items-center justify-between min-h-[26px] gap-2">
      <label htmlFor={id} className="text-[11px] shrink-0" style={{ color: '#9ca3af' }}>
        {field.label}
      </label>
      {renderControl()}
    </div>
  )
}

function ResourceBar({ resource }: { resource: Resource }) {
  return (
    <div className="flex items-center gap-2 mb-1.5 last:mb-0">
      <span className="text-[10px] w-[32px] font-medium" style={{ color: '#9ca3af' }}>{resource.label}</span>
      <div className="flex-1 h-[6px] rounded-full overflow-hidden" style={{ background: 'rgba(255,255,255,0.06)' }}>
        <div
          className="h-full rounded-full transition-all"
          style={{
            width: `${resource.value}%`,
            background: resource.color,
          }}
        />
      </div>
      <span className="text-[10px] w-[36px] text-right" style={{ color: '#d1d5db' }}>{resource.value}%</span>
    </div>
  )
}
