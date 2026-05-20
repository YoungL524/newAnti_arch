const tracks = [
  { id: 'TK-001', type: '无人机', status: '跟踪中', time: '14:32:05' },
  { id: 'TK-002', type: '直升机', status: '丢失', time: '14:30:12' },
  { id: 'TK-003', type: '固定翼', status: '跟踪中', time: '14:28:44' },
  { id: 'TK-004', type: '未知', status: '预测', time: '14:25:30' },
  { id: 'TK-005', type: '无人机', status: '跟踪中', time: '14:22:18' },
]

const statusColors: Record<string, string> = {
  '跟踪中': '#10b981',
  '丢失': '#ef4444',
  '预测': '#f59e0b',
}

export default function TrackList() {
  return (
    <div className="flex flex-col overflow-hidden" style={{ background: '#1f2937' }}>
      {/* Header */}
      <div
        className="flex items-center h-[32px] min-h-[32px] px-3"
        style={{ borderBottom: '1px solid #374151' }}
      >
        <span className="text-[11px] font-medium" style={{ color: '#f3f4f6' }}>航迹列表</span>
        <span className="ml-2 text-[10px] px-1.5 py-[1px] rounded" style={{ background: 'rgba(59,130,246,0.15)', color: '#3b82f6' }}>
          {tracks.length}
        </span>
      </div>

      {/* Table */}
      <div className="flex-1 overflow-auto">
        <table className="w-full text-[11px] border-collapse">
          <thead>
            <tr style={{ borderBottom: '1px solid #374151' }}>
              <Th>ID</Th>
              <Th>类型</Th>
              <Th>状态</Th>
              <Th>更新时间</Th>
            </tr>
          </thead>
          <tbody>
            {tracks.map((t, i) => (
              <tr
                key={t.id}
                className="hover:bg-[rgba(255,255,255,0.03)] transition-colors"
                style={{ borderBottom: i < tracks.length - 1 ? '1px solid rgba(55,65,81,0.5)' : 'none' }}
              >
                <Td>
                  <span style={{ color: '#3b82f6' }}>{t.id}</span>
                </Td>
                <Td>{t.type}</Td>
                <Td>
                  <div className="flex items-center gap-1.5">
                    <span className="text-[8px]" style={{ color: statusColors[t.status] || '#9ca3af' }}>●</span>
                    <span style={{ color: statusColors[t.status] || '#9ca3af' }}>{t.status}</span>
                  </div>
                </Td>
                <Td style={{ color: '#9ca3af' }}>{t.time}</Td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  )
}

function Th({ children }: { children: React.ReactNode }) {
  return (
    <th className="text-left font-normal py-1.5 px-2 text-[10px] font-medium"
      style={{ color: 'rgba(255,255,255,0.35)', borderBottom: '1px solid #374151' }}>
      {children}
    </th>
  )
}

function Td({ children, style }: { children: React.ReactNode; style?: React.CSSProperties }) {
  return (
    <td className="py-1.5 px-2" style={{ color: '#d1d5db', ...style }}>
      {children}
    </td>
  )
}
