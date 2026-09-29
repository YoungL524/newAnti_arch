/**
 * @file TitleBar.tsx
 * @brief 显示当前任务名称、生命周期状态和设备执行摘要。
 * @details 所有值均来自上层任务状态，标题栏不发起任务命令，也不修改任务对象。
 */

import type { Task } from '../../lib/taskTypes'
import { useCore } from '../../hooks/useCoreApi'

const STATUS_LABEL: Record<string, string> = {
  draft: '草稿', ready: '已就绪', active: '进行中', faulted: '故障',
  completed: '已完成', archived: '已归档',
}

export default function TitleBar({ activeTask, viewTask, isHistoryView, onOpenRadarSettings, radarSettingsDisabled = false }: {
  activeTask: Task | null
  viewTask: Task | null
  isHistoryView: boolean
  /** 打开当前任务的“雷达参数”面板；由 App.tsx 提供。 */
  onOpenRadarSettings: () => void
  /** 任务缺失、历史视图、非活动阶段或更新进行中时为 true；后端断开在本组件内一并判定。 */
  radarSettingsDisabled?: boolean
}) {
  const { connected, acquisition, state, commandPending, checkedDeviceIds, devices,
    noSelectionHint, commandError, interactionError, toggleAcquisition,
    dismissNoSelectionHint, dismissCommandError, dismissInteractionError } = useCore()
  const backendStatus = connected ? 'online' : 'offline'
  const settingsDisabled = radarSettingsDisabled || !connected

  const isFault = state === 'Fault'
  const checkedCount = checkedDeviceIds.length

  const btnLabel = isFault ? '复位'
    : acquisition ? '停止采集'
    : '开始采集'

  const btnBg = isFault ? '#ef4444'
    : 'var(--primaryContainer)'

  const hintStyle = {
    background: '#fef2f2',
    border: '1px solid #fecaca',
    color: '#991b1b',
  } as const
  const hintDismissStyle = { background: '#fee2e2', color: '#991b1b' } as const
  const hasHints = noSelectionHint || !!commandError || !!interactionError

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
              background: backendStatus === 'online'
                ? 'var(--primaryContainer)'
                : 'var(--error)',
              boxShadow: backendStatus === 'online' ? '0 0 8px rgba(6,78,59,0.5)' : 'none',
            }} />
          <span className="text-[10px] font-bold tracking-wider"
            style={{ color: 'var(--primaryContainer)', fontFamily: "'Inter', sans-serif" }}>
            {backendStatus === 'offline' ? '后端未连接'
              : '已连接'}
          </span>
        </div>

        {isHistoryView && viewTask ? (
          <div className="flex items-center gap-2 text-[11px]">
            <span style={{ color: 'var(--secondary)' }}>历史查看:</span>
            <span className="font-semibold" style={{ color: 'var(--ink)' }}>{viewTask.name}</span>
            <span className="text-[9px] px-1.5 py-px rounded-full font-medium"
              style={{ background: 'rgba(234,179,8,0.12)', color: '#b45309' }}>
              历史模式
            </span>
          </div>
        ) : activeTask ? (
          <div className="flex items-center gap-2 text-[11px]">
            <span style={{ color: 'var(--secondary)' }}>任务:</span>
            <span className="font-semibold" style={{ color: 'var(--ink)' }}>{activeTask.name}</span>
            <span className="text-[9px] px-1.5 py-px rounded-full font-medium"
              style={{ background: 'rgba(6,78,59,0.08)', color: 'var(--primaryContainer)' }}>
              {STATUS_LABEL[activeTask.status]}
            </span>
          </div>
        ) : null}

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

      <div className="flex items-center gap-2">
        <button
          className="px-3 py-1 rounded-lg text-[11px] font-medium border cursor-pointer transition-colors
                     disabled:opacity-40 disabled:cursor-not-allowed"
          style={{ borderColor: 'var(--hairline)', background: '#fff', color: 'var(--onSurfaceVariant)' }}
          disabled={settingsDisabled}
          onClick={onOpenRadarSettings}
          title={settingsDisabled
            ? '当前任务或连接状态不可编辑雷达参数'
            : '编辑当前任务的雷达控制参数'}
        >雷达参数</button>
        <button
          className="px-3.5 py-1 rounded-lg text-white font-semibold text-[11px] border-none cursor-pointer
                     precision-shadow active:scale-95 transition-all disabled:opacity-40 disabled:cursor-not-allowed"
          style={{ background: btnBg }}
          disabled={!connected || !activeTask || isHistoryView || commandPending || isFault}
          onClick={toggleAcquisition}
          title={!connected ? '后端未连接'
            : !activeTask ? '请先激活任务'
            : isHistoryView ? '历史查看时不能启停当前采集'
            : isFault ? '设备故障复位尚未实现'
            : acquisition ? '停止当前任务的本地采集' : '开始当前任务的本地采集'}
        >
          {commandPending ? '处理中…' : btnLabel}
        </button>
      </div>

      {hasHints && (
        <div className="absolute top-full right-4 mt-1 flex flex-col gap-2 z-50">
          {noSelectionHint && (
            <div className="px-3 py-2 rounded-lg text-[11px]" style={hintStyle}>
              请先在设备列表中勾选要控制的雷达
              <button className="ml-2 px-1.5 py-0.5 rounded text-[10px] border-none cursor-pointer"
                style={hintDismissStyle}
                onClick={dismissNoSelectionHint}
              >×</button>
            </div>
          )}
          {commandError && (
            <div className="px-3 py-2 rounded-lg text-[11px]" style={hintStyle}>
              ⚠ {commandError}
              <button className="ml-2 px-1.5 py-0.5 rounded text-[10px] border-none cursor-pointer"
                style={hintDismissStyle}
                onClick={dismissCommandError}
              >×</button>
            </div>
          )}
          {interactionError && (
            <div className="px-3 py-2 rounded-lg text-[11px]" style={hintStyle}>
              ⚠ {interactionError}
              <button className="ml-2 px-1.5 py-0.5 rounded text-[10px] border-none cursor-pointer"
                style={hintDismissStyle}
                onClick={dismissInteractionError}
              >×</button>
            </div>
          )}
        </div>
      )}
    </header>
  )
}
