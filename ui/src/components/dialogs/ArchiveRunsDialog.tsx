/**
 * @file ArchiveRunsDialog.tsx
 * @brief 展示运行记录查询窗口，上游为只读 runs HTTP，下游为当前最多 200 行。
 * @details 入口为组件；窗口拥有查询条件、当前页及取消信号，不修改系统、任务、采集或回放状态。
 */
import { useEffect, useState } from 'react'
import { useCore } from '../../hooks/useCoreApi'
import { fetchRunPage, type ArchivedRun } from '../../lib/runsApi'

/** @brief 格式化数据库已有 UTC 毫秒，缺失时保留空值，不拿异常检测时间填退出时间。 */
function timeText(value?: number): string {
  return value === undefined ? '—' : `${new Date(value).toLocaleString('zh-CN', { hour12: false })}.${String(value % 1000).padStart(3, '0')}`
}

/** @brief 打开查最新运行页；条件变化、断线、卸载均取消旧请求，翻页不累积记录。 */
export default function ArchiveRunsDialog({ onClose }: { onClose: () => void }) {
  const { connected } = useCore()
  const [start, setStart] = useState('')
  const [end, setEnd] = useState('')
  const [state, setState] = useState('')
  const [request, setRequest] = useState<{ filters: Parameters<typeof fetchRunPage>[0]; cursor?: string }>({ filters: {} })
  const [page, setPage] = useState<{ runs: ArchivedRun[]; nextCursor?: string }>({ runs: [] })
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  /** @brief 取消后所有完成分支禁止回写；断线清页，不把旧运行事实当成本次连接状态。 */
  useEffect(() => {
    setPage({ runs: [] })
    if (!connected) { setLoading(false); setError('后端未连接，无法读取运行记录'); return }
    const controller = new AbortController()
    setLoading(true); setError('')
    fetchRunPage(request.filters, request.cursor, controller.signal)
      .then(result => { if (!controller.signal.aborted) setPage(result) })
      .catch(reason => { if (!controller.signal.aborted) setError(reason instanceof Error ? reason.message : '运行记录读取失败') })
      .finally(() => { if (!controller.signal.aborted) setLoading(false) })
    return () => controller.abort()
  }, [connected, request])

  /** @brief 提交时才把本地时间转 UTC，拒绝非法区间；新条件始终从最新页开始。 */
  const query = () => {
    const first = start ? new Date(start).getTime() : undefined, last = end ? new Date(end).getTime() : undefined
    if ((first !== undefined && (!Number.isSafeInteger(first) || first <= 0))
      || (last !== undefined && (!Number.isSafeInteger(last) || last <= 0))
      || (first !== undefined && last !== undefined && last < first)) {
      setError('起止时间无效或结束时间早于开始时间'); return
    }
    setRequest({ filters: { ...(first !== undefined ? { startUtcMs: first } : {}),
      ...(last !== undefined ? { endUtcMs: last } : {}), ...(state ? { shutdownState: Number(state) } : {}) } })
  }

  return <div className="fixed inset-0 z-[100] bg-black/30 flex items-center justify-center p-5">
    <section role="dialog" aria-modal="true" aria-labelledby="archive-runs-title"
      className="bg-white rounded-xl shadow-xl w-full max-w-6xl flex flex-col max-h-[85vh] p-5 gap-3">
      <div className="flex items-center justify-between">
        <h2 id="archive-runs-title" className="text-base font-semibold">程序运行记录</h2>
        <button onClick={onClose} className="cursor-pointer" autoFocus>关闭</button>
      </div>
      <form className="flex flex-wrap gap-2 text-xs items-center" onSubmit={e => { e.preventDefault(); query() }}>
        <label>启动起点（本地）<input className="border rounded p-1" type="datetime-local" step="0.001" value={start} onChange={e => setStart(e.target.value)} /></label>
        <label>启动终点（本地）<input className="border rounded p-1" type="datetime-local" step="0.001" value={end} onChange={e => setEnd(e.target.value)} /></label>
        <label>退出标记 <select className="border rounded p-1" value={state} onChange={e => setState(e.target.value)}>
          <option value="">全部</option><option value="0">未标记退出</option><option value="1">正常退出</option><option value="2">检测到异常</option>
        </select></label>
        <button disabled={!connected} className="border rounded p-1 cursor-pointer disabled:opacity-40">查询</button>
        <button type="button" className="border rounded p-1 cursor-pointer" onClick={() => {
          setStart(''); setEnd(''); setState(''); setRequest({ filters: {} })
        }}>重置</button>
      </form>
      <p className="text-xs text-stone-500">未标记退出不代表进程仍存活；异常检测时间是下次启动发现异常的时间。</p>
      <div className="flex gap-3 text-xs items-center">
        <button disabled={!connected || loading} className="cursor-pointer disabled:opacity-40" onClick={() => setRequest({ filters: request.filters })}>最新一页</button>
        <button disabled={!connected || loading || !page.nextCursor} className="cursor-pointer disabled:opacity-40"
          onClick={() => setRequest({ filters: request.filters, cursor: page.nextCursor })}>更早一页</button>
        <span>{loading ? '正在读取…' : `当前页 ${page.runs.length} 条`}</span>
        {error && <span role="alert" className="text-red-700">{error}</span>}
      </div>
      <div className="overflow-auto min-h-0 text-xs"><table className="w-full text-left"><thead><tr>
        <th>运行 UUID</th><th>启动时间</th><th>正常退出时间</th><th>异常检测时间</th><th>退出标记</th><th>程序/结构版本</th><th>进程号</th><th>退出码</th>
      </tr></thead><tbody>{page.runs.map(run => <tr key={run.runId} className="border-t">
        <td className="p-1 break-all">{run.runUuid}</td><td className="p-1">{timeText(run.startedUtcMs)}</td>
        <td className="p-1">{timeText(run.endedUtcMs)}</td><td className="p-1">{timeText(run.abnormalDetectedUtcMs)}</td>
        <td className="p-1">{['未标记退出', '正常退出', '检测到异常'][run.shutdownState]}</td>
        <td className="p-1">{run.applicationVersion} / {run.schemaVersion}</td><td className="p-1">{run.processId ?? '—'}</td><td className="p-1">{run.exitCode ?? '—'}</td>
      </tr>)}</tbody></table>
        {!loading && !error && page.runs.length === 0 && <p className="p-4">没有匹配的运行记录</p>}
      </div>
    </section>
  </div>
}
