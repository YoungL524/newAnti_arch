/**
 * @file DeviceEditDialog.tsx
 * @brief 根据设备类型选择对应编辑表单，并提交新增或修改结果。
 * @details 对话框维护未提交的表单草稿；只有核心 API 成功后才通知上层刷新设备台账。
 */

import { useState } from 'react'
import { createPortal } from 'react-dom'
import RadarEditForm from './RadarEditForm'
import SubDeviceEditForm from './SubDeviceEditForm'
import type { AppDevice, RadarDevice, SubDevice } from '../map/types'
import type { RadarEndpoint as CommEndpoint } from '../map/types'
import type { RadarControlSettings, Task } from '../../lib/taskTypes'
import type { RadarSettingsApplyResult } from './RadarControlSettingsDialog'

interface Props {
  device: AppDevice
  radarDevices: RadarDevice[]
  onUpdateDevice: (id: string, updates: Partial<RadarDevice>) => Promise<boolean>
  onUpdateSubDevice: (id: string, updates: Partial<SubDevice>) => Promise<boolean>
  onClose: () => void
  /** 当前可编辑任务；设备不属于该任务时由雷达表单隐藏控制编辑器。 */
  controlTask: Task | null
  /** 单独提交当前任务控制参数。 */
  onApplyControlSettings: (settings: RadarControlSettings) => Promise<RadarSettingsApplyResult>
  /** 当前任务参数不可编辑或更新中。 */
  controlSettingsDisabled: boolean
}

/**
 * @brief 按设备类型展示台账编辑器，并把当前任务控制参数入口仅交给关联雷达。
 * @param device 当前编辑的台账设备。
 * @param radarDevices 子设备表单使用的雷达台账。
 * @param onUpdateDevice 提交雷达台账修改。
 * @param onUpdateSubDevice 提交子设备台账修改。
 * @param onClose 关闭设备编辑并放弃未提交草稿。
 * @param controlTask 当前活动任务；运行参数仍归任务所有。
 * @param onApplyControlSettings 提交独立任务快照。
 * @param controlSettingsDisabled 当前任务控制参数不可编辑。
 */
export default function DeviceEditDialog({ device, radarDevices, onUpdateDevice, onUpdateSubDevice,
  onClose, controlTask, onApplyControlSettings, controlSettingsDisabled }: Props) {
  const [saveError, setSaveError] = useState('')

  const handleRadarSave = async (updates: Partial<RadarDevice>, endpoint: CommEndpoint) => {
    setSaveError('')
    if (endpoint.localPort <= 0) {
      setSaveError('本机端口必须大于 0')
      return
    }
    const ok = await onUpdateDevice(device.id, { ...updates, endpointId: endpoint.id, endpoint })
    if (ok) onClose()
    else setSaveError('设备保存失败，请重试')
  }

  const handleSubSave = async (updates: Partial<SubDevice>) => {
    setSaveError('')
    const ok = await onUpdateSubDevice(device.id, updates)
    if (ok) onClose()
    else setSaveError('保存失败，请重试')
  }

  const content = 'lng' in device ? (
    <RadarEditForm
      device={device}
      onSave={handleRadarSave}
      onCancel={onClose}
      controlTask={controlTask?.deviceIds.includes(device.id) ? controlTask : null}
      onApplyControlSettings={onApplyControlSettings}
      controlSettingsDisabled={controlSettingsDisabled}
    />
  ) : (
    <SubDeviceEditForm
      device={device}
      radarDevices={radarDevices}
      onSave={handleSubSave}
      onCancel={onClose}
    />
  )

  return createPortal(
    <div
      className="fixed inset-0 z-50 flex items-center justify-center"
      style={{ background: 'rgba(0,0,0,0.12)' }}
      onClick={onClose}
    >
      <div
        className="glass-panel flex flex-col"
        style={{ width: 'lng' in device ? 'min(1120px, 96vw)' : 460,
          height: 'lng' in device ? 'min(800px, 92vh)' : undefined,
          maxHeight: '92vh', overflow: 'hidden' }}
        onClick={(e) => e.stopPropagation()}
        onKeyDown={(e) => { if (e.key === 'Escape') onClose() }}
      >
        {saveError && (
          <div className="px-5 py-2 text-[11px] flex items-center gap-2"
            style={{ background: '#fef2f2', color: '#991b1b', borderBottom: '1px solid #fecaca' }}>
            <span>⚠</span> {saveError}
          </div>
        )}
        {content}
      </div>
    </div>,
    document.body,
  )
}
