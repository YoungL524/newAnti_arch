/**
 * @file tasksApi.ts
 * @brief 封装任务 CRUD 以及开始、停止等生命周期命令。
 * @details 客户端只提交意图并展示服务端结果，合法状态迁移和设备绑定约束由 TaskService 判定。
 */

import type { RadarControlSettings, Task, TaskRadarArrayType, TaskStatus, TaskTemplate } from './taskTypes'

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
  /** 创建任务时选择的模板稳定UUID。 */
  sourceTemplateId: string
  /** 后端从模板复制出的任务独立参数快照。 */
  controlSettings: RadarControlSettings
  createdAt: number
  activatedAt?: number
  completedAt?: number
  archivedAt?: number
}

interface TaskListData { tasks: ApiTask[] }
interface TaskResponseData { task: ApiTask }
interface ActiveTaskData { activeTaskId: string | null; task?: ApiTask }
interface TemplateListData { templates: TaskTemplate[] }

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
    case 'Faulted': return 'faulted'
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
    sourceTemplateId: item.sourceTemplateId,
    controlSettings: item.controlSettings,
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
  sourceTemplateId: string
}): Record<string, unknown> {
  return {
    name: data.name,
    description: data.description ?? '',
    operatorName: data.operator ?? '',
    deviceIds: data.deviceIds ?? [],
    radarArrayType: data.radarArrayType ?? 'singleFace',
    deviceFaceMasks: data.deviceFaceMasks ?? {},
    sourceTemplateId: data.sourceTemplateId,
  }
}

/** @brief 读取后端权威任务模板；模板参数不会在前端生成默认值。 */
export async function fetchTaskTemplates(): Promise<ApiResult<TemplateListData>> {
  return apiCall('/api/v1/task-templates')
}

/**
 * @brief 读取任务台账和当前活动身份；刷新被替代或页面卸载时允许取消同一轮的两次请求。
 * @param signal 可空的浏览器取消信号，只影响只读刷新，不取消任务命令。
 * @return 后端权威列表与活动身份；失败不构造活动任务。
 */
export async function fetchTasksFromBackend(signal?: AbortSignal): Promise<{ tasks: Task[]; activeTaskId: string | null } & ApiResult> {
  const listResult = await apiCall<TaskListData>(BASE, { signal })
  if (!listResult.ok || !listResult.data) {
    return { ok: false, error: listResult.error, tasks: [], activeTaskId: null }
  }

  const activeResult = await apiCall<ActiveTaskData>(`${BASE}/active`, { signal })
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

/**
 * @brief 请求后端持久化已完成任务的归档时间，保留任务关联的航迹与事件事实。
 * @param id 已完成任务的稳定 UUID。
 * @return 更新后的权威任务；拒绝或存储失败时返回可展示错误。
 */
export async function archiveTaskApi(id: string): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/archive`, { method: 'POST' })
}

/** @brief 请求后端为指定活动任务创建本地逐阵面采集会话。 */
export async function startTaskAcquisitionApi(id: string): Promise<ApiResult<{ acquisition: boolean }>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/acquisition/start`, { method: 'POST' })
}

/** @brief 请求后端收口指定活动任务的全部本地逐阵面采集会话。 */
export async function stopTaskAcquisitionApi(id: string): Promise<ApiResult<{ acquisition: boolean }>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/acquisition/stop`, { method: 'POST' })
}

export async function deleteTaskApi(id: string): Promise<ApiResult> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}`, { method: 'DELETE' })
}

/**
 * @brief 提交当前活动任务的完整控制参数快照。
 * @param id 任务稳定 UUID。
 * @param settings 界面草稿中的完整参数；结构与值域由后端再次校验。
 * @return 更新后的权威任务；被拒绝、状态冲突或存储失败时返回可展示错误。
 * @note 只在用户点击“应用”时调用一次，输入过程不发请求。
 */
export async function updateTaskControlSettingsApi(
  id: string, settings: RadarControlSettings,
): Promise<ApiResult<TaskResponseData>> {
  return apiCall(`${BASE}/${encodeURIComponent(id)}/control-settings`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ controlSettings: settings }),
  })
}
