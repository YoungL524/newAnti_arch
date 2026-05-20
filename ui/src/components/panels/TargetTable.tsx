import { useState } from 'react'

interface Target {
  id: string
  dist: string
  az: string
  el: string
  speed: string
  rcs: string
  status: string
}

const targets: Target[] = [
  { id: 'T-001', dist: '1,250', az: '32.5°', el: '12.3°', speed: '45.2', rcs: '-12.5', status: '跟踪' },
  { id: 'T-002', dist: '850', az: '78.2°', el: '8.7°', speed: '32.1', rcs: '-18.2', status: '跟踪' },
  { id: 'T-003', dist: '1,680', az: '145.8°', el: '15.2°', speed: '78.5', rcs: '-8.5', status: '跟踪' },
  { id: 'T-004', dist: '420', az: '210.3°', el: '5.8°', speed: '12.8', rcs: '-22.1', status: '识别' },
  { id: 'T-005', dist: '1,950', az: '287.6°', el: '22.1°', speed: '120.3', rcs: '-5.2', status: '跟踪' },
  { id: 'T-006', dist: '620', az: '345.0°', el: '10.5°', speed: '55.6', rcs: '-15.8', status: '预警' },
  { id: 'T-007', dist: '1,430', az: '112.4°', el: '18.9°', speed: '88.9', rcs: '-10.3', status: '识别' },
]

const statusColors: Record<string, string> = {
  '跟踪': '#10b981',
  '识别': '#3b82f6',
  '预警': '#f59e0b',
}

export default function TargetTable() {
  const [search, setSearch] = useState('')

  const filtered = targets.filter((t) =>
    t.id.toLowerCase().includes(search.toLowerCase())
  )

  return (
    <div
      className="w-[400px] min-w-[400px] flex flex-col overflow-hidden"
      style={{ background: '#1f2937', borderLeft: '1px solid #374151' }}
    >
      {/* Title bar */}
      <div
        className="flex items-center h-[36px] min-h-[36px] px-3 gap-2"
        style={{ borderBottom: '1px solid #374151' }}
      >
        <span className="text-[12px] font-medium" style={{ color: '#f3f4f6' }}>目标表格</span>
        <span className="text-[10px] px-1.5 py-[1px] rounded"
          style={{ background: 'rgba(59,130,246,0.15)', color: '#3b82f6' }}>
          {targets.length}
        </span>
        <div className="flex-1" />
        <TableBtn title="导出 CSV">
          <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
            <path d="M7 1v8M4 6l3 3 3-3" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M2 10v2h10v-2" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
          </svg>
        </TableBtn>
        <TableBtn title="清除">
          <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
            <path d="M2 3h10M5 3V2a1 1 0 011-1h2a1 1 0 011 1v1M3 4l.7 7.7a1 1 0 001 .3h4.6a1 1 0 001-.3L11 4"
                  stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
        </TableBtn>
        <TableBtn title="过滤">
          <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
            <path d="M1 2h12l-4.5 5.5v4l-3 1.5v-5.5L1 2z" stroke="currentColor" strokeWidth="1.2" strokeLinejoin="round" />
          </svg>
        </TableBtn>
        <TableBtn title="列设置">
          <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
            <rect x="1" y="1" width="12" height="12" rx="1" stroke="currentColor" strokeWidth="1.2" />
            <path d="M1 5h12M5 1v12" stroke="currentColor" strokeWidth="1.2" />
          </svg>
        </TableBtn>
      </div>

      {/* Search */}
      <div className="px-3 py-2" style={{ borderBottom: '1px solid #374151' }}>
        <div
          className="flex items-center gap-1.5 px-2 py-1 rounded text-[11px]"
          style={{ background: 'rgba(0,0,0,0.2)', border: '1px solid #374151' }}
        >
          <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
            <circle cx="5" cy="5" r="3.5" stroke="rgba(255,255,255,0.25)" strokeWidth="1.2" />
            <path d="M7.5 7.5L10 10" stroke="rgba(255,255,255,0.25)" strokeWidth="1.2" strokeLinecap="round" />
          </svg>
          <input
            className="flex-1 bg-transparent border-none outline-none text-[11px]"
            style={{ color: '#f3f4f6' }}
            placeholder="搜索目标..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
          />
        </div>
      </div>

      {/* Table */}
      <div className="flex-1 overflow-auto">
        <table className="w-full text-[11px] border-collapse">
          <thead>
            <tr style={{ borderBottom: '1px solid #374151' }}>
              <Th>ID</Th>
              <Th>距离 (m)</Th>
              <Th>方位角</Th>
              <Th>俯仰角</Th>
              <Th>速度 (m/s)</Th>
              <Th>RCS (dBsm)</Th>
              <Th>状态</Th>
            </tr>
          </thead>
          <tbody>
            {filtered.map((t, i) => (
              <tr
                key={t.id}
                className="hover:bg-[rgba(255,255,255,0.03)] transition-colors"
                style={{
                  borderBottom: i < filtered.length - 1 ? '1px solid rgba(55,65,81,0.5)' : 'none',
                }}
              >
                <Td>
                  <span style={{ color: '#3b82f6' }}>{t.id}</span>
                </Td>
                <Td>{t.dist}</Td>
                <Td>{t.az}</Td>
                <Td>{t.el}</Td>
                <Td>{t.speed}</Td>
                <Td>{t.rcs}</Td>
                <Td>
                  <span
                    className="px-1.5 py-[1px] rounded text-[10px]"
                    style={{
                      background: `${statusColors[t.status]}20`,
                      color: statusColors[t.status],
                    }}
                  >
                    {t.status}
                  </span>
                </Td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  )
}

function Th({ children }: { children: React.ReactNode }) {
  return (
    <th
      className="text-left font-normal py-2 px-2 text-[10px] font-medium sticky top-0"
      style={{
        color: 'rgba(255,255,255,0.35)',
        background: '#1f2937',
        borderBottom: '1px solid #374151',
      }}
    >
      {children}
    </th>
  )
}

function Td({ children }: { children: React.ReactNode }) {
  return <td className="py-1.5 px-2" style={{ color: '#d1d5db' }}>{children}</td>
}

function TableBtn({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <button
      title={title}
      className="w-[24px] h-[24px] flex items-center justify-center rounded cursor-pointer border-none
                 hover:bg-[rgba(255,255,255,0.06)] transition-colors"
      style={{ background: 'transparent', color: '#9ca3af' }}
    >
      {children}
    </button>
  )
}
