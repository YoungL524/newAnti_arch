import { useState } from 'react'
import { createPortal } from 'react-dom'
import RadarEditForm from './RadarEditForm'
import SubDeviceEditForm from './SubDeviceEditForm'
import type { AppDevice, RadarDevice, SubDevice } from '../map/types'

interface Props {
  device: AppDevice
  radarDevices: RadarDevice[]
  onUpdateDevice: (id: string, updates: Partial<RadarDevice>) => Promise<boolean>
  onUpdateSubDevice: (id: string, updates: Partial<SubDevice>) => Promise<boolean>
  onClose: () => void
}

export default function DeviceEditDialog({ device, radarDevices, onUpdateDevice, onUpdateSubDevice, onClose }: Props) {
  const [saveError, setSaveError] = useState('')
  const [saving, setSaving] = useState(false)

  const handleSave = async (updates: Partial<RadarDevice> | Partial<SubDevice>) => {
    setSaveError('')
    setSaving(true)
    const ok = 'lng' in device
      ? await onUpdateDevice(device.id, updates as Partial<RadarDevice>)
      : await onUpdateSubDevice(device.id, updates as Partial<SubDevice>)
    setSaving(false)
    if (ok) onClose()
    else setSaveError('保存失败，请重试')
  }

  const content = 'lng' in device ? (
    <RadarEditForm
      device={device}
      onSave={(updates) => handleSave(updates)}
      onCancel={onClose}
    />
  ) : (
    <SubDeviceEditForm
      device={device}
      radarDevices={radarDevices}
      onSave={(updates) => handleSave(updates)}
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
        style={{ width: 420, maxHeight: '85vh', overflow: 'hidden' }}
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
