import { useState, useEffect, useRef, useMemo } from 'react'
import DataTable from './DataTable'
import DeviceTypeSelector from './DeviceTypeSelector'
import type { SelectorOption } from './DeviceTypeSelector'
import { useTrackData } from '../../hooks/useTrackData'
import { useCore } from '../../hooks/useCoreApi'
import { DeviceType } from '../../lib/protocol'
import type { AppDevice } from '../map/types'

type LogLevel = 'ALL' | 'INFO' | 'WARN' | 'ERROR'

const levelStyle: Record<string, { bg: string; text: string; label: string }> = {
  INFO:  { bg: 'rgba(16,185,129,0.08)', text: '#059669', label: 'INFO' },
  WARN:  { bg: 'rgba(234,179,8,0.08)',  text: '#b45309', label: 'WARN' },
  ERROR: { bg: 'rgba(239,68,68,0.08)',  text: '#dc2626', label: 'ERROR' },
}

const levelChips: LogLevel[] = ['ALL', 'INFO', 'WARN', 'ERROR']

function fmtTime(ts: number): string {
  const d = new Date(ts)
  return d.toLocaleTimeString('en-GB', { hour12: false }) + '.' +
    String(d.getMilliseconds()).padStart(3, '0')
}

function DataTableTab({ allDevices }: { allDevices: AppDevice[] }) {
  const [selectedValue, setSelectedValue] = useState('type_radar')
  const options: SelectorOption[] = useMemo(() => {
    return allDevices.map(d => {
      const dt = 'lng' in d ? DeviceType.RADAR : d.kind === 'opto' ? DeviceType.OPTOELECTRONIC
        : d.kind === 'level' ? DeviceType.LEVEL_SENSOR : DeviceType.GPS
      return { value: `${d.id}`, label: d.name, deviceType: dt }
    })
  }, [allDevices])
  const current = useMemo(() => options.find(o => o.value === selectedValue) ?? options[0], [selectedValue, options])
  const { rows, deviceType, isLive } = useTrackData(current.deviceType, current.label)
  if (options.length === 0) {
    return (
      <div className="flex flex-col flex-1 overflow-hidden">
        <div className="flex items-center shrink-0 px-5 py-1" style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
          <span className="text-[11px]" style={{ color: '#9ca3af' }}>暂无已注册设备</span>
        </div>
        <div className="flex-1 flex items-center justify-center" style={{ color: '#9ca3af' }}>
          <span className="text-[12px]">请在侧栏添加设备后查看数据</span>
        </div>
      </div>
    )
  }
  return (
    <div className="flex flex-col flex-1 overflow-hidden">
      <div className="flex items-center shrink-0 px-5 py-1" style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
        <DeviceTypeSelector options={options} value={selectedValue} onChange={(v) => setSelectedValue(v)} />
      </div>
      <DataTable deviceType={deviceType} rows={rows} isLive={isLive} />
    </div>
  )
}

export default function BottomPanel({ allDevices }: { allDevices: AppDevice[] }) {
  const [collapsed, setCollapsed] = useState(false)
  const [tab, setTab] = useState<'activity' | 'track'>('activity')
  const [filter, setFilter] = useState<LogLevel>('ALL')
  const { logEntries, systemEvents, historyLoading, historyError, historyViewNotice } = useCore()
  const logEndRef = useRef<HTMLDivElement>(null)

  // 按级别筛选
  const logs = useMemo(() =>
    filter === 'ALL' ? logEntries : logEntries.filter(l => l.level === filter),
  [logEntries, filter])

  // 自动滚到底部
  useEffect(() => {
    logEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [logs])

  return (
    <section
      className="flex flex-col z-20 overflow-hidden transition-all duration-200"
      style={{
        background: 'var(--surfaceLow)',
        borderTop: '1px solid rgba(191,201,195,0.3)',
        height: collapsed ? '34px' : '240px',
      }}
    >
      {/* Header */}
      <div className="px-5 flex items-center justify-between shrink-0 h-[34px]"
        style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
        <div className="flex items-center h-full gap-1.5">
          {(['activity', 'track'] as const).map((key) => (
            <button
              key={key}
              className="text-[11px] px-4 py-1.5 rounded-md border cursor-pointer transition-all font-semibold"
              style={{
                background: tab === key ? '#ffffff' : 'transparent',
                color: tab === key ? 'var(--primaryContainer)' : 'var(--onSurfaceVariant)',
                fontWeight: tab === key ? 600 : 400,
                boxShadow: tab === key ? '0 1px 3px rgba(0,0,0,0.05)' : 'none',
                borderColor: tab === key ? 'rgba(6,78,59,0.05)' : 'transparent',
                fontFamily: "'Inter', sans-serif",
              }}
              onClick={() => setTab(key)}
            >
              {key === 'activity' ? '活动日志' : '数据表格'}
            </button>
          ))}
          {/* 日志级别筛选 */}
          {tab === 'activity' && (
            <div className="flex items-center gap-0.5 ml-2">
              {levelChips.map(l => (
                <button key={l}
                  className="text-[9px] px-2 py-0.5 rounded-full border cursor-pointer font-medium transition-all"
                  style={{
                    background: filter === l ? 'var(--primaryContainer)' : 'transparent',
                    color: filter === l ? '#fff' : 'var(--secondary)',
                    borderColor: filter === l ? 'var(--primaryContainer)' : 'var(--hairline)',
                  }}
                  onClick={() => setFilter(l)}
                >{l}</button>
              ))}
            </div>
          )}
        </div>
        <div className="flex items-center gap-3">
          <button
            className="p-1 rounded border-none bg-transparent cursor-pointer transition-colors hover:bg-stone-100"
            style={{ color: '#9ca3af' }}
            onClick={() => setCollapsed((v) => !v)}
          >
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
              {collapsed
                ? <path d="M12 5v.01M12 12v.01M12 19v.01M12 6a1 1 0 110-2 1 1 0 010 2zm0 7a1 1 0 110-2 1 1 0 010 2zm0 7a1 1 0 110-2 1 1 0 010 2z" strokeLinecap="round" strokeLinejoin="round" />
                : <path d="M16 3v18M8 3v18" strokeLinecap="round" />
              }
            </svg>
          </button>
        </div>
      </div>

      {/* Content */}
      {!collapsed && (
        <div className="flex-1 flex flex-col overflow-hidden">
          {tab === 'activity' ? (
            <div className="flex-1 overflow-y-auto">
              {(historyViewNotice || historyLoading || historyError) && (
                <div className="px-5 py-1.5 text-[10px] shrink-0"
                  style={{ color: historyError ? 'var(--error)' : historyViewNotice ? '#b45309' : '#9ca3af', borderBottom: '1px solid rgba(229,231,226,0.3)' }}>
                  {historyLoading ? '正在加载任务历史数据…' : historyError || historyViewNotice}
                </div>
              )}
              {systemEvents.length > 0 && (
                <div className="px-5 py-2 shrink-0" style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
                  <div className="text-[10px] font-bold uppercase tracking-wider mb-1" style={{ color: '#9ca3af' }}>
                    任务事件 · {systemEvents.length}
                  </div>
                  <div className="space-y-1 max-h-[72px] overflow-y-auto">
                    {systemEvents.slice(0, 20).map((ev, i) => (
                      <div key={i} className="text-[10px] flex gap-2" style={{ color: 'var(--secondary)' }}>
                        <span style={{ fontFamily: "'JetBrains Mono', monospace", color: '#9ca3af' }}>
                          {fmtTime(ev.tsMs)}
                        </span>
                        <span className="font-semibold" style={{ color: '#64748b' }}>{ev.category}</span>
                        <span className="truncate flex-1">{ev.detail || ev.source}</span>
                      </div>
                    ))}
                  </div>
                </div>
              )}
              {logs.length === 0 ? (
                <div className="flex items-center justify-center h-full text-[11px]" style={{ color: '#9ca3af' }}>
                  {logEntries.length === 0 ? '等待后端日志...' : '无匹配日志'}
                </div>
              ) : (
                <table className="w-full text-left border-separate" style={{ borderSpacing: '0 1px' }}>
                  <thead className="text-[10px] font-bold uppercase tracking-wider sticky top-0 z-10"
                    style={{ color: '#9ca3af', background: 'rgba(255,255,255,0.55)', backdropFilter: 'blur(8px)' }}>
                    <tr>
                      <th className="pt-1.5 pb-1.5 font-medium pl-5 w-[110px]">时间戳</th>
                      <th className="pt-1.5 pb-1.5 font-medium w-[46px]">级别</th>
                      <th className="pt-1.5 pb-1.5 font-medium">日志内容</th>
                    </tr>
                  </thead>
                  <tbody className="text-[13px]" style={{ fontFamily: "'Inter', 'Microsoft YaHei', sans-serif" }}>
                    {logs.map((log, i) => {
                      const s = levelStyle[log.level] ?? levelStyle.INFO
                      return (
                        <tr key={i}
                          className="group transition-colors hover:bg-stone-50"
                          style={{ borderTop: i > 0 ? '1px solid rgba(229,231,226,0.3)' : 'none' }}>
                          <td className="py-2 pl-5 text-[10px]"
                            style={{ fontFamily: "'JetBrains Mono', monospace", color: '#9ca3af' }}>
                            {fmtTime(log.timestamp)}
                          </td>
                          <td className="py-2">
                            <span className="inline-flex items-center px-1.5 py-px rounded text-[9px] font-semibold"
                              style={{ background: s.bg, color: s.text }}>
                              {s.label}
                            </span>
                          </td>
                          <td className="py-2 text-[11px]" style={{ color: 'var(--body)' }}>{log.message}</td>
                        </tr>
                      )
                    })}
                  </tbody>
                </table>
              )}
              <div ref={logEndRef} />
            </div>
          ) : (
            <DataTableTab allDevices={allDevices} />
          )}
        </div>
      )}
    </section>
  )
}
