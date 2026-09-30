/**
 * @file RadarEditForm.tsx
 * @brief 在统一雷达配置工作区按侧栏分类编辑设备台账与任务控制参数。
 * @details 两类草稿切换期间保持挂载并分别提交；四面阵由物理1#绝对航向确定其余编号，各阵面安装姿态独立保存。
 */

import './RadarSettings.css'

import { useState } from 'react'
import type { RadarDevice, RadarBrand, RadarFace, RadarExternalForward } from '../map/types'
import { BRAND_LABELS, BRAND_OPTIONS } from '../map/types'
import type { RadarEndpoint as CommEndpoint } from '../map/types'
import { createRadarFaces, updateRadarFace } from '../../lib/radarFaces'
import RadarControlSettingsDialog, { type RadarSettingsApplyResult } from './RadarControlSettingsDialog'
import type { RadarControlSettings, Task } from '../../lib/taskTypes'
import { useCore } from '../../hooks/useCoreApi'
import { applyLiveRadarPose } from '../../lib/liveRadarPose'

interface Props {
  device: RadarDevice
  onSave: (updates: Partial<RadarDevice>, endpoint: CommEndpoint) => void
  onCancel: () => void
  /** 当前任务包含本设备时才允许编辑其控制快照。 */
  controlTask: Task | null
  /** 更新当前任务快照，不写入设备台账。 */
  onApplyControlSettings: (settings: RadarControlSettings) => Promise<RadarSettingsApplyResult>
  /** 任务参数更新中或当前上下文不可编辑。 */
  controlSettingsDisabled: boolean
}

const inputStyle: React.CSSProperties = {
  width: '100%',
  padding: '8px 11px',
  border: '1px solid #dce3e0',
  borderRadius: 8,
  background: '#fff',
  color: 'var(--ink)',
  fontSize: 13,
  fontFamily: "'Inter', 'Microsoft YaHei', sans-serif",
  height: 38,
  boxSizing: 'border-box',
  minWidth: 0,
}

const monoInputStyle: React.CSSProperties = {
  ...inputStyle,
  fontFamily: "'JetBrains Mono', monospace",
  fontSize: 12,
}

const labelStyle: React.CSSProperties = {
  fontSize: 12,
  color: '#52615b',
  marginBottom: 7,
  fontWeight: 500,
}

/**
 * @brief 编辑内部 UDP 端点的具名发送目标。
 * @param label 目标名称，DPH 与工作模式报文分别使用独立端口。
 * @param target 当前设备台账草稿中的地址及端口。
 * @param onChange 将修改写回设备台账草稿，不直接发送 UDP。
 */
function TargetFields({
  label,
  target,
  onChange,
}: {
  label: string
  target: { address: string; port: number }
  onChange: (t: { address: string; port: number }) => void
}) {
  return (
    <div className="rs-target-grid">
      <span className="text-[9px] font-medium" style={{ color: 'var(--secondary)' }}>
        {label}
      </span>
      <div>
        <div style={labelStyle}>地址</div>
        <input
          style={monoInputStyle}
          value={target.address}
          onChange={(e) => onChange({ ...target, address: e.target.value })}
        />
      </div>
      <div>
        <div style={labelStyle}>端口</div>
        <input
          style={monoInputStyle}
          type="number"
          value={target.port}
          onChange={(e) => onChange({ ...target, port: Number(e.target.value) || 0 })}
        />
      </div>
    </div>
  )
}

/**
 * @brief 维护设备台账草稿，并在同一工作区挂载关联任务的控制参数草稿。
 * @param device 待修改的设备台账事实。
 * @param onSave 单独提交设备台账修改。
 * @param onCancel 放弃台账草稿并关闭设备编辑。
 * @param controlTask 可编辑的当前任务，缺失时不创建控制参数草稿。
 * @param onApplyControlSettings 单独提交任务控制快照。
 * @param controlSettingsDisabled 当前任务不可编辑时禁止打开参数编辑器。
 */
export default function RadarEditForm({
  device,
  onSave,
  onCancel,
  controlTask,
  onApplyControlSettings,
  controlSettingsDisabled,
}: Props) {
  const [showControlSettings, setShowControlSettings] = useState(false)
  const [deviceSection, setDeviceSection] = useState<'basic' | 'connection' | 'deployment'>('basic')
  const [controlSection, setControlSection] = useState<'common' | 'frame' | 'face'>('common')
  const [form, setForm] = useState({
    name: device.name,
    brand: device.brand,
    model: device.model,
    protocolAdapterKey: '20260915',
    lng: device.lng,
    lat: device.lat,
    altitude: device.altitude,
    maxRange: device.maxRange,
    faces: device.faces.map((f) => ({ ...f })),
  })
  const [endpoint, setEndpoint] = useState<CommEndpoint>({
    ...device.endpoint,
    targets: { ...device.endpoint.targets },
  })
  const [externalForward, setExternalForward] = useState<RadarExternalForward>(
    device.externalForward ?? { protocol: 'udp', address: '', port: 0 },
  )
  const { deviceHeartbeat } = useCore()
  const livePose = deviceHeartbeat?.payload.radarPose?.deviceId === device.id
    ? deviceHeartbeat.payload.radarPose : undefined
  const shown = applyLiveRadarPose({ ...device, ...form }, livePose)

  const update = <K extends keyof typeof form>(key: K, value: (typeof form)[K]) => {
    setForm((prev) => ({ ...prev, [key]: value }))
  }

  const updateFace = (idx: number, field: keyof RadarFace, value: any) => {
    setForm((prev) => ({
      ...prev,
      faces: updateRadarFace(prev.faces, idx, field, value),
    }))
  }

  /** @brief 将设备阵面切换为合同允许的单面阵或四面阵。 */
  const setArrayType = (count: 1 | 4) => {
    setForm((prev) => ({
      ...prev,
      faces: createRadarFaces(count, prev.faces[0]?.heading ?? 0, prev.faces),
    }))
  }

  const setTarget = (key: string, val: { address: string; port: number }) => {
    setEndpoint((prev) => ({ ...prev, targets: { ...prev.targets, [key]: val } }))
  }

  const valid =
    form.name.trim().length > 0 &&
    form.model.trim().length > 0 &&
    form.protocolAdapterKey.trim().length > 0 &&
    endpoint.localPort > 0 &&
    (externalForward.address.trim() === ''
      ? externalForward.port === 0
      : Number.isInteger(externalForward.port) && externalForward.port > 0 && externalForward.port <= 65535) &&
    form.faces.every(
      (face) =>
        Number.isFinite(face.heading) &&
        Number.isFinite(face.pitch) &&
        face.pitch >= -90 &&
        face.pitch <= 90 &&
        Number.isFinite(face.roll) &&
        face.roll >= -90 &&
        face.roll <= 90,
    )

  const handleSave = () => {
    if (!valid) return
    onSave({ ...form, externalForward }, endpoint)
  }

  return (
    <>
      {controlTask && (
        <div style={{ display: showControlSettings ? 'block' : 'none', width: '100%', height: '100%' }}>
          <RadarControlSettingsDialog
            key={controlTask.id}
            task={controlTask}
            radarDevices={[device]}
            onApply={onApplyControlSettings}
            onClose={onCancel}
            disabled={controlSettingsDisabled}
            activeTab={controlSection}
            onTabChange={setControlSection}
            onNavigateDevice={(section) => {
              setDeviceSection(section)
              setShowControlSettings(false)
            }}
          />
        </div>
      )}
      <div
        className="flex flex-col bg-white"
        style={{ display: showControlSettings ? 'none' : 'flex', width: '100%', height: '100%' }}
      >
        {/** 雷达配置统一工作区的设备页；切换运行参数页时保留当前台账草稿。 */}
        <div
          className="flex items-center justify-between shrink-0 px-8 py-5"
          style={{ borderBottom: '1px solid var(--hairline)' }}
        >
          <div className="flex items-center gap-5">
            <span className="text-[20px] font-bold" style={{ color: 'var(--ink)' }}>
              雷达配置
            </span>
            <span
              className="pl-5 text-[14px]"
              style={{ color: 'var(--secondary)', borderLeft: '1px solid var(--hairline)' }}
            >
              {form.name}
            </span>
          </div>
          <button
            className="p-0.5 border-none bg-transparent cursor-pointer transition-colors hover:bg-black/5 rounded"
            style={{ color: 'var(--onSurfaceVariant)', lineHeight: 1 }}
            onClick={onCancel}
          >
            <svg
              width="16"
              height="16"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.6"
            >
              <path d="M18 6L6 18M6 6l12 12" strokeLinecap="round" />
            </svg>
          </button>
        </div>

        <div className="flex flex-1 min-h-0">
          <aside
            className="w-[226px] shrink-0 flex flex-col px-4 py-7"
            style={{ borderRight: '1px solid var(--hairline)', background: 'var(--surface)' }}
          >
            <div className="px-4 mb-3 text-[12px] font-semibold" style={{ color: 'var(--secondary)' }}>
              设备配置
            </div>
            {(
              [
                ['basic', '基本信息', 'description'],
                ['connection', '通信连接', 'link'],
                ['deployment', '部署与姿态', 'grid_view'],
              ] as const
            ).map(([key, label, icon]) => (
              <button
                key={key}
                type="button"
                onClick={() => setDeviceSection(key)}
                className="w-full flex items-center gap-3 px-4 py-3 rounded-lg text-[13px] text-left border-none cursor-pointer"
                style={{
                  background: deviceSection === key ? 'var(--emeraldSubtle)' : 'transparent',
                  color: deviceSection === key ? 'var(--primaryContainer)' : 'var(--onSurfaceVariant)',
                  fontWeight: deviceSection === key ? 700 : 500,
                }}
              >
                <span className="material-symbols-outlined" style={{ fontSize: 19 }}>
                  {icon}
                </span>
                {label}
              </button>
            ))}
            <div className="mx-3 my-5" style={{ borderTop: '1px solid var(--hairline)' }} />
            <div className="px-4 mb-3 text-[12px] font-semibold" style={{ color: 'var(--secondary)' }}>
              运行参数
            </div>
            {controlTask ? (
              (
                [
                  ['common', '通用参数', 'settings'],
                  ['frame', '帧参数', 'tune'],
                  ['face', '阵面参数', 'view_quilt'],
                ] as const
              ).map(([key, label, icon]) => (
                <button
                  key={key}
                  type="button"
                  disabled={controlSettingsDisabled}
                  onClick={() => {
                    setControlSection(key)
                    setShowControlSettings(true)
                  }}
                  className="w-full flex items-center gap-3 px-4 py-3 rounded-lg text-[13px] text-left border-none cursor-pointer disabled:opacity-40"
                  style={{ color: 'var(--onSurfaceVariant)', background: 'transparent' }}
                >
                  <span className="material-symbols-outlined" style={{ fontSize: 19 }}>
                    {icon}
                  </span>
                  {label}
                </button>
              ))
            ) : (
              <div className="px-4 text-[11px]" style={{ color: 'var(--secondary)' }}>
                当前设备未关联可编辑任务
              </div>
            )}
            <div
              className="mt-auto px-4 pt-5 text-[11px]"
              style={{ color: 'var(--secondary)', borderTop: '1px solid var(--hairline)' }}
            >
              当前任务
              <br />
              <span className="text-[13px] font-medium" style={{ color: 'var(--ink)' }}>
                {controlTask?.name ?? '未关联'}
              </span>
            </div>
          </aside>
          <div className="rs-content flex-1 overflow-y-auto">
            <div className="rs-page-heading">
              <h2 className="text-[21px] font-bold mb-1" style={{ color: 'var(--ink)' }}>
                {deviceSection === 'basic'
                  ? '基本信息'
                  : deviceSection === 'connection'
                    ? '通信连接'
                    : '部署与姿态'}
              </h2>
              <p className="text-[12px] mb-7" style={{ color: 'var(--secondary)' }}>
                {deviceSection === 'basic'
                  ? '管理设备名称、厂家与接入协议。'
                  : deviceSection === 'connection'
                    ? '配置本机监听端口与对端通信地址。'
                    : '设置地理位置与各阵面的安装姿态。'}
              </p>
            </div>
            {deviceSection === 'basic' && (
              <>
                <section className="rs-section rs-device-section">
                  <div className="rs-section-heading">
                    <h3>设备信息</h3>
                    <p>使用便于识别的设备名称，阵面类型在部署页设置。</p>
                  </div>{' '}
                  <div className="mb-3">
                    <div style={labelStyle}>设备名称</div>
                    <input
                      style={inputStyle}
                      value={form.name}
                      onChange={(e) => update('name', e.target.value)}
                      placeholder="例如：南京站一号雷达"
                    />
                  </div>
                </section>
              </>
            )}

            {deviceSection === 'connection' && (
              <section className="rs-section rs-device-section">
                <div className="rs-section-heading">
                  <h3>通信端点</h3>
                  <p>本机监听与目标地址</p>
                </div>

                {endpoint ? (
                  <div className="rs-endpoint">
                    <div className="grid grid-cols-2 gap-2">
                      <div>
                        <div style={labelStyle}>端点 ID</div>
                        <input style={monoInputStyle} value={endpoint.id} readOnly />
                      </div>
                      <div>
                        <div style={labelStyle}>协议</div>
                        <input style={monoInputStyle} value="UDP" readOnly />
                      </div>
                    </div>

                    <div>
                      <div style={labelStyle}>说明</div>
                      <input
                        style={inputStyle}
                        value={endpoint.description}
                        onChange={(e) => setEndpoint({ ...endpoint, description: e.target.value })}
                      />
                    </div>

                    <div className="rs-address-grid">
                      <div>
                        <div style={labelStyle}>本机监听地址</div>
                        <input
                          style={monoInputStyle}
                          value={endpoint.localAddress}
                          onChange={(e) => setEndpoint({ ...endpoint, localAddress: e.target.value })}
                        />
                      </div>
                      <div>
                        <div style={labelStyle}>本机端口</div>
                        <input
                          style={monoInputStyle}
                          type="number"
                          value={endpoint.localPort}
                          onChange={(e) =>
                            setEndpoint({ ...endpoint, localPort: Number(e.target.value) || 0 })
                          }
                        />
                      </div>
                    </div>

                    <div className="rs-targets">
                      <div className="text-[9px] font-semibold" style={{ color: 'var(--primaryContainer)' }}>
                        对端目标
                      </div>
                      <TargetFields
                        label="DPH"
                        target={endpoint.targets.dph ?? { address: '127.0.0.1', port: 0 }}
                        onChange={(t) => setTarget('dph', t)}
                      />
                      <TargetFields
                        label="workmode"
                        target={endpoint.targets.workmode ?? { address: '127.0.0.1', port: 0 }}
                        onChange={(t) => setTarget('workmode', t)}
                      />
                    </div>
                  </div>
                ) : null}
                <div className="rs-endpoint mt-5">
                  <div className="rs-section-heading">
                    <h3>外部通信</h3>
                    <p>保存转发目标配置，航迹转发功能后续接入。</p>
                  </div>
                  <div className="grid grid-cols-3 gap-3">
                    <div>
                      <div style={labelStyle}>协议</div>
                      <select
                        style={{ ...inputStyle, appearance: 'auto' }}
                        value={externalForward.protocol}
                        onChange={(e) => setExternalForward({ ...externalForward, protocol: e.target.value as RadarExternalForward['protocol'] })}
                      >
                        <option value="udp">UDP</option>
                        <option value="tcp">TCP</option>
                        <option value="http">HTTP</option>
                      </select>
                    </div>
                    <div>
                      <div style={labelStyle}>目标地址</div>
                      <input style={monoInputStyle} value={externalForward.address} onChange={(e) => setExternalForward({ ...externalForward, address: e.target.value })} placeholder="IP 地址" />
                    </div>
                    <div>
                      <div style={labelStyle}>目标端口</div>
                      <input style={monoInputStyle} type="number" min="0" max="65535" value={externalForward.port} onChange={(e) => setExternalForward({ ...externalForward, port: Number(e.target.value) || 0 })} />
                    </div>
                  </div>
                </div>
              </section>
            )}

            {deviceSection === 'basic' && (
              <section className="rs-section rs-device-section">
                <div className="rs-section-heading">
                  <h3>厂家与协议</h3>
                  <p>设备型号与通信适配</p>
                </div>

                <div className="mb-3">
                  <div style={labelStyle}>厂家</div>
                  <select
                    style={{ ...inputStyle, appearance: 'auto', cursor: 'pointer' }}
                    value={form.brand}
                    onChange={(e) => update('brand', e.target.value as RadarBrand)}
                  >
                    {BRAND_OPTIONS.map((b) => (
                      <option key={b} value={b}>
                        {BRAND_LABELS[b]}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="grid grid-cols-2 gap-3 mb-3">
                  <div>
                    <div style={labelStyle}>型号</div>
                    <input
                      style={inputStyle}
                      value={form.model}
                      onChange={(e) => update('model', e.target.value)}
                    />
                  </div>
                  <div>
                    <div style={labelStyle}>协议版本</div>
                    <input style={monoInputStyle} value={form.protocolAdapterKey} readOnly />
                  </div>
                </div>
              </section>
            )}

            {deviceSection === 'deployment' && (
              <>
                <section className="rs-section rs-device-section">
                  <div className="rs-section-heading">
                    <h3>部署位置</h3>
                    <p>雷达部署地理坐标；心跳在线时优先显示雷达实时值。</p>
                  </div>
                  {livePose && (
                    <p className="text-[11px] mb-3" style={{ color: 'var(--primaryContainer)' }}>
                      当前为 DP 心跳实时位姿；保存设备台账不会写入这些实时值。
                    </p>
                  )}

                  <div className="grid grid-cols-2 gap-3 mb-3">
                    <div>
                      <div style={labelStyle}>经度 (°)</div>
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        value={shown.lng}
                        readOnly={!!livePose}
                        onChange={(e) => update('lng', Number(e.target.value))}
                      />
                    </div>
                    <div>
                      <div style={labelStyle}>纬度 (°)</div>
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        value={shown.lat}
                        readOnly={!!livePose}
                        onChange={(e) => update('lat', Number(e.target.value))}
                      />
                    </div>
                  </div>

                  <div className="grid grid-cols-2 gap-3 mb-3">
                    <div>
                      <div style={labelStyle}>海拔 (m)</div>
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        value={shown.altitude}
                        readOnly={!!livePose}
                        onChange={(e) => update('altitude', Number(e.target.value))}
                      />
                    </div>
                  </div>
                </section>
                <section className="rs-section rs-device-section">
                  <div className="rs-section-heading">
                    <h3>阵面安装</h3>
                    <p>四面阵以 1# 航向为基准，其余航向自动确定。</p>
                  </div>

                  <div className="rs-face-table grid grid-cols-[44px_repeat(4,minmax(0,1fr))] gap-2 mb-1 px-1">
                    <span style={labelStyle}>阵面</span>
                    <span style={labelStyle}>航向 (°)</span>
                    <span style={labelStyle}>俯仰 (°)</span>
                    <span style={labelStyle}>横滚 (°)</span>
                    <span style={labelStyle}>扫描半角 (°)</span>
                  </div>

                  {form.faces.map((face, fi) => (
                    <div
                      key={fi}
                      className="rs-face-table grid grid-cols-[44px_repeat(4,minmax(0,1fr))] gap-2 mb-1.5 items-center"
                    >
                      <span className="text-[11px] font-medium" style={{ color: 'var(--ink)', minWidth: 20 }}>
                        {fi + 1}#
                      </span>
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        min="0"
                        max="359.999"
                        value={shown.faces[fi]?.heading ?? face.heading}
                        readOnly={!!livePose?.faces.some(item => item.faceIndex === fi)}
                        disabled={form.faces.length === 4 && fi > 0
                          && !livePose?.faces.some(item => item.faceIndex === fi)}
                        title={livePose?.faces.some(item => item.faceIndex === fi)
                          ? 'DP 心跳实时航向，只用于显示'
                          : form.faces.length === 4 && fi > 0 ? '由物理1#阵面航向自动确定' : undefined}
                        onChange={(e) => updateFace(fi, 'heading', Number(e.target.value))}
                      />
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        min="-90"
                        max="90"
                        value={shown.faces[fi]?.pitch ?? face.pitch}
                        readOnly={!!livePose?.faces.some(item => item.faceIndex === fi)}
                        onChange={(e) => updateFace(fi, 'pitch', Number(e.target.value))}
                      />
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        min="-90"
                        max="90"
                        value={shown.faces[fi]?.roll ?? face.roll}
                        readOnly={!!livePose?.faces.some(item => item.faceIndex === fi)}
                        onChange={(e) => updateFace(fi, 'roll', Number(e.target.value))}
                      />
                      <input
                        style={inputStyle}
                        type="number"
                        step="any"
                        value={face.scanAngle}
                        onChange={(e) => updateFace(fi, 'scanAngle', Number(e.target.value))}
                      />
                    </div>
                  ))}

                  <div className="rs-array-selector">
                    <button
                      className="px-2 py-1 rounded text-[9px] font-semibold border cursor-pointer transition-colors"
                      style={{
                        borderColor: 'var(--hairline)',
                        background: 'transparent',
                        color: 'var(--secondary)',
                      }}
                      aria-pressed={form.faces.length === 1}
                      onClick={() => setArrayType(1)}
                    >
                      单面阵
                    </button>
                    <button
                      className="px-2 py-1 rounded text-[9px] font-semibold border cursor-pointer transition-colors"
                      style={{
                        borderColor: 'var(--hairline)',
                        background: 'transparent',
                        color: 'var(--secondary)',
                      }}
                      aria-pressed={form.faces.length === 4}
                      onClick={() => setArrayType(4)}
                    >
                      四面阵
                    </button>
                  </div>
                </section>
              </>
            )}
          </div>
        </div>

        <div
          className="rs-footer flex items-center justify-between gap-2 shrink-0 px-8 py-4"
          style={{ borderTop: '1px solid var(--hairline)' }}
        >
          <span className="text-[12px]" style={{ color: 'var(--secondary)' }}>
            保存到设备台账
          </span>
          <div className="flex items-center gap-2">
            <button
              className="px-3 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-colors"
              style={{ background: 'transparent', color: 'var(--onSurfaceVariant)' }}
              onClick={onCancel}
            >
              取消
            </button>
            <button
              className="px-3 py-1.5 rounded text-[11px] font-medium border-none cursor-pointer transition-all"
              style={{
                background: valid ? 'var(--primaryContainer)' : 'rgba(6,78,59,0.15)',
                color: '#fff',
                opacity: valid ? 1 : 0.5,
              }}
              disabled={!valid}
              onClick={handleSave}
            >
              保存修改
            </button>
          </div>
        </div>
      </div>
    </>
  )
}
