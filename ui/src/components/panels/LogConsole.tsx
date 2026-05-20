import { useState } from 'react'

type Tab = '日志' | '告警' | '事件'

const tabs: Tab[] = ['日志', '告警', '事件']

interface LogEntry {
  time: string
  level: 'INFO' | 'WARN' | 'ERROR' | 'DEBUG'
  message: string
}

const logs: LogEntry[] = [
  { time: '14:32:05.123', level: 'INFO', message: '系统初始化完成，所有模块就绪' },
  { time: '14:32:05.456', level: 'INFO', message: '雷达传感器连接成功 [SN: RD-2024-0872]' },
  { time: '14:32:06.012', level: 'WARN', message: '信号增益略高于建议阈值 (42dB)' },
  { time: '14:32:06.789', level: 'INFO', message: '开始数据采集周期 #1' },
  { time: '14:32:07.234', level: 'ERROR', message: '目标跟踪丢失: TK-002, 信噪比低于下限' },
  { time: '14:32:08.001', level: 'INFO', message: '已检测到 7 个目标, 3 个正在跟踪' },
  { time: '14:32:08.567', level: 'DEBUG', message: 'CFAR 处理完成, 耗时 1.2ms' },
  { time: '14:32:09.123', level: 'WARN', message: '网络延迟增加, 当前 47ms' },
  { time: '14:32:10.456', level: 'INFO', message: '数据已写入数据库 (批次 #42)' },
]

const levelColors: Record<string, { text: string; bg: string }> = {
  INFO: { text: '#10b981', bg: 'rgba(16,185,129,0.12)' },
  WARN: { text: '#f59e0b', bg: 'rgba(245,158,11,0.12)' },
  ERROR: { text: '#ef4444', bg: 'rgba(239,68,68,0.12)' },
  DEBUG: { text: '#3b82f6', bg: 'rgba(59,130,246,0.12)' },
}

export default function LogConsole() {
  const [activeTab, setActiveTab] = useState<Tab>('日志')

  return (
    <div
      className="flex flex-col overflow-hidden"
      style={{ background: '#0b0f19', borderTop: '1px solid #374151' }}
    >
      {/* Toolbar */}
      <div
        className="flex items-center h-[32px] min-h-[32px] px-3 gap-3"
        style={{ borderBottom: '1px solid #374151' }}
      >
        {/* Tabs */}
        <div className="flex items-center gap-3 h-full">
          {tabs.map((tab) => (
            <button
              key={tab}
              className="relative h-full text-[11px] cursor-pointer border-none bg-transparent transition-colors"
              style={{
                color: activeTab === tab ? '#f3f4f6' : '#9ca3af',
                fontWeight: activeTab === tab ? 500 : 400,
              }}
              onClick={() => setActiveTab(tab)}
            >
              {tab}
              {activeTab === tab && (
                <div
                  className="absolute bottom-0 left-0 right-0 h-[2px]"
                  style={{ background: '#3b82f6' }}
                />
              )}
            </button>
          ))}
        </div>

        <div className="flex-1" />

        {/* Actions */}
        <div className="flex items-center gap-1">
          <LogBtn title="清空">
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
              <path d="M2 3h10M5 3V2a1 1 0 011-1h2a1 1 0 011 1v1M3 4l.7 7.7a1 1 0 001 .3h4.6a1 1 0 001-.3L11 4"
                    stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </LogBtn>
          <LogBtn title="暂停">
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
              <rect x="3" y="2" width="3" height="10" rx="0.5" stroke="currentColor" strokeWidth="1.2" />
              <rect x="8" y="2" width="3" height="10" rx="0.5" stroke="currentColor" strokeWidth="1.2" />
            </svg>
          </LogBtn>
          <LogBtn title="自动滚动">
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
              <path d="M7 1v8M4 6l3 3 3-3" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
              <path d="M2 11v1h10v-1" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
            </svg>
          </LogBtn>
        </div>
      </div>

      {/* Log content */}
      <div className="flex-1 overflow-y-auto font-mono">
        {logs.map((log, i) => (
          <div
            key={i}
            className="flex items-start gap-2 px-3 py-[3px] text-[11px] leading-[18px]
                       hover:bg-[rgba(255,255,255,0.02)] transition-colors"
          >
            <span style={{ color: 'rgba(255,255,255,0.25)', minWidth: '80px' }}>{log.time}</span>
            <span
              className="px-1 rounded text-[10px] font-medium min-w-[38px] text-center"
              style={{
                color: levelColors[log.level].text,
                background: levelColors[log.level].bg,
              }}
            >
              {log.level}
            </span>
            <span style={{ color: '#d1d5db' }}>{log.message}</span>
          </div>
        ))}
      </div>
    </div>
  )
}

function LogBtn({ title, children }: { title: string; children: React.ReactNode }) {
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
