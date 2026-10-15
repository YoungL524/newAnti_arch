/**
 * @file TrackReplayControls.tsx
 * @brief 显示历史查询范围、当前页定位与只读播放操作。
 * @details 表单只持有本地时间输入，查询/页/时钟由回放控制器拥有，不调用设备命令。
 */
import { useEffect, useState } from 'react'
import { useCore } from '../../hooks/useCoreApi'

/**
 * @brief 提交时间范围或调整当前页位置；每页最多 1000 条，切页不连接旧页轨迹。
 * @return 本地时间输入与播放控件；输入按浏览器时区转换为 UTC 毫秒，组件不保存轨迹数据。
 */
export default function TrackReplayControls() {
  const { replay, connected, isHistoryView } = useCore()
  const [start, setStart] = useState('')
  const [end, setEnd] = useState('')
  useEffect(() => { setStart(''); setEnd('') }, [replay?.taskId])
  if (!isHistoryView || !replay) return null
  const disabled = !connected || replay.loading
  return (
    <div className="absolute bottom-3 left-3 right-3 z-30 rounded-lg bg-white/95 shadow-md p-2 text-[10px] flex flex-col gap-2">
      <div className="flex items-center gap-2 flex-wrap">
        <span>航迹回放 · 1× · 按采集时安装配置定位</span>
        <label>起始（本地时间）<input type="datetime-local" step="0.001" value={start}
          onChange={event => setStart(event.target.value)} aria-label="回放起始时间" /></label>
        <label>结束<input type="datetime-local" step="0.001" value={end}
          onChange={event => setEnd(event.target.value)} aria-label="回放结束时间" /></label>
        <button disabled={disabled} className="cursor-pointer disabled:opacity-40"
          onClick={() => replay.queryTimeRange(start ? new Date(start).getTime() : undefined, end ? new Date(end).getTime() : undefined)}>查询 / 重置</button>
        <button disabled={!connected || (!replay.playing && (replay.loading || !replay.pageCount || !!replay.error))} className="cursor-pointer disabled:opacity-40"
          onClick={replay.togglePlaying}>{replay.playing ? '暂停' : '播放'}</button>
        <button disabled={disabled || !replay.hasNext} className="cursor-pointer disabled:opacity-40" onClick={replay.nextPage}>下一页</button>
        <button disabled={disabled || !replay.pageCount} className="cursor-pointer disabled:opacity-40"
          onClick={() => replay.seekWithinPage(replay.last)}>本页全览</button>
        <span>{replay.loading ? '正在读取归档…' : `当前页 ${replay.pageCount} 条 · 已显示 ${replay.points.length} 条`}</span>
      </div>
      <div className="flex items-center gap-2">
        <span>{replay.position ? new Date(replay.position).toLocaleString('zh-CN', { hour12: false }) : '尚无观测'}</span>
        <input type="range" className="flex-1" aria-label="当前页播放位置" step="1"
          min={replay.first} max={Math.max(replay.first + 1, replay.last)} value={Math.max(replay.first, replay.position)}
          disabled={disabled || !replay.pageCount} onChange={event => replay.seekWithinPage(Number(event.target.value))} />
        {replay.error && <span style={{ color: 'var(--error)' }}>{replay.error}</span>}
      </div>
    </div>
  )
}
