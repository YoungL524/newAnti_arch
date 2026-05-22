import { useState, useEffect, useRef } from 'react'

interface LogEntry {
  time: string; id: string; message: string; integrity: 'Nominal' | 'Caution' | 'Breach'
}

const initialLogs: LogEntry[] = [
  { time: '14:20:44.02', id: 'CORE_SYS', message: '系统初始化完成，注册表握手成功。', integrity: 'Nominal' },
  { time: '14:21:12.88', id: 'NODE_ALPHA', message: '传感器已接入：thermal_array_01 运行中。', integrity: 'Nominal' },
  { time: '14:22:05.15', id: 'NODE_ALPHA', message: '频段 4.2GHz 检测到信号抖动。', integrity: 'Caution' },
  { time: '14:23:18.42', id: 'NETWORK', message: '与区域中继服务器 09-B 握手超时。', integrity: 'Breach' },
]

type FilterKey = 'all' | 'critical' | 'sensors'

const messages = [
  ['NODE_ALPHA', '接收到深层传感器心跳。', 'Nominal'] as const,
  ['CORE_SYS', '缓存优化已完成。', 'Nominal'] as const,
  ['NETWORK', '次路上行链路出现延迟峰值。', 'Caution'] as const,
  ['SENSOR_A1', '区域 4 环境阈值超限。', 'Caution'] as const,
  ['SENSOR_A2', '阵列子区检测到热漂移。', 'Caution'] as const,
  ['NETWORK', '主上行链路已恢复，重传缓冲区。', 'Nominal'] as const,
]

const integrityStyle: Record<string, { bg: string; text: string; border: string }> = {
  Nominal: {
    bg: 'var(--emeraldSubtle)',
    text: 'var(--primaryContainer)',
    border: 'rgba(6, 78, 59, 0.1)',
  },
  Caution: {
    bg: 'rgba(202, 138, 4, 0.1)',
    text: 'var(--primaryContainer)',
    border: 'rgba(202, 138, 4, 0.15)',
  },
  Breach: {
    bg: 'var(--errorContainer)',
    text: '#93000a',
    border: 'rgba(186, 26, 26, 0.1)',
  },
}

export default function BottomPanel() {
  const [collapsed, setCollapsed] = useState(false)
  const [filter, setFilter] = useState<FilterKey>('all')
  const [logs, setLogs] = useState<LogEntry[]>(initialLogs)
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null)

  useEffect(() => {
    intervalRef.current = setInterval(() => {
      if (Math.random() > 0.7) {
        const msg = messages[Math.floor(Math.random() * messages.length)]
        const now = new Date()
        const time = now.toLocaleTimeString('en-GB', { hour12: false }) + '.' +
          Math.floor(Math.random() * 99).toString().padStart(2, '0')
        setLogs((prev) => {
          const next = [{ time, id: msg[0], message: msg[1], integrity: msg[2] }, ...prev]
          return next.slice(0, 8)
        })
      }
    }, 8000)
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current)
    }
  }, [])

  const filtered = filter === 'all'
    ? logs
    : filter === 'critical'
      ? logs.filter((l) => l.integrity === 'Breach' || l.integrity === 'Caution')
      : logs.filter((l) => l.id === 'SENSOR_A1' || l.id === 'SENSOR_A2' || l.id === 'NODE_ALPHA')

  return (
    <section
      className="glass-panel flex flex-col z-20 overflow-hidden transition-all duration-200"
      style={{
        borderTop: '1px solid rgba(229, 231, 226, 0.5)',
        borderBottom: 'none',
        borderLeft: 'none',
        borderRight: 'none',
        height: collapsed ? '36px' : '200px',
      }}
    >
      {/* Header */}
      <div className="px-4 py-1.5 flex items-center justify-between bg-white/30 border-b border-stone-200/30 shrink-0">
        <div className="flex items-center gap-5">
          <div className="flex items-center gap-2">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6"
              style={{ color: 'var(--primaryContainer)' }}>
              <path d="M4 17l6-6-6-6M12 19h8" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            <h2 className="text-[10px] font-bold tracking-[0.18em] uppercase opacity-80"
              style={{ color: 'var(--primaryContainer)', fontFamily: "'Inter', sans-serif" }}>
              活动日志
            </h2>
          </div>
          <div className="flex gap-2">
            {([
              ['all', '全部信号'],
              ['critical', '严重'],
              ['sensors', '传感器'],
            ] as const).map(([key, label]) => (
              <button
                key={key}
                className="text-[10px] px-2.5 py-0.5 rounded-full border-none cursor-pointer
                           transition-colors font-semibold"
                style={{
                  background: filter === key ? 'var(--primaryContainer)' : 'transparent',
                  color: filter === key ? '#ffffff' : 'var(--onSurfaceVariant)',
                }}
                onClick={() => setFilter(key)}
              >
                {label}
              </button>
            ))}
          </div>
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
            className="p-1 rounded-md border-none bg-transparent cursor-pointer transition-colors hover:bg-stone-100"
            style={{ color: 'var(--onSurfaceVariant)' }}
            onClick={() => setCollapsed((v) => !v)}
          >
            {collapsed ? (
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <path d="M8 3v18l8-9-8-9z" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
            ) : (
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <path d="M16 3v18M8 3v18" strokeLinecap="round" />
              </svg>
            )}
          </button>
        </div>
      </div>

      {/* Table */}
      {!collapsed && (
        <div className="flex-1 overflow-y-auto px-4 py-2 bg-white/20">
          <table className="w-full text-left border-separate" style={{ borderSpacing: '0 3px' }}>
            <thead>
              <tr className="text-[10px] opacity-60" style={{ color: 'var(--hairlineStrong)' }}>
                <th className="font-semibold pb-2 pl-4 tracking-[0.1em]">时间戳</th>
                <th className="font-semibold pb-2 tracking-[0.1em]">系统 ID</th>
                <th className="font-semibold pb-2 tracking-[0.1em]">事件</th>
                <th className="font-semibold pb-2 text-right pr-6 tracking-[0.1em]">状态</th>
              </tr>
            </thead>
            <tbody style={{ fontFamily: "'JetBrains Mono', monospace", fontSize: '11px' }}>
              {filtered.map((log, i) => {
                const s = integrityStyle[log.integrity]
                const idColor = log.integrity === 'Breach' ? 'var(--error)'
                  : log.integrity === 'Caution' ? 'var(--tertiaryContainer)'
                    : 'var(--primaryContainer)'
                return (
                  <tr key={i}
                    className="transition-all group"
                    style={{ background: 'rgba(255,255,255,0.6)', border: '1px solid rgba(229,231,226,0.1)', borderRadius: '0.75rem' }}
                  >
                    <td className="py-1.5 pl-3 rounded-l-md" style={{ color: 'var(--hairlineStrong)' }}>{log.time}</td>
                    <td className="py-1.5 font-semibold" style={{ color: idColor }}>{log.id}</td>
                    <td className="py-1.5" style={{ color: 'rgba(25, 28, 27, 0.8)' }}>{log.message}</td>
                    <td className="py-1.5 text-right pr-3 rounded-r-md">
                      <span className="px-2 py-0.5 rounded text-[9px] font-bold tracking-wider border"
                        style={{ background: s.bg, color: s.text, borderColor: s.border }}>
                        {log.integrity === 'Nominal' ? '正常' : log.integrity === 'Caution' ? '警告' : '异常'}
                      </span>
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
      )}
    </section>
  )
}
