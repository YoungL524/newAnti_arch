/**
 * @file StatusBar.tsx
 * @brief 显示核心服务连接、WebSocket 数据流和当前时间等运行状态。
 * @details 状态栏消费 CoreProvider 的系统最后快照与连接状态，不参与重连或航迹缓存更新。
 */

import { useState, useEffect } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import HeartbeatIndicators from './HeartbeatIndicators'
import type { Task } from '../../lib/taskTypes'

/**
 * @brief 生成状态栏所需的本地时钟文本。
 * @return 采用 24 小时制的时分秒字符串；组件卸载时释放唯一计时器。
 */
function useClock() {
  const [time, setTime] = useState(new Date())
  useEffect(() => {
    const id = setInterval(() => setTime(new Date()), 1000)
    return () => clearInterval(id)
  }, [])
  return time.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

/**
 * @brief 汇总展示进程就绪状态、设备心跳、航迹显示计数和本地时间。
 * @param activeTask 当前活动任务；为空时不显示与任何设备无关的心跳灯。
 * @return 只读页脚；设备心跳事实仍由后端 DeviceState 唯一持有和判定。
 */
export default function StatusBar({ activeTask }: { activeTask: Task | null }) {
  const clock = useClock()
  const { trackRate, acquisition, connected, systemState } = useCore()
  const system = connected ? systemState?.payload : undefined
  const systemLabel = !connected ? 'Core 未连接' : !system ? '等待系统状态'
    : system.health === 2 ? 'Core 故障' : system.health === 1 ? 'Core 降级'
      : ['Core 启动中', 'Core 就绪', 'Core 停止中', 'Core 已停止', 'Core 故障'][system.phase]
  const systemColor = !system ? 'var(--hairlineStrong)' : system.health === 2 ? '#ef4444'
    : system.health === 1 ? '#f59e0b' : system.phase === 1 ? 'var(--primaryContainer)' : 'var(--hairlineStrong)'
  const systemDetail = system ? [
    (system.degradationFlags & 1) !== 0 ? '归档提交失败' : '',
    (system.degradationFlags & 2) !== 0 ? '已发生航迹归档缺口' : '',
    (system.degradationFlags & 4) !== 0 ? '已发生事件归档缺口' : '',
  ].filter(Boolean).join('；') || systemLabel : systemLabel
  const activeFaceIndices = activeTask
    ? [0, 1, 2, 3].filter(index => activeTask.deviceIds.some(deviceId =>
        ((activeTask.deviceFaceMasks[deviceId] ?? 0x01) & (1 << index)) !== 0))
    : []

  return (
    <footer
      className="h-6 flex items-center justify-between px-4 shrink-0 select-none z-20 text-[10px]"
      style={{
        background: 'var(--surfaceLow)',
        borderTop: '1px solid rgba(191, 201, 195, 0.3)',
        color: 'var(--onSurfaceVariant)',
        fontFamily: "'JetBrains Mono', monospace",
      }}
    >
      <div className="flex items-center gap-2" title={systemDetail}>
        <span className="w-1.5 h-1.5 rounded-full"
          style={{ background: systemColor }} />
        <span>DCMP v2.4.0 · {systemLabel}</span>
      </div>
      <div className="flex gap-4 items-center">
        {activeTask && <HeartbeatIndicators faceIndices={activeFaceIndices} />}
        {acquisition && (
          <span className="flex items-center gap-1.5">
            <span>显示点数 {trackRate}</span>
            <span className="w-1.5 h-1.5 rounded-full"
              style={{ background: trackRate > 0 ? '#22c55e' : 'var(--hairlineStrong)' }} />
          </span>
        )}
        <span>{clock}</span>
      </div>
    </footer>
  )
}
