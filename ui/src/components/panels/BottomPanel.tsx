import { useState, useEffect, useRef, useMemo } from 'react'
import DataTable from './DataTable'
import DeviceTypeSelector from './DeviceTypeSelector'
import type { SelectorOption } from './DeviceTypeSelector'
import { useTrackData } from '../../hooks/useTrackData'
import { DeviceType } from '../../lib/protocol'
import type { AppDevice } from '../map/types'

interface LogEntry {
  time: string; id: string; message: string; integrity: 'Nominal' | 'Caution' | 'Breach'
}

const initialLogs: LogEntry[] = [
  { time: '14:20:44.02', id: 'CORE_SYS', message: '系统初始化完成，注册表握手成功。', integrity: 'Nominal' },
  { time: '14:21:12.88', id: 'NODE_ALPHA', message: '传感器已接入：thermal_array_01 运行中。', integrity: 'Nominal' },
  { time: '14:22:05.15', id: 'NODE_ALPHA', message: '频段 4.2GHz 检测到信号抖动。', integrity: 'Caution' },
  { time: '14:23:18.42', id: 'NETWORK', message: '与区域中继服务器 09-B 握手超时。', integrity: 'Breach' },
]

const messages = [
  ['NODE_ALPHA', '接收到深层传感器心跳。', 'Nominal'] as const,
  ['CORE_SYS', '缓存优化已完成。', 'Nominal'] as const,
  ['NETWORK', '次路上行链路出现延迟峰值。', 'Caution'] as const,
  ['SENSOR_A1', '区域 4 环境阈值超限。', 'Caution'] as const,
  ['SENSOR_A2', '阵列子区检测到热漂移。', 'Caution'] as const,
  ['NETWORK', '主上行链路已恢复，重传缓冲区。', 'Nominal'] as const,
]

const statusStyle: Record<string, { badge: string; text: string; border: string }> = {
  Nominal: {
    badge: 'rgba(16,185,129,0.08)',
    text: '#059669',
    border: 'rgba(16,185,129,0.15)',
  },
  Caution: {
    badge: 'rgba(234,179,8,0.08)',
    text: '#b45309',
    border: 'rgba(234,179,8,0.15)',
  },
  Breach: {
    badge: 'rgba(239,68,68,0.08)',
    text: '#dc2626',
    border: 'rgba(239,68,68,0.15)',
  },
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

  const current = useMemo(() => {
    return options.find(o => o.value === selectedValue) ?? options[0]
  }, [selectedValue, options])

  const { rows, deviceType, isLive } = useTrackData(current.deviceType, current.label)

  const handleChange = (value: string, dt: DeviceType) => {
    setSelectedValue(value)
  }

  if (options.length === 0) {
    return (
      <div className="flex flex-col flex-1 overflow-hidden">
        <div className="flex items-center shrink-0 px-5 py-1"
          style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
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
      <div className="flex items-center shrink-0 px-5 py-1"
        style={{ borderBottom: '1px solid rgba(229,231,226,0.35)' }}>
        <DeviceTypeSelector options={options} value={selectedValue} onChange={handleChange} />
      </div>
      <DataTable deviceType={deviceType} rows={rows} isLive={isLive} />
    </div>
  )
}

export default function BottomPanel({ allDevices }: { allDevices: AppDevice[] }) {
  const [collapsed, setCollapsed] = useState(false)
  const [tab, setTab] = useState<'activity' | 'track'>('activity')
  const [logs, setLogs] = useState<LogEntry[]>(initialLogs)
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null)
  const logEndRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    logEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [logs])

  useEffect(() => {
    intervalRef.current = setInterval(() => {
      if (Math.random() > 0.7) {
        const msg = messages[Math.floor(Math.random() * messages.length)]
        const now = new Date()
        const time = now.toLocaleTimeString('en-GB', { hour12: false }) + '.' +
          Math.floor(Math.random() * 99).toString().padStart(2, '0')
        setLogs((prev) => {
          const next = [...prev, { time, id: msg[0], message: msg[1], integrity: msg[2] }]
          return next.slice(-60)
        })
      }
    }, 4000)
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current)
    }
  }, [])

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
        </div>
        <div className="flex items-center gap-3">
          <div className="flex -space-x-2">
            <div className="w-6 h-6 rounded-full border-2 border-white flex items-center justify-center shadow-sm"
              style={{ background: 'var(--surfaceHigh)' }}>
              <span className="text-[8px] font-bold" style={{ color: 'var(--hairlineStrong)' }}>SY</span>
            </div>
            <div className="w-6 h-6 rounded-full border-2 border-white flex items-center justify-center shadow-sm"
              style={{ background: '#b0f0d6' }}>
              <span className="text-[8px] font-bold" style={{ color: 'var(--primaryContainer)' }}>01</span>
            </div>
          </div>
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
              <table className="w-full text-left border-separate" style={{ borderSpacing: '0 1px' }}>
                <thead className="text-[10px] font-bold uppercase tracking-wider sticky top-0 z-10"
                  style={{ color: '#9ca3af', background: 'rgba(255,255,255,0.55)', backdropFilter: 'blur(8px)' }}>
                  <tr>
                    <th className="pt-1.5 pb-1.5 font-medium pl-5 w-[110px]">时间戳</th>
                    <th className="pt-1.5 pb-1.5 font-medium w-[120px]">节点标识</th>
                    <th className="pt-1.5 pb-1.5 font-medium">详情描述</th>
                    <th className="pt-1.5 pb-1.5 font-medium text-right pr-5 w-[60px]">状态</th>
                  </tr>
                </thead>
                <tbody className="text-[13px]" style={{ fontFamily: "'Inter', 'Microsoft YaHei', sans-serif" }}>
                  {logs.map((log, i) => {
                    const s = statusStyle[log.integrity]
                    const idColor = log.integrity === 'Breach' ? 'var(--error)'
                      : log.integrity === 'Caution' ? '#b45309'
                        : 'var(--primaryBright)'
                    return (
                      <tr key={i}
                        className="group transition-colors hover:bg-stone-50"
                        style={{ borderTop: i > 0 ? '1px solid rgba(229,231,226,0.3)' : 'none' }}>
                        <td className="py-2.5 pl-5 text-[11px]"
                          style={{ fontFamily: "'JetBrains Mono', monospace", color: '#9ca3af' }}>
                          {log.time}
                        </td>
                        <td className="py-2.5 font-semibold text-[11px]"
                          style={{ fontFamily: "'JetBrains Mono', monospace", color: idColor }}>
                          [{log.id}]
                        </td>
                        <td className="py-2.5" style={{ color: 'var(--body)' }}>{log.message}</td>
                        <td className="py-2.5 text-right pr-5">
                          <span className="inline-flex items-center px-2 py-0.5 rounded-full text-[11px] font-medium border"
                            style={{ background: s.badge, color: s.text, borderColor: s.border }}>
                            {log.integrity === 'Nominal' ? '正常' : log.integrity === 'Caution' ? '警告' : '异常'}
                          </span>
                        </td>
                      </tr>
                    )
                  })}
                </tbody>
              </table>
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
