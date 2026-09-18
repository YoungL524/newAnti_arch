/**
 * @file HeartbeatIndicators.tsx
 * @brief 在主界面状态栏显示当前任务所选阵面的 DP、SP 心跳状态。
 * @details 组件只消费 CoreProvider 唯一 WebSocket 的最后完整快照；颜色由后端状态和阵面位图直接派生，
 * 不维护计时器、历史或设备控制状态。FP 表示 DP 报告的在线状态，数据表示 SP 报告的数据状态。
 */

import type { HeartbeatStatus } from '../../lib/protocol'
import { useCore } from '../../hooks/useCoreApi'

/** @brief 三态指示灯的显示颜色与文字。 */
const HEARTBEAT_VIEW: Record<HeartbeatStatus, { color: string; label: string }> = {
  unknownOrTimedOut: { color: '#94a3b8', label: '未知/超时' },
  online: { color: '#22c55e', label: '在线' },
  offlineOrLate: { color: '#f59e0b', label: '离线/迟到' },
}

/**
 * @brief 生成一个带文字提示的紧凑状态灯。
 * @param label 状态栏中显示的业务名称。
 * @param status 后端已经判定的三态值。
 * @return 不保存状态的展示节点。
 */
function Indicator({ label, status }: { label: string; status: HeartbeatStatus }) {
  const view = HEARTBEAT_VIEW[status]
  return (
    <span className="flex items-center gap-1" title={`${label}：${view.label}`}>
      <span className="w-1.5 h-1.5 rounded-full" style={{ background: view.color }} />
      <span>{label}</span>
    </span>
  )
}

/**
 * @brief 将父进程状态与阵面位图合成为单个阵面状态。
 * @param parentStatus DP 或 SP 的后端判定状态。
 * @param mask 对应业务的四阵面有效位图，仅使用低四位。
 * @param faceIndex 阵面索引，取值范围为 0～3。
 * @return 父进程未知时为灰、离线或迟到时为黄；仅父进程在线时才采信位图。
 */
function faceStatus(parentStatus: HeartbeatStatus, mask: number, faceIndex: number): HeartbeatStatus {
  if (parentStatus !== 'online') return parentStatus
  return (mask & (1 << faceIndex)) !== 0 ? 'online' : 'offlineOrLate'
}

/**
 * @brief 显示DP/SP和四阵面FP/数据指示灯。
 * @param faceIndices 当前任务实际启用的阵面索引。
 * @return 连接中断时所有设备灯立即转灰的状态栏片段；连接状态仅由顶部统一展示。
 */
export default function HeartbeatIndicators({ faceIndices }: { faceIndices: number[] }) {
  const { connected, deviceHeartbeat } = useCore()
  const dp: HeartbeatStatus = connected && deviceHeartbeat ? deviceHeartbeat.payload.dpHeartbeat : 'unknownOrTimedOut'
  const sp: HeartbeatStatus = connected && deviceHeartbeat ? deviceHeartbeat.payload.spHeartbeat : 'unknownOrTimedOut'

  return (
    <div className="flex items-center gap-3">
      <Indicator label="DP" status={dp} />
      <Indicator label="SP" status={sp} />
      {faceIndices.map(index => {
        const fp = faceStatus(dp, deviceHeartbeat?.payload.fpOnlineMask ?? 0, index)
        const data = faceStatus(sp, deviceHeartbeat?.payload.fpDataMask ?? 0, index)
        return (
          <span key={index} className="flex items-center gap-1.5">
            <span>{index + 1}#</span>
            <Indicator label="FP" status={fp} />
            <Indicator label="数据" status={data} />
          </span>
        )
      })}
    </div>
  )
}
