/**
 * @file ArchiveEventsDialog.tsx
 * @brief 从全局事件 HTTP 查询已提交日志，提供任务、设备、域、级别及本地时间筛选。
 * @details 从组件入口阅读；本窗口拥有唯一当前页和取消信号，不读 Topic，不改任务、采集或回放状态。
 */
import { useEffect, useState } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import { fetchArchivedEventPage, type ArchiveEventFilters } from '../../lib/historyApi'
import type { LogEntry } from '../../lib/protocol'

/** @brief 打开即查全局最新页，关闭/断线/替换查询取消旧请求；翻页替换最多 200 行，不累积历史。 */
export default function ArchiveEventsDialog({ onClose }: { onClose: () => void }) {
  const { connected } = useCore()
  const [taskId, setTaskId] = useState('')
  const [deviceId, setDeviceId] = useState('')
  const [domain, setDomain] = useState('')
  const [severity, setSeverity] = useState('')
  const [start, setStart] = useState('')
  const [end, setEnd] = useState('')
  const [request, setRequest] = useState<{ filters: ArchiveEventFilters; cursor?: string }>({ filters: {} })
  const [page, setPage] = useState<{ logs: LogEntry[]; nextCursor?: string }>({ logs: [] })
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  /** @brief 只保存当前请求结果；断线清页，旧响应在取消后不能覆盖新条件。 */
  useEffect(() => {
    setPage({ logs: [] })
    if (!connected) { setLoading(false); setError('后端未连接，无法读取归档事件'); return }
    const controller = new AbortController()
    setLoading(true)
    setError('')
    fetchArchivedEventPage(request.filters, request.cursor, controller.signal)
      .then(result => { if (!controller.signal.aborted) setPage(result) })
      .catch(reason => { if (!controller.signal.aborted) setError(reason instanceof Error ? reason.message : '归档事件读取失败') })
      .finally(() => { if (!controller.signal.aborted) setLoading(false) })
    return () => controller.abort()
  }, [connected, request])

  /** @brief 本地时间输入只在提交时转 UTC；非法区间不发送，合法条件始终从最新页开始。 */
  const query = () => {
    const first = start ? new Date(start).getTime() : undefined
    const last = end ? new Date(end).getTime() : undefined
    if ((first !== undefined && (!Number.isSafeInteger(first) || first <= 0))
      || (last !== undefined && (!Number.isSafeInteger(last) || last <= 0))
      || (first !== undefined && last !== undefined && last < first)) {
      setError('请输入有效的起止时间，结束时间不能早于开始时间')
      return
    }
    setRequest({ filters: {
      ...(taskId.trim() ? { taskId: taskId.trim() } : {}),
      ...(deviceId.trim() ? { deviceId: deviceId.trim() } : {}),
      ...(domain ? { domain: Number(domain) } : {}), ...(severity ? { severity: Number(severity) } : {}),
      ...(first !== undefined ? { startUtcMs: first } : {}), ...(last !== undefined ? { endUtcMs: last } : {}),
    } })
  }

  return <div className="fixed inset-0 z-[100] bg-black/30 flex items-center justify-center p-5">
    <section role="dialog" aria-modal="true" aria-labelledby="archive-events-title"
      className="bg-white rounded-xl shadow-xl w-full max-w-5xl flex flex-col max-h-[85vh] p-5 gap-3">
      <div className="flex items-center justify-between">
        <h2 id="archive-events-title" className="text-base font-semibold">系统归档日志</h2>
        <button onClick={onClose} className="cursor-pointer" autoFocus>关闭</button>
      </div>
      <form className="flex flex-wrap gap-2 text-xs items-center" onSubmit={e => { e.preventDefault(); query() }}>
        <label>任务 UUID <input className="border rounded p-1" value={taskId} onChange={e => setTaskId(e.target.value)} placeholder="全部任务及系统事件" /></label>
        <label>设备 UUID <input className="border rounded p-1" value={deviceId} onChange={e => setDeviceId(e.target.value)} placeholder="全部设备" /></label>
        <label>事件域 <select className="border rounded p-1" value={domain} onChange={e => setDomain(e.target.value)}>
          <option value="">全部</option>{['设备', '航迹', '系统', '控制'].map((name, i) => <option key={name} value={i + 1}>{name}</option>)}
        </select></label>
        <label>级别 <select className="border rounded p-1" value={severity} onChange={e => setSeverity(e.target.value)}>
          <option value="">全部</option>{['信息', '警告', '错误', '故障', '告警'].map((name, i) => <option key={name} value={i}>{name}</option>)}
        </select></label>
        <label>开始（本地）<input className="border rounded p-1" type="datetime-local" step="0.001" value={start} onChange={e => setStart(e.target.value)} /></label>
        <label>结束（本地）<input className="border rounded p-1" type="datetime-local" step="0.001" value={end} onChange={e => setEnd(e.target.value)} /></label>
        <button type="submit" disabled={!connected} className="border rounded p-1 cursor-pointer disabled:opacity-40">查询</button>
        <button type="button" className="border rounded p-1 cursor-pointer" onClick={() => {
          setTaskId(''); setDeviceId(''); setDomain(''); setSeverity(''); setStart(''); setEnd(''); setRequest({ filters: {} })
        }}>重置</button>
      </form>
      <div className="flex gap-3 text-xs items-center">
        <button disabled={!connected || loading} className="cursor-pointer disabled:opacity-40"
          onClick={() => setRequest({ filters: request.filters })}>最新一页</button>
        <button disabled={!connected || loading || !page.nextCursor} className="cursor-pointer disabled:opacity-40"
          onClick={() => setRequest({ filters: request.filters, cursor: page.nextCursor })}>更早一页</button>
        <span>{loading ? '正在读取…' : `当前页 ${page.logs.length} 条`}</span>
        {error && <span role="alert" className="text-red-700">{error}</span>}
      </div>
      <div className="overflow-auto min-h-0 text-xs">
        <table className="w-full text-left"><thead><tr>
          <th>本地时间</th><th>级别</th><th>内容</th><th>任务</th><th>设备</th>
        </tr></thead><tbody>{page.logs.map((log, i) => <tr key={i} className="border-t">
          <td className="p-1 whitespace-nowrap">{new Date(log.timestamp).toLocaleString('zh-CN', { hour12: false })}.{String(log.timestamp % 1000).padStart(3, '0')}</td>
          <td className="p-1">{log.level}</td><td className="p-1">{log.message}</td>
          <td className="p-1 break-all">{log.taskId ?? '—'}</td><td className="p-1 break-all">{log.deviceId ?? '—'}</td>
        </tr>)}</tbody></table>
        {!loading && !error && page.logs.length === 0 && <p className="p-4">没有匹配的已提交事件</p>}
      </div>
    </section>
  </div>
}
