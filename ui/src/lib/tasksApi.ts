/**
 * @file tasksApi.ts
 * @brief 封装任务 CRUD 以及开始、停止等生命周期命令。
 * @details 客户端只提交意图并展示服务端结果，合法状态迁移和设备绑定约束由 TaskService 判定。
 */

import type { Task, TaskRadarArrayType, TaskStatus } from './taskTypes'

const BASE = '/api/v1/tasks'

export interface ApiResult<T = boolean> {
  ok: boolean
  error?: string
  data?: T
}

export interface ApiTask {
  taskId: string
  name: string
  description?: string
  operatorName?: string
  status: string
  radarArrayType?: TaskRadarArrayType
  deviceIds: string[]
  deviceFaceMasks?: Record<string, number>
  createdAt: number
  activatedAt?: number
  completedAt?: number
  archivedAt?: number
}

interface TaskListData { tasks: ApiTask[] }
interface TaskResponseData { task: ApiTask }
interface ActiveTaskData { activeTaskId: string | null; task?: ApiTask }

async function apiCall<T = any>(url: string, options?: RequestInit): Promise<ApiResult<T>> {
  try {
    const res = await fetch(url, options)
    const data = await res.json().catch(() => null)
    if (!res.ok) return { ok: false, error: data?.error?.message || `HTTP ${res.status}` }
    if (!data) return { ok: false, error: 'API 未返回 JSON' }
    if (!data.ok) return { ok: false, error: data.error?.message || 'API 返回失败' }
    return { ok: true, data: data.data }
  } catch (e: any) {
    return { ok: false, error: e?.message || '网络错误' }
  }
}

function mapStatus(status: string): TaskStatus {
  switch (status) {
    case 'Ready': return 'ready'
    case 'Active': return 'active'
    case 'Completed': return 'completed'
    case 'Archived': return 'archived'
    case 'Draft': return 'draft'
    default: return 'draft'
  }
}

export function apiTaskToTask(item: ApiTask): Task {
  return {
    id: item.taskId,
    name: item.name,
    description: item.description ?? '',
    operator: item.operatorName ?? '',
    status: mapStatus(item.status),
    radarArrayType: item.radarArrayType ?? 'singleFace',
    deviceIds: item.deviceIds ?? [],
    deviceFaceMasks: item.deviceFaceMasks ?? {},
    createdAt: item.createdAt,
    activatedAt: item.activatedAt || undefined,
    completedAt: item.completedAt || undefined,
    archivedAt: item.archivedAt || undefined,
  }
}

export function taskToApiPayload(data: {
  name: string
  description?: string
  operator?: string
  deviceIds?: string[]
  radarArrayType?: TaskRadarArrayType
  deviceFaceMasks?: Record<string, number>
}): Record<string, unknown> {
  return {
    name: data.name,
    description: data.description ?? '',
    operatorName: data.operator ?? '',
    deviceIds: data.deviceIds ?? [],
    radarArrayType: data.radarArrayType ?? 'singleFace',
    deviceFaceMasks: data.deviceFaceMasks ?? {},
  }
}

export async function fetchTasksFromBackend(): Promise<{ tasks: Task[]; activeTaskId: string | null } & ApiResult> {
  const listResult = await apiCall<TaskListData>(BASE)
  if (!listResult.ok || !listResult.data) {
    return { ok: false, error: listResult.error, tasks: [], activeTaskId: null }
  }

  const activeResult = await apiCall<ActiveTaskData>(`${BASE}/active`)
  if (!activeResult.ok || !activeResult.data) {
    return { ok: false, error: activeResult.error, tasks: [], activeTaskId: null }
  }

  const tasks = (listResult.data.tasks ?? []).map(apiTaskToTask)
  const activeTaskId = activeResult.data.activeTaskId ?? null
  if (activeTaskId && activeResult.data.task && !tasks.some(task => task.id === activeTaskId)) {
    tasks.unshift(apiTaskToTask(activeResult.data.task))
  }

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

export async function deleteTaskApi(id: string): Promise<ApiResult> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}`, { method: 'DELETE' })
}
