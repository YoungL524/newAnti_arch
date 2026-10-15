/**
 * @file historyApi.ts
 * @brief 从归档 HTTP 读取全局/任务事件与航迹，直接形成最终日志行及地理绘制点。
 * @details 上游是任务/时间/游标，下游是最多 200 条日志或 1000 条回放点；不读实时 Topic，不累积全量历史。
 * 从两个 fetchTask*Page 阅读取消及输入校验；未知事件保留稳定码，历史航迹不套用最新安装姿态。
 */
import type { LogEntry, TrackHistoryPoint } from './protocol'
import { projectRadarObservation } from './radarPose'

/** @brief 归档边界的短期原始事件；ID/参数为十进制字符串，避免 int64 精度损失。 */
interface ArchivedEvent {
  eventId: string ///< 数据库主键，不是 Topic sequence。
  timestamp: number ///< Unix epoch UTC 毫秒。
  eventCode: number ///< 已归档稳定码，未知码仍可读取。
  severity: number ///< Info/Warning/Error/Fault/Alarm 的 0～4。
  argument0?: string ///< 按事件码解释的有符号 int64 参数。
  argument1?: string ///< 按事件码解释的有符号 int64 参数。
  deviceId?: string ///< 可选稳定设备 UUID，不是当前进程句柄。
  taskId?: string ///< 实际归档关联任务；系统级事件可没有任务。
  domain: number ///< 事件所属域 1～4，不用于路由命令。
}

/** @brief 归档只读筛选输入；省略字段表示全部，时间为 Unix epoch UTC 毫秒闭区间。 */
export interface ArchiveEventFilters {
  taskId?: string ///< 可选稳定任务 UUID。
  deviceId?: string ///< 可选稳定设备 UUID，包含其历史安装 revision。
  domain?: number ///< 设备/航迹/系统/控制分别为 1～4。
  severity?: number ///< 精确级别 Info/Warning/Error/Fault/Alarm 为 0～4。
  startUtcMs?: number ///< 可选正安全整数 UTC 起点。
  endUtcMs?: number ///< 可选不早于起点的 UTC 终点。
}

/** @brief 按归档原始稳定码生成只读文案，参数保留原值；不据此修改任何当前运行状态。 */
function eventText(event: ArchivedEvent): string {
  const a = event.argument0, b = event.argument1
  switch (event.eventCode) {
    case 0x01010001: return `无效设备报文，当前原因 ${a ?? '未知'}，窗口拒绝 ${b ?? '未记录'} 条`
    case 0x01010002: return `${a === '1' ? 'DP' : 'SP'} 心跳${['超时', '在线', '离线或迟到'][Math.floor(Number(b) / 16)] ?? '状态变化'}`
    case 0x02010001: return `航迹校验拒绝，当前原因 ${a ?? '未知'}，窗口拒绝 ${b ?? '未记录'} 条`
    case 0x03010001: return `历史序号缺口：${a} 至 ${b}`
    case 0x03020001: return (Number(b) & 1) !== 0 ? '归档提交失败，系统降级' : '归档提交可用，历史缺口以系统状态为准'
    case 0x03020002: return `航迹首次归档缺口：${a} 至 ${b}`
    case 0x03020003: return `事件首次归档缺口：${a} 至 ${b}`
    case 0x04010002: return '控制报文未能提交本地 UDP'
    case 0x04010003: return ({ '1': '任务已创建', '2': '任务已激活', '3': '任务已完成', '4': '任务已归档' } as Record<string, string>)[a ?? ''] ?? '任务生命周期变化'
    case 0x04010004: return '任务控制参数保存失败'
    case 0x04010005: return '任务参数已保存，运行控制快照更新失败'
    case 0x04010006: return a === '1' ? '本地采集已开始' : '本地采集已停止'
    case 0x04010007: return '控制报文未能提交本地 UDP'
    case 0x04010008: return '周期控制报文本地提交已恢复'
    default: return `归档事件 0x${event.eventCode.toString(16).padStart(8, '0')}`
  }
}

/** @brief 验证数据库边界 JSON 的整数、字符串和容量合同，非法记录不伪装为空页。 */
function validEvent(value: unknown): value is ArchivedEvent {
  if (!value || typeof value !== 'object') return false
  const e = value as Partial<ArchivedEvent>
  const int64 = (v: unknown): boolean => typeof v === 'string' && /^-?\d{1,19}$/.test(v)
    && BigInt(v) >= -9223372036854775808n && BigInt(v) <= 9223372036854775807n
  return int64(e.eventId) && BigInt(e.eventId!) > 0n
    && Number.isSafeInteger(e.timestamp) && (e.timestamp ?? 0) > 0
    && Number.isInteger(e.eventCode) && (e.eventCode ?? -1) >= 0 && (e.eventCode ?? Infinity) <= 0xffffffff
    && Number.isInteger(e.severity) && (e.severity ?? -1) >= 0 && (e.severity ?? Infinity) <= 4
    && Number.isInteger(e.domain) && (e.domain ?? 0) >= 1 && (e.domain ?? Infinity) <= 4
    && (e.argument0 === undefined || int64(e.argument0)) && (e.argument1 === undefined || int64(e.argument1))
    && (e.deviceId === undefined || (typeof e.deviceId === 'string'
      && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(e.deviceId)
      && e.deviceId !== '00000000-0000-0000-0000-000000000000'))
    && (e.taskId === undefined || (typeof e.taskId === 'string'
      && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(e.taskId)
      && e.taskId !== '00000000-0000-0000-0000-000000000000'))
}

/**
 * @brief 读取最新或更早一页日志；由调用方取消过期请求，失败抛出供页面显示。
 * @param taskId 历史任务稳定 UUID。
 * @param cursor 上页返回的游标；省略表示最新一页。
 * @param signal 调用方持有的取消信号，不取消后端归档或任务命令。
 * @return 当前最多 200 条日志和可选下一页游标，不自动追页或保留历史缓存。
 */
export async function fetchTaskLogPage(taskId: string, cursor: string | undefined, signal: AbortSignal): Promise<{ logs: LogEntry[]; nextCursor?: string }> {
  return fetchArchivedEventPage({ taskId }, cursor, signal)
}

/**
 * @brief 复用事件接口读取全局或筛选后一页；结果仅为最终日志行，不累积历史。
 * @param filters 本轮已提交条件，续页必须原样复用。
 * @param cursor 上页游标；省略读取最新页。
 * @param signal 窗口或任务页 owner 的取消信号，取消不影响后端归档。
 * @return 最多 200 条只读日志和后续游标；网络、参数或格式失败抛错。
 */
export async function fetchArchivedEventPage(filters: ArchiveEventFilters, cursor: string | undefined,
  signal: AbortSignal): Promise<{ logs: LogEntry[]; nextCursor?: string }> {
  const query = new URLSearchParams({ limit: '200' })
  for (const [key, value] of Object.entries(filters)) if (value !== undefined) query.set(key, String(value))
  if (cursor) query.set('cursor', cursor)
  const response = await fetch(`/api/v1/archive/events?${query}`, { signal })
  const body = await response.json()
  if (!response.ok || body?.ok !== true) throw new Error(body?.error?.message || '历史日志读取失败')
  const data = body.data
  if (!data || !Array.isArray(data.events) || data.events.length > 200 || !data.events.every(validEvent)
    || (data.nextCursor !== undefined && (typeof data.nextCursor !== 'string'
      || !/^[A-Za-z0-9_-]{1,512}$/.test(data.nextCursor)))) throw new Error('历史日志响应格式无效')
  const events = data.events as ArchivedEvent[]
  for (let i = 0; i < events.length; ++i) {
    const event = events[i], previous = events[i - 1]
    if ((filters.taskId !== undefined && event.taskId !== filters.taskId.toLowerCase())
      || (filters.deviceId !== undefined && event.deviceId !== filters.deviceId.toLowerCase())
      || (filters.domain !== undefined && event.domain !== filters.domain)
      || (filters.severity !== undefined && event.severity !== filters.severity)
      || event.timestamp < (filters.startUtcMs ?? 1) || event.timestamp > (filters.endUtcMs ?? Number.MAX_SAFE_INTEGER)
      || (previous && (event.timestamp > previous.timestamp
        || (event.timestamp === previous.timestamp && BigInt(event.eventId) >= BigInt(previous.eventId)))))
      throw new Error('归档事件条件或页顺序无效')
  }
  return {
    logs: events.map(event => ({
      timestamp: event.timestamp, ...(event.taskId ? { taskId: event.taskId } : {}),
      ...(event.deviceId ? { deviceId: event.deviceId } : {}),
      level: event.severity === 0 ? 'INFO' : event.severity === 1 ? 'WARN' : 'ERROR',
      message: `${event.severity === 3 ? '故障：' : event.severity === 4 ? '告警：' : ''}${eventText(event)}`,
    })),
    ...(data.nextCursor ? { nextCursor: data.nextCursor } : {}),
  }
}

/**
 * @brief 读取一个升序航迹页，校验实际归档字段后直接形成最终绘制点，原始网络 pose 不长期驻留。
 * @param taskId 稳定任务 UUID。
 * @param startUtcMs 可选 UTC 毫秒闭区间起点。
 * @param endUtcMs 可选 UTC 毫秒闭区间终点。
 * @param cursor 同一任务/时间查询的后续位置。
 * @param signal 查询取消信号，不影响归档或设备。
 * @return 最多 1000 条最终绘制点及可选下一页游标；非法数据抛错，不丢字段后猜测绘制。
 */
export async function fetchTaskTrackPage(taskId: string, startUtcMs: number | undefined,
  endUtcMs: number | undefined, cursor: string | undefined, signal: AbortSignal): Promise<{ points: TrackHistoryPoint[]; nextCursor?: string }> {
  const query = new URLSearchParams({ taskId, limit: '1000' })
  if (startUtcMs !== undefined) query.set('startUtcMs', String(startUtcMs))
  if (endUtcMs !== undefined) query.set('endUtcMs', String(endUtcMs))
  if (cursor) query.set('cursor', cursor)
  const response = await fetch(`/api/v1/archive/tracks?${query}`, { signal })
  const body = await response.json()
  if (!response.ok || body?.ok !== true) throw new Error(body?.error?.message || '历史航迹读取失败')
  const data = body.data
  if (!data || !Array.isArray(data.tracks) || data.tracks.length > 1000
    || (data.nextCursor !== undefined && (typeof data.nextCursor !== 'string'
      || !/^[A-Za-z0-9_-]{1,512}$/.test(data.nextCursor)))) throw new Error('历史航迹响应格式无效')
  const uuid = (v: unknown): boolean => typeof v === 'string'
    && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(v)
    && v !== '00000000-0000-0000-0000-000000000000'
  const points: TrackHistoryPoint[] = data.tracks.map((row: any) => {
    const pose = row?.pose
    if (!row || !pose || !uuid(row.deviceId) || !uuid(row.acquisitionId)
      || typeof row.observationId !== 'string' || !/^[1-9]\d{0,18}$/.test(row.observationId)
      || BigInt(row.observationId) > 9223372036854775807n
      || !Number.isSafeInteger(row.tsMs) || row.tsMs <= 0
      || !Number.isInteger(row.targetId) || row.targetId < 0 || row.targetId > 0xffffffff
      || !Number.isInteger(row.faceIndex) || row.faceIndex < 0 || row.faceIndex > 3
      || !Number.isFinite(row.range) || row.range < 0
      || !Number.isFinite(row.azimuth) || Math.abs(row.azimuth) > Math.PI / 4
      || !Number.isFinite(row.elevation) || row.elevation < 0 || row.elevation > Math.PI / 4
      || !Number.isFinite(row.radialSpeed) || !Number.isFinite(row.amplitude) || (row.packFlag !== 0 && row.packFlag !== 1)
      || !Number.isFinite(pose.lng) || Math.abs(pose.lng) > 180 || !Number.isFinite(pose.lat) || Math.abs(pose.lat) >= 90
      || !Number.isFinite(pose.altitude) || !Number.isFinite(pose.heading) || Math.abs(pose.heading) > 360
      || !Number.isFinite(pose.pitch) || Math.abs(pose.pitch) > 90 || !Number.isFinite(pose.roll) || Math.abs(pose.roll) > 180
      || (row.scanSequence !== undefined && (!Number.isInteger(row.scanSequence) || row.scanSequence < 0 || row.scanSequence > 0xffffffff)))
      throw new Error('归档航迹或采集时安装配置无效')
    const position = projectRadarObservation(pose, pose, row.azimuth * 180 / Math.PI, row.elevation * 180 / Math.PI, row.range)
    if (![position.lng, position.lat, position.altitude].every(Number.isFinite)) throw new Error('归档航迹无法转换为地理位置')
    return {
      taskId, deviceId: row.deviceId, acquisitionId: row.acquisitionId, observationId: row.observationId,
      tsMs: row.tsMs, targetId: row.targetId, faceIndex: row.faceIndex, range: row.range,
      azimuth: row.azimuth, elevation: row.elevation, radialSpeed: row.radialSpeed,
      amplitude: row.amplitude, packFlag: row.packFlag, ...(row.scanSequence !== undefined ? { scanSequence: row.scanSequence } : {}),
      longitude: position.lng, latitude: position.lat, altitude: position.altitude,
    }
  })
  for (let i = 1; i < points.length; ++i) {
    if (points[i].tsMs < points[i - 1].tsMs || (points[i].tsMs === points[i - 1].tsMs
      && BigInt(points[i].observationId) <= BigInt(points[i - 1].observationId))) throw new Error('归档航迹页顺序无效')
  }
  return { points, ...(data.nextCursor ? { nextCursor: data.nextCursor } : {}) }
}
