/**
 * @file taskLogsApi.ts
 * @brief 查询与指定任务关联的结构化日志记录。
 * @details 接口保留服务端时间、级别和设备归属字段，供任务复盘使用，不替代实时日志推送。
 */

import type { LogEntry } from './protocol'

interface TaskLogsResponse {
  ok: boolean
  error?: string
  logs?: LogEntry[]
}

export async function fetchTaskLogs(taskId: string, limit = 500): Promise<TaskLogsResponse> {
  try {
    const query = new URLSearchParams({ taskId, limit: String(limit) })
    const response = await fetch(`/api/task-logs?${query}`)
    if (!response.ok) return { ok: false, error: `HTTP ${response.status}`, logs: [] }
    const data = await response.json() as TaskLogsResponse
    if (!data.ok) return { ok: false, error: data.error || '任务日志加载失败', logs: [] }
    return { ok: true, logs: Array.isArray(data.logs) ? data.logs : [] }
  } catch (error: any) {
    return { ok: false, error: error?.message || '网络错误', logs: [] }
  }
}

