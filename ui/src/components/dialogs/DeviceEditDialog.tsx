import { useState } from 'react'
import { createPortal } from 'react-dom'
import RadarEditForm from './RadarEditForm'
import SubDeviceEditForm from './SubDeviceEditForm'
import type { AppDevice, RadarDevice, SubDevice } from '../map/types'
import { saveEndpoints, type CommEndpoint } from '../../lib/endpointsApi'

interface Props {
  device: AppDevice
  radarDevices: RadarDevice[]
  onUpdateDevice: (id: string, updates: Partial<RadarDevice>) => Promise<boolean>
  onUpdateSubDevice: (id: string, updates: Partial<SubDevice>) => Promise<boolean>
  onClose: () => void
}

export default function DeviceEditDialog({ device, radarDevices, onUpdateDevice, onUpdateSubDevice, onClose }: Props) {
  const [saveError, setSaveError] = useState('')

  const handleRadarSave = async (updates: Partial<RadarDevice>, endpoint: CommEndpoint) => {
    setSaveError('')
    if (endpoint.localPort <= 0) {
      setSaveError('本机端口必须大于 0')
      return
    }
    const epResult = await saveEndpoints([endpoint])
    if (!epResult.ok) {
      setSaveError(epResult.error || '端点保存失败')
      return
    }
    const ok = await onUpdateDevice(device.id, { ...updates, endpointId: endpoint.id })
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
        style={{ width: 460, maxHeight: '85vh', overflow: 'hidden' }}
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
