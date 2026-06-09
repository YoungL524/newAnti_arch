import { useState, useMemo } from 'react'
import type { RadarDevice, SubDevice, AppDevice, RadarFace, RadarBrand } from '../map/types'
import { BRAND_LABELS } from '../map/types'
import type { Task } from '../../lib/taskTypes'
import { useCore } from '../../hooks/useCoreApi'

interface NavItem {
  id: string; icon: string; label: string
}

const topNav: NavItem[] = [
  { id: 'devices', icon: 'router', label: '设备列表' },
  { id: 'resources', icon: 'folder_open', label: '资源管理' },
  { id: 'replay', icon: 'replay', label: '数据回放' },
  { id: 'calibration', icon: 'tune', label: '校准分析' },
  { id: 'archives', icon: 'archive', label: '日志归档' },
]

export type DeviceLoadState = 'loading' | 'ready' | 'empty' | 'unavailable'

interface SidebarProps {
  onClose?: () => void
  allDevices: AppDevice[]
  onAddDevice: (d: Omit<RadarDevice, 'id'>) => Promise<boolean>
  onRemoveDevice: (id: string) => Promise<boolean>
  onUpdateDevice: (id: string, updates: Partial<RadarDevice>) => Promise<boolean>
  onAddSubDevice: (d: Omit<SubDevice, 'id'>) => Promise<boolean>
  onRemoveSubDevice: (id: string) => Promise<boolean>
  onEditDevice: (d: AppDevice) => void
  activeTask: Task | null
  tasks: Task[]
  deviceLoadState: DeviceLoadState
  onNewTask: () => void
  onCompleteTask: () => void
  onArchiveTask: (taskId: string) => void
  onDeleteTask: (taskId: string) => void
}

type FormKind = 'radar' | 'opto' | 'level' | 'gps'

const RADAR_DEFAULTS: Omit<RadarDevice, 'id'> = {
  name: '', brand: 'cetc38', lng: 118.6, lat: 31.9, altitude: 0,
  maxRange: 5,
  faces: [{ label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true }],
}

const BRAND_OPTIONS: RadarBrand[] = ['cetc38', 'cetc14', 'leiying']

const FACE_PRESETS: { label: string; faces: Omit<RadarFace, 'active'>[] }[] = [
  { label: '单面阵', faces: [{ label: 'A', boresightAzimuth: 0, scanAngle: 45 }] },
  { label: '四面阵', faces: [
    { label: 'A', boresightAzimuth: 0, scanAngle: 90 },
    { label: 'B', boresightAzimuth: 90, scanAngle: 90 },
    { label: 'C', boresightAzimuth: 180, scanAngle: 90 },
    { label: 'D', boresightAzimuth: 270, scanAngle: 90 },
  ]},
]

function isRadar(d: AppDevice): d is RadarDevice {
  return 'lng' in d
}

function kindLabel(kind: FormKind) {
  return { radar: '雷达', opto: '光学', level: '水平仪', gps: 'GPS' }[kind]
}

function kindIcon(kind: FormKind) {
  return { radar: 'sensors', opto: 'videocam', level: 'balance', gps: 'satellite_alt' }[kind]
}

function nextFaceLabel(faces: RadarFace[]) {
  const used = new Set(faces.map(f => f.label))
  for (let i = 0; i < 26; i++) {
    const label = String.fromCharCode(65 + i)
    if (!used.has(label)) return label
  }
  return `F${faces.length + 1}`
}

const inputStyle: React.CSSProperties = {
  width: '100%', padding: '4px 6px', borderRadius: 4,
  border: '1px solid var(--hairline)', background: '#fff', color: 'var(--ink)',
  fontSize: 10, outline: 'none', fontFamily: "'Inter','Microsoft YaHei',sans-serif",
}

const STATUS_META: Record<string, { icon: string; label: string }> = {
  active: { icon: 'radio_button_checked', label: '进行中' },
  completed: { icon: 'check_circle', label: '已完成' },
  archived: { icon: 'archive', label: '已归档' },
}

export default function Sidebar({ onClose, allDevices, onAddDevice, onRemoveDevice, onUpdateDevice, onAddSubDevice, onRemoveSubDevice, onEditDevice, activeTask, tasks, deviceLoadState, onNewTask, onCompleteTask, onArchiveTask, onDeleteTask }: SidebarProps) {
  const [activeNav, setActiveNav] = useState('devices')
  const [formExpanded, setFormExpanded] = useState(false)
  const [formKind, setFormKind] = useState<FormKind>('radar')
  const [radarForm, setRadarForm] = useState(RADAR_DEFAULTS)
  const [subForm, setSubForm] = useState<Omit<SubDevice, 'id'>>({ name: '', kind: 'opto', brand: 'cetc38', linkedRadarId: '' })

  const radarDevices = useMemo(() => allDevices.filter(isRadar), [allDevices])
  const { devices: deviceStates, checkedDeviceIds, noSelectionHint, acquisition, state: globalState, setCheckedDevices } = useCore()

  const toggleDeviceCheck = (id: string) => {
    setCheckedDevices(
      checkedDeviceIds.includes(id)
        ? checkedDeviceIds.filter(did => did !== id)
        : [...checkedDeviceIds, id]
    )
  }

  const resetForm = () => {
    setRadarForm(RADAR_DEFAULTS)
    setSubForm({ name: '', kind: 'opto', brand: 'cetc38', linkedRadarId: '' })
    setFormKind('radar')
    setFormExpanded(false)
  }

  const kindChips: FormKind[] = ['radar', 'opto', 'level', 'gps']

  // --- Face helpers for the add form ---
  const updateFaceInForm = (idx: number, field: keyof RadarFace, value: any) => {
    const newFaces = radarForm.faces.map((f, i) => i === idx ? { ...f, [field]: value } : f)
    setRadarForm({ ...radarForm, faces: newFaces })
  }
  const addFaceToForm = () => {
    const label = nextFaceLabel(radarForm.faces)
    setRadarForm({ ...radarForm, faces: [...radarForm.faces, { label, boresightAzimuth: 0, scanAngle: 45, active: true }] })
  }
  const removeFaceFromForm = (idx: number) => {
    if (radarForm.faces.length <= 1) return
    setRadarForm({ ...radarForm, faces: radarForm.faces.filter((_, i) => i !== idx) })
  }
  const applyFacePreset = (preset: Omit<RadarFace, 'active'>[]) => {
    setRadarForm({ ...radarForm, faces: preset.map(f => ({ ...f, active: true })) })
  }

  // --- Brand select shared styles ---
  const selectStyle: React.CSSProperties = {
    ...inputStyle, appearance: 'auto', cursor: 'pointer',
  }

  return (
    <aside
      className="w-56 flex flex-col h-full py-4 px-3 gap-y-2 z-30 shrink-0 overflow-y-auto"
      style={{
        background: 'var(--surfaceLow)',
        borderRight: '1px solid rgba(191, 201, 195, 0.3)',
      }}
    >
      {/* Brand */}
      <div className="mb-3 px-1">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-[15px] font-bold tracking-tight" style={{ color: 'var(--primaryContainer)', fontFamily: "'Inter', sans-serif" }}>
              DCMP
            </h1>
            <p className="text-[9px] uppercase tracking-[0.18em] mt-0.5 opacity-60" style={{ color: 'var(--outline)', fontFamily: "'Inter', sans-serif" }}>
              系统注册表 V2.4.0
            </p>
          </div>
          {onClose && (
            <button onClick={onClose} className="border-none bg-transparent cursor-pointer p-1"
              style={{ color: 'var(--onSurfaceVariant)' }}>
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                <path d="M18 6L6 18M6 6l12 12" strokeLinecap="round" />
              </svg>
            </button>
          )}
        </div>
      </div>

      {/* New Mission button */}
      <button className="mb-3 mx-0 py-2 px-3 rounded-lg text-white font-medium text-[12px]
                         flex items-center justify-center gap-2 precision-shadow
                         hover:brightness-110 transition-all active:scale-[0.98]
                         border-none cursor-pointer"
        style={{ background: 'var(--primaryContainer)' }}
        onClick={onNewTask}>
        <span className="material-symbols-outlined !text-[16px]">add</span>
        新建任务
      </button>

      {/* Navigation */}
      <nav className="flex-1 space-y-0.5">
        {topNav.map((item) => (
          <NavButton
            key={item.id}
            icon={item.icon}
            label={item.label}
            active={activeNav === item.id}
            onClick={() => setActiveNav(item.id)}
          />
        ))}

        {(activeNav === 'devices' || activeNav === 'resources' || activeNav === 'replay' || activeNav === 'calibration' || activeNav === 'archives') && (
          <div className="flex-1 px-1 overflow-y-auto mt-3">
            {activeNav === 'devices' && (
              <>
                {/* Task context bar */}
                {activeTask && (
                  <div className="flex items-center gap-2 px-2 py-1.5 mb-1 rounded-md"
                    style={{ background: 'var(--emeraldSubtle)' }}>
                    <span className="material-symbols-outlined !text-[14px]" style={{ color: 'var(--primaryContainer)' }}>
                      {STATUS_META[activeTask.status]?.icon ?? 'radio_button_unchecked'}
                    </span>
                    <div className="flex-1 min-w-0">
                      <div className="text-[10px] font-semibold truncate" style={{ color: 'var(--ink)' }}>
                        {activeTask.name}
                      </div>
                      <div className="text-[8px]" style={{ color: 'var(--secondary)' }}>
                        {STATUS_META[activeTask.status]?.label} &middot; {activeTask.operator || '未指定'}
                      </div>
                    </div>
                    {activeTask.status === 'active' && (
                      <button
                        className="px-1.5 py-0.5 rounded text-[8px] font-semibold border cursor-pointer"
                        style={{ borderColor: 'var(--primaryContainer)', color: 'var(--primaryContainer)', background: 'transparent' }}
                        onClick={onCompleteTask}
                      >完成</button>
                    )}
                  </div>
                )}
                <div key={noSelectionHint ? 'flash' : 'ok'} className={noSelectionHint ? 'animate-alert-flash rounded-md' : ''}>
                <p className="text-[9px] uppercase tracking-[0.18em] mb-0.5 opacity-80 font-semibold"
                  style={{ color: 'var(--outline)', fontFamily: "'Inter', sans-serif" }}>
                  {activeTask ? '任务设备' : '已注册设备'} &middot; {allDevices.length}
                </p>

                {deviceLoadState === 'loading' && (
                  <p className="text-[10px] py-2" style={{ color: 'var(--secondary)' }}>正在加载设备台账...</p>
                )}
                {deviceLoadState === 'unavailable' && (
                  <p className="text-[10px] py-2" style={{ color: '#991b1b' }}>后端设备台账不可用，设备管理功能受限</p>
                )}
                {deviceLoadState === 'empty' && (
                  <p className="text-[10px] py-2" style={{ color: 'var(--secondary)' }}>后端台账为空，请添加设备</p>
                )}

                {allDevices.map(d => {
                  if (isRadar(d)) {
                    const checked = checkedDeviceIds.includes(d.id)
                    const controllable = d.backendManaged === true && !!d.endpointId
                    const stateColor =
                      globalState === 'Fault' ? '#ef4444' :
                      acquisition ? '#22c55e' : '#94a3b8'
                    return (
                      <div key={d.id}>
                        <div className="flex items-center gap-1.5 px-1.5 py-1 rounded-md hover:bg-black/3 transition-colors cursor-pointer"
                          onDoubleClick={() => onEditDevice(d)} title="双击编辑设备参数">
                          {/* Checkbox — disabled if not from backend or no endpoint binding */}
                          <input type="checkbox" checked={checked}
                            disabled={!controllable}
                            onChange={() => toggleDeviceCheck(d.id)}
                            onClick={(e) => e.stopPropagation()}
                            style={{ width: 11, height: 11, cursor: controllable ? 'pointer' : 'not-allowed', accentColor: '#064e3b' }}
                          />
                          <span className="material-symbols-outlined !text-[13px]" style={{ color: 'var(--primaryContainer)' }}>sensors</span>
                          <div className="flex-1 min-w-0">
                            <div className="flex items-center gap-1.5">
                              <span className="text-[11px] font-semibold truncate" style={{ color: 'var(--ink)' }}>{d.name}</span>
                              <span className="text-[8px] px-1 py-px rounded-full font-medium" style={{ background: 'rgba(6,78,59,0.08)', color: 'var(--primaryContainer)' }}>雷达</span>
                            </div>
                            <div className="text-[9px]" style={{ color: 'var(--secondary)' }}>
                              {BRAND_LABELS[d.brand]} &middot; {d.faces.length}面 &middot; {d.maxRange}km
                              {d.endpointId ? ` · 端点 ${d.endpointId}` : <span style={{ color: '#991b1b' }}> · 未绑定端点</span>}
                            </div>
                          </div>
                          {/* Status dot */}
                          <span className="w-2 h-2 rounded-full flex-shrink-0" style={{ background: stateColor }} title={deviceStates.length > 0 ? globalState : '未连接'} />
                          <button
                            className="p-0.5 rounded border-none bg-transparent cursor-pointer hover:bg-red-50 flex-shrink-0"
                            style={{ color: 'var(--onSurfaceVariant)' }}
                            onClick={async () => { if (await onRemoveDevice(d.id)) setCheckedDevices(checkedDeviceIds.filter(did => did !== d.id)) }}
                          >
                            <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                              <path d="M18 6L6 18M6 6l12 12" strokeLinecap="round" />
                            </svg>
                          </button>
                        </div>
                      </div>
                    )
                  }
                  const s = d as SubDevice
                  const icon = kindIcon(s.kind)
                  return (
                    <div key={s.id} className="flex items-center gap-2 px-2 py-1.5 rounded-md hover:bg-black/3 transition-colors cursor-pointer"
                      onDoubleClick={() => onEditDevice(s)} title="双击编辑设备参数">
                      <span className="material-symbols-outlined !text-[14px]" style={{ color: 'var(--secondary)' }}>{icon}</span>
                      <div className="flex-1 min-w-0">
                        <div className="flex items-center gap-1.5">
                          <span className="text-[11px] font-semibold truncate" style={{ color: 'var(--ink)' }}>{s.name}</span>
                          <span className="text-[8px] px-1 py-px rounded-full font-medium" style={{ background: 'rgba(100,116,139,0.08)', color: '#64748b' }}>{kindLabel(s.kind)}</span>
                        </div>
                        <div className="text-[9px]" style={{ color: 'var(--secondary)' }}>
                          {BRAND_LABELS[s.brand]}
                          {s.kind !== 'gps' && (s.linkedRadarId ? ` · 关联: ${allDevices.find(d => isRadar(d) && d.id === s.linkedRadarId)?.name ?? s.linkedRadarId}` : ' · 未关联')}
                          {s.kind === 'gps' && (s.coordinateSystem ? ` · ${s.coordinateSystem}` : '')}
                        </div>
                      </div>
                      <button
                        className="p-0.5 rounded border-none bg-transparent cursor-pointer hover:bg-red-50 flex-shrink-0"
                        style={{ color: 'var(--onSurfaceVariant)' }}
                        onClick={async () => { if (await onRemoveSubDevice(s.id)) setCheckedDevices(checkedDeviceIds.filter(did => did !== s.id)) }}
                      >
                        <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
                          <path d="M18 6L6 18M6 6l12 12" strokeLinecap="round" />
                        </svg>
                      </button>
                    </div>
                  )
                })}

                </div>

                {!formExpanded ? (
                  <button
                    className="w-full py-1.5 mt-1 rounded-md text-[10px] font-semibold border border-dashed cursor-pointer transition-colors hover:bg-black/3"
                    style={{ color: 'var(--primaryBright)', borderColor: 'rgba(6,78,59,0.2)', background: 'transparent' }}
                    onClick={() => setFormExpanded(true)}
                  >
                    + 添加设备
                  </button>
                ) : (
                  <div className="space-y-1.5 mt-1 p-2 rounded-md" style={{ background: 'var(--surface)' }}>
                    {/* Type selector chips */}
                    <div className="flex gap-1">
                      {kindChips.map(k => (
                        <button key={k}
                          className="flex-1 py-1 rounded text-[9px] font-semibold border cursor-pointer transition-all"
                          style={{
                            background: formKind === k ? 'var(--primaryContainer)' : 'transparent',
                            color: formKind === k ? '#ffffff' : 'var(--secondary)',
                            borderColor: formKind === k ? 'var(--primaryContainer)' : 'var(--hairline)',
                          }}
                          onClick={() => { setFormKind(k); setSubForm(prev => ({ ...prev, kind: k, brand: prev.brand })) }}
                        >
                          {kindLabel(k)}
                        </button>
                      ))}
                    </div>

                    {/* Common: name */}
                    <input
                      style={inputStyle}
                      placeholder="设备名称"
                      value={formKind === 'radar' ? radarForm.name : subForm.name}
                      onChange={e => {
                        if (formKind === 'radar') setRadarForm({ ...radarForm, name: e.target.value })
                        else setSubForm({ ...subForm, name: e.target.value })
                      }}
                    />

                    {/* ─── Radar fields ─── */}
                    {formKind === 'radar' && (
                      <>
                        {/* Brand */}
                        <select style={selectStyle}
                          value={radarForm.brand}
                          onChange={e => setRadarForm({ ...radarForm, brand: e.target.value as RadarBrand })}
                        >
                          {BRAND_OPTIONS.map(b => <option key={b} value={b}>{BRAND_LABELS[b]}</option>)}
                        </select>

                        <div className="grid grid-cols-2 gap-1">
                          <input type="number" step="any" placeholder="经度" style={inputStyle}
                            value={radarForm.lng} onChange={e => setRadarForm({ ...radarForm, lng: Number(e.target.value) })} />
                          <input type="number" step="any" placeholder="纬度" style={inputStyle}
                            value={radarForm.lat} onChange={e => setRadarForm({ ...radarForm, lat: Number(e.target.value) })} />
                        </div>
                        <div className="grid grid-cols-2 gap-1">
                          <input type="number" placeholder="海拔m" style={inputStyle}
                            value={radarForm.altitude} onChange={e => setRadarForm({ ...radarForm, altitude: Number(e.target.value) })} />
                          <input type="number" placeholder="距离km" style={inputStyle}
                            value={radarForm.maxRange} onChange={e => setRadarForm({ ...radarForm, maxRange: Number(e.target.value) })} />
                        </div>

                        {/* Face config table */}
                        <div className="text-[9px] font-semibold mt-1" style={{ color: 'var(--secondary)' }}>阵面配置</div>
                        {radarForm.faces.map((face, fi) => (
                          <div key={fi} className="grid grid-cols-[auto_1fr_1fr_auto] gap-1 items-center">
                            <span className="text-[9px] font-medium" style={{ color: 'var(--ink)', minWidth: 16 }}>{face.label}</span>
                            <input type="number" step="any" placeholder="朝向°" style={inputStyle}
                              value={face.boresightAzimuth} onChange={e => updateFaceInForm(fi, 'boresightAzimuth', Number(e.target.value))} />
                            <input type="number" step="any" placeholder="范围°" style={inputStyle}
                              value={face.scanAngle} onChange={e => updateFaceInForm(fi, 'scanAngle', Number(e.target.value))} />
                            <button
                              className="p-0.5 rounded border-none bg-transparent cursor-pointer hover:bg-red-50"
                              style={{ color: 'var(--onSurfaceVariant)', lineHeight: 1, fontSize: 12 }}
                              onClick={() => removeFaceFromForm(fi)}
                              title="删除阵面"
                            >×</button>
                          </div>
                        ))}

                        {/* Face action row */}
                        <div className="flex gap-1 mt-0.5">
                          <button className="px-1.5 py-0.5 rounded text-[8px] font-semibold border cursor-pointer"
                            style={{ borderColor: 'var(--hairline)', background: 'transparent', color: 'var(--secondary)' }}
                            onClick={addFaceToForm}
                          >+ 添加阵面</button>
                          {FACE_PRESETS.map(p => (
                            <button key={p.label} className="px-1.5 py-0.5 rounded text-[8px] font-semibold border cursor-pointer"
                              style={{ borderColor: 'var(--hairline)', background: 'transparent', color: 'var(--secondary)' }}
                              onClick={() => applyFacePreset(p.faces)}
                            >{p.label}</button>
                          ))}
                        </div>
                      </>
                    )}

                    {/* ─── Opto fields ─── */}
                    {formKind === 'opto' && (
                      <>
                        <select style={selectStyle}
                          value={subForm.brand}
                          onChange={e => setSubForm({ ...subForm, brand: e.target.value as RadarBrand })}
                        >
                          {BRAND_OPTIONS.map(b => <option key={b} value={b}>{BRAND_LABELS[b]}</option>)}
                        </select>
                        <select style={selectStyle}
                          value={subForm.linkedRadarId ?? ''}
                          onChange={e => setSubForm({ ...subForm, linkedRadarId: e.target.value || undefined })}
                        >
                          <option value="">关联雷达 (可选)</option>
                          {radarDevices.map(r => (
                            <option key={r.id} value={r.id}>{r.name}</option>
                          ))}
                        </select>
                        <input type="number" step="any" placeholder="安装偏角 (°)" style={inputStyle}
                          value={subForm.mountOffsetAzimuth ?? ''}
                          onChange={e => setSubForm({ ...subForm, mountOffsetAzimuth: e.target.value ? Number(e.target.value) : undefined })} />
                      </>
                    )}

                    {/* ─── Level fields ─── */}
                    {formKind === 'level' && (
                      <>
                        <select style={selectStyle}
                          value={subForm.brand}
                          onChange={e => setSubForm({ ...subForm, brand: e.target.value as RadarBrand })}
                        >
                          {BRAND_OPTIONS.map(b => <option key={b} value={b}>{BRAND_LABELS[b]}</option>)}
                        </select>
                        <select style={selectStyle}
                          value={subForm.linkedRadarId ?? ''}
                          onChange={e => setSubForm({ ...subForm, linkedRadarId: e.target.value || undefined })}
                        >
                          <option value="">关联雷达 (可选)</option>
                          {radarDevices.map(r => (
                            <option key={r} value={r.id}>{r.name}</option>
                          ))}
                        </select>
                      </>
                    )}

                    {/* ─── GPS fields ─── */}
                    {formKind === 'gps' && (
                      <>
                        <select style={selectStyle}
                          value={subForm.brand}
                          onChange={e => setSubForm({ ...subForm, brand: e.target.value as RadarBrand })}
                        >
                          {BRAND_OPTIONS.map(b => <option key={b} value={b}>{BRAND_LABELS[b]}</option>)}
                        </select>
                        <input type="text" placeholder="坐标系 (默认 WGS-84)" style={inputStyle}
                          value={subForm.coordinateSystem ?? ''}
                          onChange={e => setSubForm({ ...subForm, coordinateSystem: e.target.value || undefined })} />
                      </>
                    )}

                    {/* Buttons */}
                    <div className="flex gap-1.5 pt-0.5">
                      <button className="flex-1 py-1 rounded text-[10px] font-semibold border-none cursor-pointer"
                        style={{ background: 'var(--primaryContainer)', color: 'var(--onPrimary)' }}
                        disabled={!((formKind === 'radar' ? radarForm.name : subForm.name).trim())}
                        onClick={async () => {
                          const name = (formKind === 'radar' ? radarForm.name : subForm.name).trim()
                          if (!name) return
                          const ok = formKind === 'radar'
                            ? await onAddDevice(radarForm)
                            : await onAddSubDevice({ ...subForm, name })
                          if (ok) resetForm()
                        }}
                      >确认添加</button>
                      <button className="px-2 py-1 rounded text-[10px] border-none bg-transparent cursor-pointer"
                        style={{ color: 'var(--secondary)' }}
                        onClick={resetForm}
                      >取消</button>
                    </div>
                  </div>
                )}
              </>
            )}

            {activeNav === 'resources' && (
              <PanelPlaceholder icon="folder_open" title="资源管理"
                desc="导入/导出设备配置文件、校准数据、回放数据文件、地图图层等资源。" />
            )}

            {activeNav === 'replay' && (
              <PanelPlaceholder icon="replay" title="数据回放"
                desc="加载航迹数据文件，在地图上回放探测事件，支持时间轴控制和事件浏览。" />
            )}

            {activeNav === 'calibration' && (
              <PanelPlaceholder icon="tune" title="校准分析"
                desc="查看各设备校准数据、方位/俯仰修正表、校准质量指标及前后对比报告。" />
            )}

            {activeNav === 'archives' && (
              <div className="space-y-3">
                <div className="flex items-center gap-2">
                  <span className="material-symbols-outlined !text-[18px]" style={{ color: 'var(--primaryContainer)' }}>archive</span>
                  <span className="text-[11px] font-semibold" style={{ color: 'var(--ink)', fontFamily: "'Inter', sans-serif" }}>日志归档</span>
                </div>
                {tasks.length === 0 ? (
                  <p className="text-[10px]" style={{ color: 'var(--secondary)' }}>暂无任务记录，创建任务并完成后将在此归档。</p>
                ) : (
                  <div className="space-y-0.5">
                    {tasks.map(t => (
                      <div key={t.id}
                        className="flex items-center gap-2 px-2 py-1.5 rounded-md"
                        style={{ background: t.id === activeTask?.id ? 'var(--emeraldSubtle)' : 'transparent' }}>
                        <span className="material-symbols-outlined !text-[14px]" style={{ color: 'var(--secondary)' }}>
                          {STATUS_META[t.status]?.icon ?? 'radio_button_unchecked'}
                        </span>
                        <div className="flex-1 min-w-0">
                          <div className="text-[10px] font-semibold truncate" style={{ color: 'var(--ink)' }}>{t.name}</div>
                          <div className="text-[8px]" style={{ color: 'var(--secondary)' }}>
                            {STATUS_META[t.status]?.label} &middot; {t.operator || '未指定'} &middot; {t.deviceIds.length} 设备
                          </div>
                        </div>
                        {t.id !== activeTask?.id && (
                          <>
                            {t.status === 'completed' && (
                              <button
                                className="px-1 py-0.5 rounded text-[8px] font-semibold border cursor-pointer"
                                style={{ borderColor: 'var(--secondary)', color: 'var(--secondary)', background: 'transparent' }}
                                onClick={() => onArchiveTask(t.id)}
                              >归档</button>
                            )}
                            <button
                              className="px-1 py-0.5 rounded text-[8px] font-semibold border cursor-pointer hover:bg-red-50"
                              style={{ borderColor: 'transparent', color: 'var(--secondary)', background: 'transparent' }}
                              onClick={() => onDeleteTask(t.id)}
                              title="删除任务"
                            >×</button>
                          </>
                        )}
                        {t.id === activeTask?.id && (
                          <span className="text-[8px] px-1 py-px rounded-full font-medium"
                            style={{ background: 'rgba(6,78,59,0.08)', color: 'var(--primaryContainer)' }}>
                            当前
                          </span>
                        )}
                      </div>
                    ))}
                  </div>
                )}
              </div>
            )}
          </div>
        )}

        {/* Structure tree — radar devices with face toggles */}
        {!['devices','resources','replay','calibration','archives'].includes(activeNav) && (
          <div className="mt-5 px-1">
            <p className="text-[9px] uppercase tracking-[0.18em] mb-2 opacity-80 font-semibold"
              style={{ color: 'var(--outline)', fontFamily: "'Inter', sans-serif" }}>
              结构
            </p>
            <div className="space-y-1.5">
              <TreeNode icon="hub" label="全局集群" arrow="keyboard_arrow_down" />
              {radarDevices.map(d => (
                <div key={d.id}>
                  <TreeNode
                    icon="sensors"
                    label={`${d.name}  (${d.faces.filter(f => f.active).length}/${d.faces.length})`}
                    depth={1}
                    onClick={() => {
                      const allActive = d.faces.every(f => f.active)
                      onUpdateDevice(d.id, { faces: d.faces.map(f => ({ ...f, active: !allActive })) })
                    }}
                  />
                  {d.faces.map((face, fi) => (
                    <TreeNode
                      key={fi}
                      icon={face.active ? 'radio_button_checked' : 'radio_button_unchecked'}
                      label={`阵面 ${face.label} · ${face.boresightAzimuth}° ±${face.scanAngle}°`}
                      depth={2}
                      active={face.active}
                      onClick={() => {
                        const newFaces = d.faces.map((f, i) => i === fi ? { ...f, active: !f.active } : f)
                        onUpdateDevice(d.id, { faces: newFaces })
                      }}
                    />
                  ))}
                </div>
              ))}
            </div>
          </div>
        )}
      </nav>

      {/* Bottom */}
      <div className="mt-auto pt-3 space-y-0.5">
        <button className="w-full px-2 py-1.5 rounded-md flex items-center gap-2
                           border-none bg-transparent cursor-pointer transition-colors
                           hover:bg-white/60"
          style={{ color: 'rgba(64, 73, 68, 0.6)' }}>
          <span className="material-symbols-outlined !text-[16px]">help</span>
          <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>帮助</span>
        </button>
        <button className="w-full px-2 py-1.5 rounded-md flex items-center gap-2
                           border-none bg-transparent cursor-pointer transition-colors
                           hover:bg-white/60"
          style={{ color: 'rgba(64, 73, 68, 0.6)' }}>
          <span className="material-symbols-outlined !text-[16px]">terminal</span>
          <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>系统日志</span>
        </button>
      </div>
    </aside>
  )
}

function NavButton({ icon, label, active, onClick }: {
  icon: string; label: string; active: boolean; onClick: () => void
}) {
  return (
    <button
      onClick={onClick}
      className={`w-full px-2 py-1.5 rounded-md flex items-center gap-2
                  border-none cursor-pointer transition-all ${active ? '' : 'hover:bg-white/60'}`}
      style={{
        background: active ? '#ffffff' : 'transparent',
        color: active ? 'var(--primaryContainer)' : 'var(--onSurfaceVariant)',
        fontWeight: active ? 600 : 400,
        boxShadow: active ? '0 1px 3px rgba(0,0,0,0.05)' : 'none',
        border: active ? '1px solid rgba(6, 78, 59, 0.05)' : '1px solid transparent',
      }}
    >
      <span className="material-symbols-outlined !text-[16px]">{icon}</span>
      <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>{label}</span>
    </button>
  )
}

function TreeNode({ icon, label, arrow, active, depth = 0, onClick }: {
  icon: string; label: string; arrow?: string; active?: boolean; depth?: number; onClick?: () => void
}) {
  return (
    <div
      onClick={onClick}
      className="flex items-center gap-3 cursor-pointer transition-colors group"
      style={{
        marginLeft: `${depth * 10}px`,
        color: active ? 'var(--primaryContainer)' : depth === 0 ? 'var(--onSurfaceVariant)' : 'rgba(64, 73, 68, 0.7)',
        fontWeight: active ? 600 : 400,
        borderLeft: depth > 0 ? '2px solid' : 'none',
        borderColor: depth === 1 ? 'rgba(6, 78, 59, 0.2)' : depth === 2 ? (active ? 'var(--primaryContainer)' : 'rgba(6, 78, 59, 0.1)') : 'var(--stone200)',
        paddingLeft: depth > 0 ? '8px' : '0',
      }}
    >
      {arrow && <span className="material-symbols-outlined !text-[14px] opacity-40">{arrow}</span>}
      <span className="material-symbols-outlined !text-[16px]">{icon}</span>
      <span className="text-[12px] font-medium" style={{ fontFamily: "'Inter', sans-serif" }}>{label}</span>
    </div>
  )
}

function PanelPlaceholder({ icon, title, desc }: { icon: string; title: string; desc: string }) {
  return (
    <div className="space-y-3">
      <div className="flex items-center gap-2">
        <span className="material-symbols-outlined !text-[18px]" style={{ color: 'var(--primaryContainer)' }}>{icon}</span>
        <span className="text-[11px] font-semibold" style={{ color: 'var(--ink)', fontFamily: "'Inter', sans-serif" }}>{title}</span>
      </div>
      <p className="text-[10px] leading-relaxed" style={{ color: 'var(--secondary)' }}>{desc}</p>
      <div className="mt-2 p-3 rounded-md flex items-center justify-center"
        style={{ background: 'var(--surface)', border: '1px dashed var(--hairline)' }}>
        <span className="text-[10px]" style={{ color: 'var(--secondary)' }}>功能开发中...</span>
      </div>
    </div>
  )
}
