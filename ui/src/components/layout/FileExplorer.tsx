import { useState } from 'react'

export default function FileExplorer({ onClose }: { onClose?: () => void }) {
  const [projOpen, setProjOpen] = useState(true)
  const [devOpen, setDevOpen] = useState(true)

  return (
    <div
      className="h-full flex flex-col overflow-hidden rounded shadow-sm"
      style={{
        background: 'rgba(255,255,255,0.92)',
        backdropFilter: 'blur(10px)',
        border: '1px solid var(--hairline)',
      }}
    >
      {/* Header */}
      <div
        className="flex items-center justify-between px-3 py-2 shrink-0"
        style={{ borderBottom: '1px solid var(--hairline)' }}
      >
        <span
          className="text-[11px] font-semibold uppercase tracking-[0.08em]"
          style={{ color: 'var(--secondary)' }}
        >
          资源管理器
        </span>
        {onClose && (
          <button
            className="flex items-center justify-center w-5 h-5 rounded cursor-pointer border-none
                       hover:bg-[rgba(0,0,0,0.04)] transition-colors"
            style={{ background: 'transparent', color: 'var(--secondary)' }}
            onClick={onClose}
            title="收起面板"
          >
            <svg width="12" height="12" viewBox="0 0 14 14" fill="none">
              <path d="M10 2L6 7l4 5M4 2l-4 5 4 5" stroke="currentColor" strokeWidth="1.3"
                strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </button>
        )}
      </div>

      {/* Content */}
      <div className="flex-1 overflow-y-auto p-2">
        {/* 工程项目 */}
        <div className="mb-4">
          <div
            className="flex items-center gap-2 px-2 py-1.5 rounded cursor-pointer hover:bg-[rgba(0,0,0,0.03)] group"
            style={{ color: 'var(--ink)' }}
            onClick={() => setProjOpen(!projOpen)}
          >
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none"
              style={{ color: 'var(--secondary)', transform: projOpen ? 'rotate(90deg)' : 'rotate(0deg)', transition: 'transform 0.15s' }}>
              <path d="M5 2l5 5-5 5" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            <span className="text-[13px] font-medium">工程项目</span>
          </div>
          {projOpen && (
            <div className="pl-6 flex flex-col gap-0.5 mt-1">
              <FileItem label="config_main.yaml" />
              <FileItem label="calibration_data.json" />
            </div>
          )}
        </div>

        {/* 设备列表 */}
        <div>
          <div
            className="flex items-center gap-2 px-2 py-1.5 rounded cursor-pointer hover:bg-[rgba(0,0,0,0.03)] group"
            style={{ color: 'var(--ink)' }}
            onClick={() => setDevOpen(!devOpen)}
          >
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none"
              style={{ color: 'var(--secondary)', transform: devOpen ? 'rotate(90deg)' : 'rotate(0deg)', transition: 'transform 0.15s' }}>
              <path d="M5 2l5 5-5 5" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            <span className="text-[13px] font-medium">设备列表</span>
          </div>
          {devOpen && (
            <div className="pl-6 flex flex-col gap-0.5 mt-1">
              <DeviceItem label="Array_01 (Active)" active />
              <DeviceItem label="Array_02 (Standby)" />
            </div>
          )}
        </div>
      </div>
    </div>
  )
}

function FileItem({ label }: { label: string }) {
  return (
    <div
      className="flex items-center gap-2 px-2 py-1 rounded cursor-pointer hover:bg-[rgba(0,0,0,0.03)]
                 transition-colors text-[13px]"
      style={{ color: 'var(--secondary)' }}
    >
      <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
        <path d="M3 1h8v12H3V1z" stroke="currentColor" strokeWidth="1.2" />
        <path d="M5 4h4M5 7h4M5 10h3" stroke="currentColor" strokeWidth="1" strokeLinecap="round" />
      </svg>
      <span>{label}</span>
    </div>
  )
}

function DeviceItem({ label, active }: { label: string; active?: boolean }) {
  return (
    <div
      className="flex items-center gap-2 px-2 py-1 rounded cursor-pointer transition-colors text-[13px]"
      style={{
        color: active ? 'var(--primaryBright)' : 'var(--secondary)',
        background: active ? 'var(--surfaceHigh)' : 'transparent',
      }}
    >
      <svg width="14" height="14" viewBox="0 0 14 14" fill="none"
        style={{ color: active ? 'var(--targetGrep)' : 'var(--secondary)' }}>
        <rect x="2" y="2" width="10" height="7" rx="1.5" stroke="currentColor" strokeWidth="1.2" />
        <path d="M5 9v2h4V9" stroke="currentColor" strokeWidth="1.2" />
        <circle cx="7" cy="12" r="0.5" fill="currentColor" />
      </svg>
      <span>{label}</span>
    </div>
  )
}
