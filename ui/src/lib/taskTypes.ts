export type TaskStatus = 'draft' | 'active' | 'completed' | 'archived'

export interface Task {
  id: string
  name: string
  description: string
  operator: string
  status: TaskStatus
  deviceIds: string[]
  geofence?: { lng: number; lat: number }[]
  alertRule?: string
  createdAt: number
  activatedAt?: number
  completedAt?: number
  archivedAt?: number
}
