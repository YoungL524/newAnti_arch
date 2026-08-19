/**
 * @file taskTypes.ts
 * @brief 定义任务实体、生命周期状态和设备绑定的前端类型。
 * @details 状态值必须与核心 TaskService 状态机一致；界面不得自行构造核心不接受的状态迁移。
 */

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
