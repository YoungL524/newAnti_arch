import { createPortal } from 'react-dom'
import RadarEditForm from './RadarEditForm'
import SubDeviceEditForm from './SubDeviceEditForm'
import type { AppDevice, RadarDevice, SubDevice } from '../map/types'

interface Props {
  device: AppDevice
  radarDevices: RadarDevice[]
  onUpdateDevice: (id: string, updates: Partial<RadarDevice>) => void
  onUpdateSubDevice: (id: string, updates: Partial<SubDevice>) => void
  onClose: () => void
}

export default function DeviceEditDialog({ device, radarDevices, onUpdateDevice, onUpdateSubDevice, onClose }: Props) {
  const content = 'lng' in device ? (
    <RadarEditForm
      device={device}
      onSave={(updates) => { onUpdateDevice(device.id, updates); onClose() }}
      onCancel={onClose}
    />
  ) : (
    <SubDeviceEditForm
      device={device}
      radarDevices={radarDevices}
      onSave={(updates) => { onUpdateSubDevice(device.id, updates); onClose() }}
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
        {content}
      </div>
    </div>,
    document.body,
  )
}
