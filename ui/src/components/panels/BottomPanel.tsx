import { useState } from 'react'

type TabKey = 'targets' | 'logs' | 'alerts' | 'events'

interface Tab { key: TabKey; label: string; count?: number }

const tabs: Tab[] = [
  { key: 'targets', label: '目标列表', count: 7 },
  { key: 'logs', label: '日志', count: 45 },
  { key: 'alerts', label: '告警', count: 3 },
  { key: 'events', label: '事件', count: 12 },
]

interface Target {
  id: string; dist: string; az: string; el: string; speed: string; rcs: string; status: string
}

const targets: Target[] = [
  { id: 'T-001', dist: '1,250', az: '32.5°', el: '12.3°', speed: '45.2', rcs: '-12.5', status: '跟踪' },
  { id: 'T-002', dist: '850', az: '78.2°', el: '8.7°', speed: '32.1', rcs: '-18.2', status: '跟踪' },
  { id: 'T-003', dist: '1,680', az: '145.8°', el: '15.2°', speed: '78.5', rcs: '-8.5', status: '跟踪' },
  { id: 'T-004', dist: '420', az: '210.3°', el: '5.8°', speed: '12.8', rcs: '-22.1', status: '识别' },
  { id: 'T-005', dist: '1,950', az: '287.6°', el: '22.1°', speed: '120.3', rcs: '-5.2', status: '跟踪' },
  { id: 'T-006', dist: '620', az: '345.0°', el: '10.5°', speed: '55.6', rcs: '-15.8', status: '预警' },
]

const statusColors: Record<string, string> = {
  '跟踪': '#9fc9a2', '识别': '#9fbbe0', '预警': '#dfa88f',
}

interface LogEntry { time: string; level: 'INFO' | 'WARN' | 'ERROR' | 'DEBUG'; message: string }

const logs: LogEntry[] = [
  { time: '14:32:05', level: 'INFO', message: '系统初始化完成，所有模块就绪' },
  { time: '14:32:05', level: 'INFO', message: '雷达传感器连接成功 [SN: RD-2024-0872]' },
  { time: '14:32:06', level: 'WARN', message: '信号增益略高于建议阈值 (42dB)' },
  { time: '14:32:06', level: 'INFO', message: '开始数据采集周期 #1' },
  { time: '14:32:07', level: 'ERROR', message: '目标跟踪丢失: TK-002, 信噪比低于下限' },
  { time: '14:32:08', level: 'INFO', message: '已检测到 7 个目标, 3 个正在跟踪' },
  { time: '14:32:09', level: 'WARN', message: '网络延迟增加, 当前 47ms' },
  { time: '14:32:10', level: 'INFO', message: '数据已写入数据库 (批次 #42)' },
]

const logColors: Record<string, { text: string; bg: string }> = {
  INFO: { text: '#9fc9a2', bg: 'rgba(159,201,162,0.25)' },
  WARN: { text: '#c08532', bg: 'rgba(223,168,143,0.25)' },
  ERROR: { text: '#ba1a1a', bg: 'rgba(255,218,214,0.6)' },
  DEBUG: { text: '#005baf', bg: 'rgba(159,187,224,0.3)' },
}

interface Alert { time: string; level: 'critical' | 'major' | 'minor'; message: string }

const alerts: Alert[] = [
  { time: '14:32:07', level: 'critical', message: '目标 TK-002 信号丢失，信噪比低于下限' },
  { time: '14:31:52', level: 'major', message: '扇区 B 信号衰减超过阈值 15%' },
  { time: '14:30:18', level: 'minor', message: '数据库写入延迟告警 (42ms > 30ms)' },
]

const alertColors: Record<string, { text: string; bg: string }> = {
  critical: { text: '#ba1a1a', bg: 'var(--errorContainer)' },
  major: { text: '#c08532', bg: 'rgba(223,168,143,0.25)' },
  minor: { text: '#005baf', bg: 'rgba(159,187,224,0.3)' },
}

interface Event { time: string; type: string; detail: string }

const events: Event[] = [
  { time: '14:32:10', type: '数据存储', detail: '批次 #42 写入完成, 12 条记录' },
  { time: '14:32:05', type: '系统启动', detail: '雷达初始化完成, 进入待机状态' },
  { time: '14:32:06', type: '采集开始', detail: '数据采集周期 #1 启动' },
  { time: '14:31:00', type: '配置变更', detail: '用户修改了 CFAR 参数: CA → OS' },
]

export default function BottomPanel() {
  const [active, setActive] = useState<TabKey>('logs')
  const [collapsed, setCollapsed] = useState(false)

  return (
    <div
      className="absolute bottom-0 left-0 right-0 z-20 transition-all duration-200"
      style={{
        background: 'rgba(255,255,255,0.92)',
        backdropFilter: 'blur(10px)',
        borderTop: '1px solid var(--hairline)',
        boxShadow: '0 -2px 12px rgba(0,0,0,0.04)',
        height: collapsed ? '36px' : '200px',
      }}
    >
      {/* Tab bar */}
      <div
        className="flex items-center h-9 px-3 shrink-0"
        style={{
          background: 'rgba(255,255,255,0.5)',
          borderBottom: collapsed ? 'none' : '1px solid var(--hairline)',
        }}
      >
        {/* Tabs */}
        <div className="flex items-center h-full gap-6">
          {tabs.map((tab) => (
            <button
              key={tab.key}
              className="relative h-full flex items-center gap-2 text-[13px] cursor-pointer
                         border-none bg-transparent transition-colors"
              style={{
                color: active === tab.key ? 'var(--primaryBright)' : 'var(--secondary)',
                fontWeight: active === tab.key ? 500 : 400,
                borderBottom: active === tab.key
                  ? '2px solid var(--primaryBright)'
                  : '2px solid transparent',
              }}
              onClick={() => { setActive(tab.key); setCollapsed(false) }}
            >
              {tab.label}
              {tab.count != null && (
                <span
                  className="text-[10px] px-1.5 rounded-full font-medium"
                  style={{
                    background: active === tab.key ? 'var(--primary)' : 'var(--surfaceHigh)',
                    color: active === tab.key ? '#ffffff' : 'var(--secondary)',
                  }}
                >
                  {tab.count}
                </span>
              )}
            </button>
          ))}
        </div>

        <div className="flex-1" />

        {/* Actions */}
        <div className="flex items-center gap-2">
          <ActionBtn title="清空">
            <svg width="15" height="15" viewBox="0 0 15 15" fill="none">
              <path d="M2 3.5h11M5 3.5V2.5a1 1 0 011-1h3a1 1 0 011 1v1M3.5 4l.7 7.7a1 1 0 001 .3h4.6a1 1 0 001-.3L11.5 4"
                stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </ActionBtn>
          <ActionBtn title="导出">
            <svg width="15" height="15" viewBox="0 0 15 15" fill="none">
              <path d="M7.5 1.5v8M4 6l3.5 3.5L11 6" stroke="currentColor" strokeWidth="1.2"
                strokeLinecap="round" strokeLinejoin="round" />
              <path d="M1.5 10.5v2h12v-2" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
            </svg>
          </ActionBtn>
          <div className="w-px h-4" style={{ background: 'var(--hairline)' }} />
          <ActionBtn title={collapsed ? '展开' : '收起'}>
            <svg width="15" height="15" viewBox="0 0 15 15" fill="none"
              style={{ transform: collapsed ? 'rotate(0deg)' : 'rotate(180deg)', transition: 'transform 0.2s' }}>
              <path d="M3 6l4.5 4.5L12 6" stroke="currentColor" strokeWidth="1.2"
                strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </ActionBtn>
          <ActionBtn title="关闭" onClick={() => setCollapsed(!collapsed)}>
            <svg width="15" height="15" viewBox="0 0 15 15" fill="none"
              style={{ transform: collapsed ? 'rotate(0deg)' : 'rotate(180deg)', transition: 'transform 0.2s' }}>
              <path d="M3 9l4.5-4.5L12 9" stroke="currentColor" strokeWidth="1.2"
                strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </ActionBtn>
        </div>
      </div>

      {/* Content */}
      {!collapsed && (
        <div className="overflow-y-auto" style={{ height: '164px', background: 'rgba(255,255,255,0.5)' }}>
          <TargetsView active={active === 'targets'} />
          <LogsView active={active === 'logs'} />
          <AlertsView active={active === 'alerts'} />
          <EventsView active={active === 'events'} />
        </div>
      )}
    </div>
  )
}

function ActionBtn({ title, children, onClick }: { title: string; children: React.ReactNode; onClick?: () => void }) {
  return (
    <button
      title={title}
      className="flex items-center justify-center w-7 h-7 rounded cursor-pointer border-none transition-colors
                 hover:bg-[rgba(0,0,0,0.04)]"
      style={{ background: 'transparent', color: 'var(--secondary)' }}
      onClick={onClick}
    >
      {children}
    </button>
  )
}

/* ── Sub-views ── */

function TargetsView({ active }: { active: boolean }) {
  if (!active) return null
  return (
    <table className="w-full text-[12px] border-collapse">
      <thead>
        <tr style={{ borderBottom: '1px solid var(--hairline)' }}>
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
        {targets.map((t, i) => (
          <tr
            key={t.id}
            className="hover:bg-[rgba(0,0,0,0.02)]"
            style={{ borderBottom: i < targets.length - 1 ? '1px solid var(--hairline)' : 'none',
              opacity: 1 }}
          >
            <Td><span style={{ color: 'var(--primaryBright)', fontWeight: 500 }}>{t.id}</span></Td>
            <Td>{t.dist}</Td>
            <Td>{t.az}</Td>
            <Td>{t.el}</Td>
            <Td>{t.speed}</Td>
            <Td>{t.rcs}</Td>
            <Td>
              <span className="px-1.5 py-[1px] rounded text-[10px] font-medium"
                style={{ background: `${statusColors[t.status]}30`, color: statusColors[t.status] }}>
                {t.status}
              </span>
            </Td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

function LogsView({ active }: { active: boolean }) {
  if (!active) return null
  return (
    <div className="px-4 py-2 text-[12px]" style={{ fontFamily: "'JetBrains Mono', monospace" }}>
      <table className="w-full text-left border-collapse">
        <tbody>
          {logs.map((log, i) => (
            <tr key={i} className="hover:bg-[rgba(0,0,0,0.02)]">
              <td className="py-[2px] w-[72px]" style={{ color: 'var(--secondary)' }}>{log.time}</td>
              <td className="py-[2px] w-[56px]">
                <span className="px-1 rounded text-[10px] font-bold"
                  style={{ color: logColors[log.level].text, background: logColors[log.level].bg }}>
                  {log.level}
                </span>
              </td>
              <td className="py-[2px]" style={{ color: 'var(--ink)' }}>{log.message}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function AlertsView({ active }: { active: boolean }) {
  if (!active) return null
  return (
    <div className="px-4 py-2 text-[12px]">
      {alerts.map((a, i) => (
        <div key={i} className="flex items-start gap-2 py-[3px] hover:bg-[rgba(0,0,0,0.02)]">
          <span className="w-1.5 h-1.5 rounded-full mt-1.5 shrink-0" style={{ background: alertColors[a.level].text }} />
          <span style={{ color: 'var(--secondary)', minWidth: '64px' }}>{a.time}</span>
          <span className="px-1 rounded text-[10px] font-medium"
            style={{ color: alertColors[a.level].text, background: alertColors[a.level].bg }}>
            {a.level === 'critical' ? '严重' : a.level === 'major' ? '重要' : '轻微'}
          </span>
          <span style={{ color: 'var(--ink)' }}>{a.message}</span>
        </div>
      ))}
    </div>
  )
}

function EventsView({ active }: { active: boolean }) {
  if (!active) return null
  return (
    <div className="px-4 py-2 text-[12px]">
      {events.map((e, i) => (
        <div key={i} className="flex items-start gap-2 py-[3px] hover:bg-[rgba(0,0,0,0.02)]">
          <span style={{ color: 'var(--secondary)', minWidth: '64px' }}>{e.time}</span>
          <span className="px-1 rounded text-[10px] font-medium"
            style={{ color: '#005baf', background: 'rgba(159,187,224,0.3)' }}>
            {e.type}
          </span>
          <span style={{ color: 'var(--ink)' }}>{e.detail}</span>
        </div>
      ))}
    </div>
  )
}

function Th({ children }: { children: React.ReactNode }) {
  return (
    <th className="text-left font-semibold py-2 px-2 text-[10px] sticky top-0"
      style={{ color: 'var(--secondary)', background: 'rgba(255,255,255,0.95)' }}>
      {children}
    </th>
  )
}

function Td({ children }: { children: React.ReactNode }) {
  return <td className="py-1.5 px-2" style={{ color: 'var(--ink)' }}>{children}</td>
}
