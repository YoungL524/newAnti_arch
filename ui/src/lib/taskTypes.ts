export type TaskStatus = 'active' | 'completed' | 'archived'

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
  completedAt?: number
}

export const TASK_STORAGE_KEY = 'dcmp_tasks'
export const ACTIVE_TASK_KEY = 'dcmp_activeTaskId'

export function loadTasks(): Task[] {
  try {
    const raw = localStorage.getItem(TASK_STORAGE_KEY)
    return raw ? JSON.parse(raw) : []
  } catch { return [] }
}

export function saveTasks(tasks: Task[]) {
  localStorage.setItem(TASK_STORAGE_KEY, JSON.stringify(tasks))
}

export function loadActiveTaskId(): string | null {
  return localStorage.getItem(ACTIVE_TASK_KEY)
}

export function saveActiveTaskId(id: string | null) {
  if (id) localStorage.setItem(ACTIVE_TASK_KEY, id)
  else localStorage.removeItem(ACTIVE_TASK_KEY)
}
