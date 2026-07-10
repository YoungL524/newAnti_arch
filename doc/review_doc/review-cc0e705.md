# Review cc0e705

## 结论

当前系统已经被实现为：

```text
基础设施常驻 + 业务采集由单个 active task 驱动
```

它不是“整个上位机所有功能都必须依赖任务”：

- 设备台账、通信端点、系统健康、配置管理可以在无任务时工作
- 开始采集、航迹显示、日志/事件/历史数据归属要求 active task

这个方向本身没错。真正的问题是当前一个 `Task` 同时承担了两种完全不同的概念：

1. 可长期复用的任务方案/业务配置
2. 某一次不可复用的执行记录

当前代码明确把 Task 定义成 one-shot：

```cpp
/// Tasks are one-shot: Completed/Archived cannot re-activate
```

而创建弹窗所谓“部署模板”只复制 `deviceIds`。这正是用户觉得逻辑不对的根源。

建议将模型拆成：

```text
任务方案 MissionPlan（可复用）
任务执行 TaskRun（一次性、可追溯）
```

用户界面仍然可以把 MissionPlan 简称为“任务”，把 TaskRun 显示为“执行记录”。

## 先回答：是否每次开机都要新建任务？

不应该。

当前后端已经会在启动时查询数据库中的 `Active` task，并刷新 `ActiveTaskContext`。因此只要任务一直保持 Active，关闭再启动 core 后理论上可以恢复，不需要重新创建。

但当前缺少清晰的恢复/暂停语义：

- 应用正常关闭后，Active 是否继续视为 Active？
- 设备停止采集但任务未完成，属于 Active 还是 Suspended？
- 第二天继续同一警戒业务，是继续同一执行记录，还是创建新执行批次？

建议规则：

```text
同一个业务目标长期存在：复用同一个 MissionPlan
同一次连续值守/行动：使用同一个 TaskRun
应用重启：恢复未完成的 Active/Suspended TaskRun
明确完成一次行动：结束 TaskRun
下一次行动：从同一个 MissionPlan 创建新的 TaskRun
```

这样既不要求每次开机新建“任务方案”，又能保留每次执行的独立历史记录。

## 当前模型为什么别扭

当前 `TaskInfo` 同时包含：

- name/description：像任务定义
- deviceIds：像部署方案
- operatorName：像本次执行信息
- status：像执行状态机
- activatedAt/completedAt：像执行记录
- tracks/logs/system_events 的 taskId：像数据归属批次

这导致：

1. Completed task 不能重新激活，无法“复用任务”
2. 新建任务只能复制设备，无法复用完整业务配置
3. 修改任务设备会同时改变定义和执行上下文
4. 历史查看与任务激活被混成一件事
5. 删除任务可能让历史数据失去入口

## 推荐领域模型

### 1. MissionPlan：可复用的任务方案

建议字段：

```text
id
name
description
version
enabled
device bindings / device roles
runtime settings（DPH、工作参数）
display policy（航迹样式策略）
alert rules
geofence / working area
data forwarding rules
createdAt / updatedAt
```

它表达：

```text
这个地方/业务长期应该怎么运行
```

雷达长期部署在同一地点时，MissionPlan 可以一直复用。

### 2. TaskRun：一次任务执行记录

建议字段：

```text
id
missionPlanId
missionPlanVersion
planSnapshotJson
operatorName
status
startedAt
pausedAt
completedAt
abortReason
createdAt
```

它表达：

```text
在某个时间段，谁使用哪一版任务方案执行了一次业务
```

所有运行数据应归属 `taskRunId`：

- tracks
- logs
- system_events
- acquisition sessions

当前数据库字段名 `task_id` 在迁移期可以继续保留，但语义必须明确为 `taskRunId`，不是 MissionPlan id。

### 3. AcquisitionSession：可选的采集会话

如果需要区分同一 TaskRun 中多次开始/停止采集，可以增加：

```text
sessionId
taskRunId
startedAt
stoppedAt
stopReason
```

但第一阶段不必立即实现。TaskRun 可以跨越一次应用重启和多次 Start/Stop。

## “复用整个业务逻辑”应该是什么意思

用户截图中的“从已有任务复制设备”应删除或重命名，因为它不是任务复用，只是设备列表复制。

真正的复用应该是：

```text
选择 MissionPlan
  -> 加载该方案的设备角色、运行参数、告警规则、显示策略、区域和转发规则
  -> 创建一个新的 TaskRun
  -> 把当前方案版本做成不可变快照
  -> 本次航迹/日志/事件归属新 TaskRun
```

要注意：

- 复用配置，不复用历史数据
- 复用 MissionPlan，不重新激活 Completed TaskRun
- TaskRun 创建后保存方案快照，后续修改 MissionPlan 不应篡改历史执行语义

## P1. 当前“复制已有任务”功能概念错误

位置：

- `ui/src/components/dialogs/TaskCreateDialog.tsx`

当前：

```ts
const deviceIds = copyFrom
  ? tasks.find(...).deviceIds
  : []
```

界面却称为：

```text
部署模板
```

建议近期先做最小修正：

1. 删除“部署模板/从已有任务复制设备”选项，避免继续强化错误模型。
2. 新任务创建时显式选择设备，或者选择真正的 MissionPlan。
3. 在 MissionPlan 尚未实现前，可以临时改名为“复制设备选择”，但不建议长期保留。

## P1. 历史查看不应依赖重新激活任务

当前 UI 只有：

```text
activeTaskId
```

历史航迹和事件也跟随 activeTaskId 加载。

这导致用户要么只能看当前任务，要么误以为必须重新激活旧任务才能查看历史。

建议前端拆成：

```ts
activeTaskRunId   // 当前真正运行的执行记录
viewTaskRunId     // 当前正在查看的历史执行记录
```

规则：

- `activeTaskRunId` 控制实时采集和实时航迹
- `viewTaskRunId` 控制历史航迹、日志、事件查询
- 查看 Completed/Archived TaskRun 不改变系统当前运行状态
- 可以一边运行任务 A，一边查看任务 B 的历史，但 UI 必须明确处于历史查看模式

“日志归档”列表中的任务应该支持点击查看，而不是只有归档和删除按钮。

## P1. 删除任务会破坏历史入口

位置：

- `core/business/TaskService.cpp`
- `TaskService::remove()`
- `ui/src/components/layout/Sidebar.tsx`

当前 UI 对非 active task 显示删除按钮。后端删除：

```sql
DELETE FROM task_devices WHERE task_id = ?
DELETE FROM tasks WHERE id = ?
```

但 tracks/system_events 仍通过 `task_id` 保存归属。删除 task 后，这些数据可能成为孤儿，并且历史 API 失去任务列表入口。

建议：

1. 有运行数据的 TaskRun 禁止物理删除。
2. 使用 Archived/DeletedAt 软删除。
3. 历史数据保留策略由明确的数据生命周期策略控制。
4. Draft 且从未执行的 TaskRun 才允许物理删除。

## P1. 任务执行配置必须做快照

当前 `task_devices` 是可修改关系，TaskInfo 在 Archived 之前也可能更新。

如果一次 TaskRun 执行完成后，用户修改设备列表或未来修改运行参数，历史数据的业务含义会发生漂移。

建议 TaskRun 激活时生成不可变快照：

```text
plan_snapshot_json
device_snapshot_json
runtime_settings_snapshot_json
```

至少应快照：

- 业务设备 id/name/type/endpoint binding
- DPH/运行参数
- 航迹显示策略版本
- 告警规则版本

不要只保存当前对 MissionPlan 的引用。

## P2. 当前状态机缺少 Suspended/Aborted

当前：

```text
Draft -> Active -> Completed -> Archived
```

建议 TaskRun 状态：

```text
Draft -> Active <-> Suspended -> Completed
                         \-> Aborted
Completed/Aborted -> Archived
```

语义：

- Stop acquisition 不等于 Complete TaskRun
- 应用重启可以恢复 Active，或启动时转为 Suspended 等待确认
- 操作员主动结束才是 Completed
- 异常中止是 Aborted，不应伪装成 Completed

## P2. 新激活任务自动完成旧任务过于强硬

位置：

- `TaskService::activate()`

当前激活新 task 会把其他 Active task 自动改成 Completed。

这在“单活动执行”约束下可以理解，但不应静默完成旧任务。

建议 API 返回冲突：

```text
当前已有 active TaskRun，请先暂停或完成
```

或者显式提供：

```text
POST /runs/{id}/switch
```

并要求调用方选择：暂停旧任务还是完成旧任务。

## 本轮代码 review

本轮 `cc0e705` 对上一轮问题的修复方向基本正确：

- `/api/start` 无 active task 时明确拒绝
- 激活任务前要求至少一个已绑定 endpoint 的雷达
- 移除了 `refreshActiveTaskCache()` 中隐式写 task_devices
- 移除了前端启动采集时偷偷合并任务设备
- 删除了单航迹 80 点上限
- `TrackDisplayManager` 开始严格检查 `pt.taskId == activeTaskId`
- 启动诊断开始打印 config/database/device/task 映射

这些修改符合架构方向。

但任务模型本身仍然需要按上文拆分。

## 推荐迁移路线

为了避免一次性大改，建议分四步。

### Milestone A：先修 UI 和历史查看

1. 删除/重命名“从已有任务复制设备”。
2. 增加 `viewTaskRunId`，允许点击 Completed/Archived 记录查看历史。
3. 禁止删除有历史数据的任务，只允许归档。
4. 明确提示：应用重启会恢复未完成 active task，不必每次新建。

### Milestone B：引入 MissionPlan

新增：

```text
mission_plans
mission_plan_devices
```

第一阶段只包含：

- name/description
- deviceIds
- runtime settings
- display policy

### Milestone C：将现有 tasks 明确为 TaskRun

在现有 tasks 表增加：

```text
mission_plan_id
mission_plan_version
plan_snapshot_json
```

历史 tasks 可迁移为：

```text
mission_plan_id = NULL
legacy standalone TaskRun
```

这样不需要立即重命名所有 `task_id` 字段。

### Milestone D：完善执行状态机

增加：

- Suspended
- Aborted
- Resume
- 启动恢复策略
- 可选 AcquisitionSession

## 建议 Cursor 下一步做什么

不要马上实现完整 MissionPlan 大重构。

下一轮先做一份简短设计和最小 UI 修正：

1. 删除“部署模板/复制设备”的误导入口。
2. 将任务列表分成“当前执行”和“历史执行记录”。
3. 增加 `viewTaskRunId`，让 Completed/Archived 任务可查看历史，不影响 activeTaskRunId。
4. 禁止删除已有航迹/事件数据的 TaskRun。
5. 新建 `doc/business/任务方案与执行记录设计.md`，固定 MissionPlan/TaskRun 的字段、生命周期和迁移方案。

设计确认后，再让 Cursor 实现 MissionPlan。
