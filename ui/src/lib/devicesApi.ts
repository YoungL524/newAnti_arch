const BASE = '/api/devices'

export async function fetchDevices(): Promise<any[]> {
  const res = await fetch(BASE)
  const data = await res.json()
  if (data.ok && data.devices) return data.devices
  return []
}

export async function createDevice(device: any): Promise<boolean> {
  const res = await fetch(BASE, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(device),
  })
  const data = await res.json()
  if (!data.ok) console.warn('[devicesApi] create failed:', data.error)
  return !!data.ok
}

export async function updateDevice(id: string, device: any): Promise<boolean> {
  const res = await fetch(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(device),
  })
  const data = await res.json()
  if (!data.ok) console.warn('[devicesApi] update failed:', data.error)
  return !!data.ok
}

export async function deleteDevice(id: string): Promise<boolean> {
  const res = await fetch(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'DELETE',
  })
  const data = await res.json()
  if (!data.ok) console.warn('[devicesApi] delete failed:', data.error)
  return !!data.ok
}
