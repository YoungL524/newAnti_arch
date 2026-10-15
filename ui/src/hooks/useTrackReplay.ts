/**
 * @file useTrackReplay.ts
 * @brief 拥有历史航迹的一页查询与回放时钟，输出地图/表格共用的可见记录。
 * @details 上游为历史任务与连接状态，下游为当前页时间窗口；不发送设备命令、不写实时缓存、不保存跨页历史。
 * 从查询 effect 与播放 effect 阅读；请求切换取消旧响应，单页最多 1000 条，加载下一页时冻结时钟。
 */
import { useCallback, useEffect, useMemo, useState } from 'react'
import { fetchTaskTrackPage } from '../lib/historyApi'
import type { TrackHistoryPoint } from '../lib/protocol'

/**
 * @brief 只读回放一个历史任务，按接收 UTC 的 1 倍速推进，单调时钟只用于计算播放经过时间。
 * @param taskId 历史任务稳定 UUID；null 表示退出回放，立即隐藏并释放旧页。
 * @param connected Core 已连接；断线暂停并取消查询，不让旧响应覆盖重连结果。
 * @return 当前可见点、页边界、播放位置和显式控制方法；points 为当前页派生视图，无全量历史缓存。
 */
export function useTrackReplay(taskId: string | null, connected: boolean) {
  const [request, setRequest] = useState<{ taskId: string; start?: number; end?: number; cursor?: string; keepClock?: boolean }>({ taskId: '' })
  const [page, setPage] = useState<{ taskId: string; points: TrackHistoryPoint[]; nextCursor?: string }>({ taskId: '', points: [] })
  const [position, setPosition] = useState(0)
  const [playing, setPlaying] = useState(false)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const current = page.taskId === taskId ? page.points : []
  const first = current[0]?.tsMs ?? 0
  const last = current[current.length - 1]?.tsMs ?? 0

  /** @brief 切任务或退出即撤销旧范围/游标及播放位置；重新进入同一任务也从首个查询开始。 */
  useEffect(() => {
    setPlaying(false); setLoading(false); setError(''); setPosition(0)
    setPage({ taskId: taskId ?? '', points: [] }); setRequest({ taskId: taskId ?? '' })
  }, [taskId])

  /** @brief 仅保留当前页；查询条件变化或退出时取消旧请求，继续页才允许保留旧播放位置。 */
  useEffect(() => {
    if (!taskId) {
      setLoading(false)
      return
    }
    if (request.taskId !== taskId) return
    if (!connected) { setPlaying(false); setLoading(false); setError('后端未连接，回放已暂停'); return }
    const abort = new AbortController()
    const query = request
    if (!query.cursor) { setPlaying(false); setPage({ taskId, points: [] }); setPosition(0) }
    setLoading(true); setError('')
    fetchTaskTrackPage(taskId, query.start, query.end, query.cursor, abort.signal)
      .then(result => {
        if (abort.signal.aborted) return
        setPage({ taskId, ...result })
        setPosition(previous => query.keepClock && previous > 0 && result.points.length > 0 ? previous : result.points[0]?.tsMs ?? 0)
        if (result.points.length === 0) setPlaying(false)
      })
      .catch(reason => {
        if (!abort.signal.aborted) { setPlaying(false); setError(reason instanceof Error ? reason.message : '历史航迹读取失败') }
      })
      .finally(() => { if (!abort.signal.aborted) setLoading(false) })
    return () => abort.abort()
  }, [taskId, connected, request])

  /** @brief 空闲/暂停/加载时不启动时钟；页尾钳位后由下一页读取决定何时继续，保留跨页时间间隔。 */
  useEffect(() => {
    if (!taskId || !connected || !playing || loading || !last) return
    let previousTick = performance.now()
    const timer = window.setInterval(() => {
      const now = performance.now()
      const elapsed = Math.max(0, now - previousTick)
      previousTick = now
      setPosition(value => Math.min(last, value + elapsed))
    }, 50)
    return () => window.clearInterval(timer)
  }, [taskId, connected, playing, loading, last])

  /** @brief 页尾顺序读取下一页，替换旧页；末页停住，不补发命令或自动从头循环。 */
  useEffect(() => {
    if (!playing || loading || !taskId || page.taskId !== taskId || !last || position < last) return
    if (!page.nextCursor) { setPlaying(false); return }
    setRequest(previous => ({ ...(previous.taskId === taskId ? previous : {}), taskId, cursor: page.nextCursor, keepClock: true }))
  }, [playing, loading, taskId, page.taskId, page.nextCursor, last, position])

  /** @brief 显式查询 UTC 闭区间并暂停，省略时间表示全范围；非法范围只报错，不发送请求。 */
  const queryTimeRange = useCallback((start?: number, end?: number) => {
    if (!taskId) return
    setPlaying(false)
    if ((start !== undefined && (!Number.isSafeInteger(start) || start <= 0))
      || (end !== undefined && (!Number.isSafeInteger(end) || end <= 0))
      || (start !== undefined && end !== undefined && end < start)) { setError('回放时间范围无效'); return }
    setPlaying(false); setRequest({ taskId, start, end })
  }, [taskId])

  /** @brief 暂停或从当前位置播放；已经在末页页尾时重新播放本页，不请求整任务历史。 */
  const togglePlaying = useCallback(() => {
    if (playing) { setPlaying(false); return }
    if (!connected || loading || !current.length || error) return
    if (!playing && position >= last && !page.nextCursor) setPosition(first)
    setPlaying(value => !value)
  }, [connected, loading, current.length, error, playing, position, last, page.nextCursor, first])

  /** @brief 手动下一页从其首条开始，播放状态暂停；只保留一页，不积累已看过的记录。 */
  const nextPage = useCallback(() => {
    if (!taskId || !connected || loading || page.taskId !== taskId || !page.nextCursor) return
    setPlaying(false)
    setRequest(previous => ({ ...(previous.taskId === taskId ? previous : {}), taskId, cursor: page.nextCursor, keepClock: false }))
  }, [taskId, connected, loading, page.taskId, page.nextCursor])

  /** @brief 在当前页内定位并暂停；本页全览同样使用页尾位置，不生成另一个轨迹缓存。 */
  const seekWithinPage = useCallback((utcMs: number) => {
    if (!first || loading || !Number.isFinite(utcMs)) return
    setPlaying(false); setPosition(Math.max(first, Math.min(last, utcMs)))
  }, [first, last, loading])

  const points = useMemo(() => page.taskId === taskId ? page.points.filter(point => point.tsMs <= position) : [], [page, taskId, position])
  return { taskId, points, loading, error, playing: playing && connected, position, first, last,
    pageCount: current.length, hasNext: page.taskId === taskId && !!page.nextCursor,
    queryTimeRange, togglePlaying, nextPage, seekWithinPage }
}
