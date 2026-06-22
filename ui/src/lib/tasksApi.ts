import type { Task, TaskStatus } from './taskTypes'
import { loadTasks, loadActiveTaskId } from './taskTypes'

const BASE = '/api/tasks'

export interface ApiResult<T = boolean> {
  ok: boolean
  error?: string
  data?: T
}

export interface ApiTask {
  id: string
  name: string
  description?: string
  operatorName?: string
  status: string
  deviceIds: string[]
  createdAt: number
  activatedAt?: number
  completedAt?: number
  archivedAt?: number
}

interface TaskListData { ok: boolean; tasks: ApiTask[] }
interface TaskResponseData { ok: boolean; task: ApiTask }
interface ActiveTaskData { ok: boolean; activeTaskId: string | null; task?: ApiTask }

async function apiCall<T = any>(url: string, options?: RequestInit): Promise<ApiResult<T>> {
  try {
    const res = await fetch(url, options)
    if (!res.ok) return { ok: false, error: `HTTP ${res.status}` }
    const data = await res.json()
    if (!data.ok) return { ok: false, error: data.error || 'API 返回失败' }
    return { ok: true, data }
  } catch (e: any) {
    return { ok: false, error: e?.message || '网络错误' }
  }
}

function mapStatus(status: string): TaskStatus {
  switch (status) {
    case 'Active': return 'active'
    case 'Completed': return 'completed'
    case 'Archived': return 'archived'
    case 'Draft': return 'draft'
    default: return 'draft'
  }
}

export function apiTaskToTask(item: ApiTask): Task {
  return {
    id: item.id,
    name: item.name,
    description: item.description ?? '',
    operator: item.operatorName ?? '',
    status: mapStatus(item.status),
    deviceIds: item.deviceIds ?? [],
    createdAt: item.createdAt,
    completedAt: item.completedAt || undefined,
  }
}

export function taskToApiPayload(data: {
  name: string
  description?: string
  operator?: string
  deviceIds?: string[]
}): Record<string, unknown> {
  return {
    name: data.name,
    description: data.description ?? '',
    operatorName: data.operator ?? '',
    deviceIds: data.deviceIds ?? [],
  }
}

export async function fetchTasksFromBackend(): Promise<{ tasks: Task[]; activeTaskId: string | null } & ApiResult> {
  const listResult = await apiCall<TaskListData>(BASE)
  if (!listResult.ok || !listResult.data) {
    return { ok: false, error: listResult.error, tasks: loadTasks(), activeTaskId: loadActiveTaskId() }
  }

  const tasks = (listResult.data.tasks ?? []).map(apiTaskToTask)
  const activeResult = await apiCall<ActiveTaskData>(`${BASE}/active`)
  const activeTaskId = activeResult.ok && activeResult.data?.activeTaskId
    ? activeResult.data.activeTaskId
    : null

  return { ok: true, tasks, activeTaskId }
}

export async function createTaskApi(payload: Record<string, unknown>): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
}

export async function activateTaskApi(id: string): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/activate`, { method: 'POST' })
}

export async function completeTaskApi(id: string): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/complete`, { method: 'POST' })
}

export async function archiveTaskApi(id: string): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/archive`, { method: 'POST' })
}

export async function deleteTaskApi(id: string): Promise<ApiResult> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}`, { method: 'DELETE' })
}

export async function updateTaskApi(id: string, payload: Record<string, unknown>): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
}
