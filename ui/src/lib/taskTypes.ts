/**
 * @file taskTypes.ts
 * @brief 定义任务实体、生命周期状态和设备绑定的前端类型。
 * @details 状态值必须与核心 TaskService 状态机一致；界面不得自行构造核心不接受的状态迁移。
 */

export type TaskStatus = 'draft' | 'ready' | 'active' | 'completed' | 'archived'

/** 任务直接使用的雷达整机形态，用于从设备台账筛选兼容设备。 */
export type TaskRadarArrayType = 'singleFace' | 'fourFace'

export interface Task {
  id: string
  name: string
  description: string
  operator: string
  status: TaskStatus
  /** 本次任务要求的雷达整机形态。 */
  radarArrayType: TaskRadarArrayType
  deviceIds: string[]
  /** 每台雷达本次任务启用的阵面位图；bit0～bit3对应1#～4#。 */
  deviceFaceMasks: Record<string, number>
  geofence?: { lng: number; lat: number }[]
  alertRule?: string
  createdAt: number
  activatedAt?: number
  completedAt?: number
  archivedAt?: number
}
