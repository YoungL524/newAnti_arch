import { useState } from 'react'

interface TreeNode {
  label: string
  children?: TreeNode[]
  expanded?: boolean
}

const tree: TreeNode[] = [{
  label: 'Radar_Project',
  expanded: true,
  children: [{
    label: 'src',
    expanded: true,
    children: [
      { label: 'main.cpp' },
      { label: 'radar.cpp' },
      { label: 'display.cpp' },
    ],
  }, {
    label: 'config',
    children: [
      { label: 'settings.json' },
    ],
  }, {
    label: 'README.md',
  }, {
    label: '.gitignore',
  }, {
    label: 'CMakeLists.txt',
  }],
}]

export default function Sidebar() {
  return (
    <aside
      className="w-[260px] min-w-[260px] flex flex-col overflow-hidden"
      style={{ background: 'var(--bg-sidebar)' }}
    >
      {/* Header */}
      <div
        className="h-[36px] min-h-[36px] flex items-center px-4"
        style={{
          background: 'linear-gradient(180deg, rgba(77,141,255,0.05) 0%, transparent 100%)',
        }}
      >
        <span
          className="text-[10px] font-semibold tracking-[0.9px]"
          style={{ color: 'rgba(255,255,255,0.30)' }}
        >
          EXPLORER
        </span>
      </div>

      {/* Tree */}
      <div className="flex-1 overflow-auto py-1">
        {tree.map((node, i) => (
          <TreeNodeItem key={i} node={node} depth={0} />
        ))}
      </div>
    </aside>
  )
}

function TreeNodeItem({ node, depth }: { node: TreeNode; depth: number }) {
  const hasChildren = node.children && node.children.length > 0
  const [expanded, setExpanded] = useState(node.expanded ?? true)

  return (
    <div>
      <div
        className="flex items-center h-[26px] cursor-pointer text-[12px] pl-[12px] pr-2
                   hover:bg-[rgba(255,255,255,0.03)]"
        style={{ paddingLeft: `${12 + depth * 14}px`, color: 'var(--text-body)' }}
        onClick={() => hasChildren && setExpanded(!expanded)}
      >
        {hasChildren ? (
          <span className="w-4 text-center text-[10px] mr-1"
                style={{ color: 'rgba(255,255,255,0.30)' }}>
            {expanded ? '▾' : '▸'}
          </span>
        ) : (
          <span className="w-4 mr-1" />
        )}
        <span>{node.label}</span>
      </div>
      {hasChildren && expanded && (
        node.children!.map((child, i) => (
          <TreeNodeItem key={i} node={child} depth={depth + 1} />
        ))
      )}
    </div>
  )
}
