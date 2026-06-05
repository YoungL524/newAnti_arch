import type { Task } from '../../lib/taskTypes'
import { useCore } from '../../hooks/useCoreApi'

const STATUS_LABEL: Record<string, string> = {
  active: '进行中', completed: '已完成', archived: '已归档',
}

export default function TitleBar({ activeTask }: { activeTask: Task | null }) {
  const { connected, acquisition, state, checkedDeviceIds, devices, noSelectionHint, commandError, dismissNoSelectionHint, dismissCommandError, toggleAcquisition } = useCore()

  const isTransient = state === 'Starting' || state === 'Stopping'
  const isFault = state === 'Fault'
  const checkedCount = checkedDeviceIds.length

  const btnLabel = isFault ? '复位'
    : acquisition ? '停止采集'
    : '开始采集'

  const btnBg = isFault ? '#ef4444'
    : 'var(--primaryContainer)'

  return (
    <header className="h-11 glass-panel flex justify-between items-center w-full px-4 z-20 shrink-0 relative"
      style={{ borderBottom: '1px solid rgba(229, 231, 226, 0.5)' }}
    >
      <div className="flex items-center gap-3">
        <div className="flex items-center gap-2 px-2.5 py-0.5 rounded-full border"
          style={{
            background: 'var(--emeraldSubtle)',
            borderColor: 'rgba(6, 78, 59, 0.1)',
          }}>
          <span className="w-1.5 h-1.5 rounded-full animate-pulse"
            style={{
              background: connected ? 'var(--primaryContainer)' : 'var(--error)',
              boxShadow: connected ? '0 0 8px rgba(6,78,59,0.5)' : 'none',
            }} />
          <span className="text-[10px] font-bold tracking-wider"
            style={{ color: 'var(--primaryContainer)', fontFamily: "'Inter', sans-serif" }}>
            {connected ? '已连接' : '已断开'}
          </span>
        </div>

        {activeTask && (
          <div className="flex items-center gap-2 text-[11px]">
            <span style={{ color: 'var(--secondary)' }}>任务:</span>
            <span className="font-semibold" style={{ color: 'var(--ink)' }}>{activeTask.name}</span>
            <span className="text-[9px] px-1.5 py-px rounded-full font-medium"
              style={{ background: 'rgba(6,78,59,0.08)', color: 'var(--primaryContainer)' }}>
              {STATUS_LABEL[activeTask.status]}
            </span>
          </div>
        )}

        {/* Device count badge */}
        {devices.length > 0 && (
          <span className="text-[9px]" style={{ color: 'var(--secondary)' }}>
            {acquisition
              ? `${devices.filter(d => d.acquisition).length}/${devices.length} 工作中`
              : `${devices.length} 设备`}
            {checkedCount > 0 && ` · 已选 ${checkedCount}`}
          </span>
        )}
      </div>

      <button
        className="px-3.5 py-1 rounded-lg text-white font-semibold text-[11px] border-none cursor-pointer
                   precision-shadow active:scale-95 transition-all disabled:opacity-40 disabled:cursor-not-allowed"
        style={{ background: btnBg }}
        onClick={toggleAcquisition}
        disabled={isTransient}
      >
        {btnLabel}
      </button>

      {/* 未勾选设备提示 */}
      {noSelectionHint && (
        <div className="absolute top-full right-4 mt-1 px-3 py-2 rounded-lg text-[11px] z-50"
          style={{ background: '#fef2f2', border: '1px solid #fecaca', color: '#991b1b' }}>
          请先在设备列表中勾选要控制的雷达
          <button className="ml-2 px-1.5 py-0.5 rounded text-[10px] border-none cursor-pointer"
            style={{ background: '#fee2e2', color: '#991b1b' }}
            onClick={dismissNoSelectionHint}
          >×</button>
        </div>
      )}
      {commandError && (
        <div className="absolute top-full right-4 mt-1 px-3 py-2 rounded-lg text-[11px] z-50"
          style={{ background: '#fef2f2', border: '1px solid #fecaca', color: '#991b1b' }}>
          ⚠ {commandError}
          <button className="ml-2 px-1.5 py-0.5 rounded text-[10px] border-none cursor-pointer"
            style={{ background: '#fee2e2', color: '#991b1b' }}
            onClick={dismissCommandError}
          >×</button>
        </div>
      )}
    </header>
  )
}
