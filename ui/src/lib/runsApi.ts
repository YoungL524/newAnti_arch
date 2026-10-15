/**
 * @file runsApi.ts
 * @brief 从归档 HTTP 读取运行记录，校验后直接交付窗口当前页。
 * @details 从 fetchRunPage 阅读；不查询进程存活，不修改退出标记，不缓存历史页。
 */

/** @brief 数据库运行事实的最终展示记录；异常检测时刻与真正退出时刻严格区分。 */
export interface ArchivedRun {
  runId: string ///< 正 int64 十进制数据库主键。
  runUuid: string ///< 稳定运行 UUID，不是任务或采集身份。
  startedUtcMs: number ///< 启动 UTC 毫秒。
  endedUtcMs?: number ///< 正常退出 UTC 毫秒，缺失不能补造。
  abnormalDetectedUtcMs?: number ///< 下次启动检测到异常的 UTC 毫秒，不是退出时刻。
  applicationVersion: string ///< 启动时程序版本。
  schemaVersion: number ///< 启动时数据库结构版本。
  processId?: string ///< 可空进程号，保留十进制精度，不用其推断当前存活。
  shutdownState: 0 | 1 | 2 ///< 未标记退出/正常退出/检测到异常。
  exitCode?: number ///< 可空退出码，不据状态补零。
}

/** @brief 验证运行事实字段及互斥退出时间；损坏记录不能伪装成正常运行。 */
function validRun(value: unknown): value is ArchivedRun {
  if (!value || typeof value !== 'object') return false
  const r = value as Partial<ArchivedRun>
  const positiveInt64 = (v: unknown): boolean => typeof v === 'string' && /^[1-9]\d{0,18}$/.test(v)
    && BigInt(v) <= 9223372036854775807n
  const time = (v: unknown): boolean => typeof v === 'number' && Number.isSafeInteger(v) && v > 0
  if (!positiveInt64(r.runId) || typeof r.runUuid !== 'string'
    || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(r.runUuid)
    || r.runUuid === '00000000-0000-0000-0000-000000000000' || !time(r.startedUtcMs)
    || typeof r.applicationVersion !== 'string' || !r.applicationVersion
    || !Number.isSafeInteger(r.schemaVersion) || (r.schemaVersion ?? 0) <= 0
    || (r.processId !== undefined && !positiveInt64(r.processId))
    || (r.exitCode !== undefined && !Number.isSafeInteger(r.exitCode))) return false
  if (r.shutdownState === 0) return r.endedUtcMs === undefined && r.abnormalDetectedUtcMs === undefined
  if (r.shutdownState === 1) return time(r.endedUtcMs) && r.endedUtcMs! >= r.startedUtcMs!
    && r.abnormalDetectedUtcMs === undefined
  return r.shutdownState === 2 && r.endedUtcMs === undefined && time(r.abnormalDetectedUtcMs)
    && r.abnormalDetectedUtcMs! >= r.startedUtcMs!
}

/**
 * @brief 只读一页运行记录；取消由窗口拥有，非法字段、条件或顺序抛错。
 * @param filters 启动 UTC 毫秒闭区间及精确退出标记，省略为全部；续页保持原条件。
 * @param cursor 可选更早游标，不是 Topic cursor。
 * @param signal 关闭、断线或替换查询时取消，不影响后端生命周期。
 * @return 最多 200 条最终记录与可选后续游标，不自动追页。
 */
export async function fetchRunPage(filters: { startUtcMs?: number; endUtcMs?: number; shutdownState?: number },
  cursor: string | undefined, signal: AbortSignal): Promise<{ runs: ArchivedRun[]; nextCursor?: string }> {
  const query = new URLSearchParams({ limit: '200' })
  for (const [key, value] of Object.entries(filters)) if (value !== undefined) query.set(key, String(value))
  if (cursor) query.set('cursor', cursor)
  const response = await fetch(`/api/v1/archive/runs?${query}`, { signal })
  const body = await response.json()
  if (!response.ok || body?.ok !== true) throw new Error(body?.error?.message || '运行记录读取失败')
  const data = body.data
  if (!data || !Array.isArray(data.runs) || data.runs.length > 200 || !data.runs.every(validRun)
    || (data.nextCursor !== undefined && (typeof data.nextCursor !== 'string'
      || !/^[A-Za-z0-9_-]{1,512}$/.test(data.nextCursor)))) throw new Error('运行记录响应格式无效')
  const runs = data.runs as ArchivedRun[]
  for (let i = 0; i < runs.length; ++i) {
    const run = runs[i], previous = runs[i - 1]
    if (run.startedUtcMs < (filters.startUtcMs ?? 1) || run.startedUtcMs > (filters.endUtcMs ?? Number.MAX_SAFE_INTEGER)
      || (filters.shutdownState !== undefined && run.shutdownState !== filters.shutdownState)
      || (previous && (run.startedUtcMs > previous.startedUtcMs
        || (run.startedUtcMs === previous.startedUtcMs && BigInt(run.runId) >= BigInt(previous.runId)))))
      throw new Error('运行记录筛选条件或页顺序无效')
  }
  return { runs, ...(data.nextCursor ? { nextCursor: data.nextCursor } : {}) }
}
