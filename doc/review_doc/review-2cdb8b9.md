# Review 2cdb8b9

## 结论

这一版是一次很小但正确的精修：

1. `normalizeTrackHistoryPoint()` 已要求 `deviceId` 非空。
2. `fetchTrackHistory()` 的 `deviceId` 查询参数已统一 `trim`。
3. `fetchSystemEvents()` 的 `taskId/deviceId/category` 查询参数已统一 `trim`。

当前没有发现新的代码问题。  
任务驱动上下文的数据链路已经闭合，可以进入“任务切换加载上下文”的策略设计。

## 本轮做对的点

### 1. 历史航迹归一化与类型一致

位置：

- `ui/src/lib/tracksApi.ts`

现在：

```ts
if (!taskId || !deviceId) return null
```

这和类型定义一致：

```ts
deviceId: string
```

符合当前架构：正常历史航迹必须能归属到业务设备。

### 2. 查询参数 trim 统一

位置：

- `ui/src/lib/tracksApi.ts`
- `ui/src/lib/systemEventsApi.ts`

`taskId/deviceId/category` 都先 trim 再拼 query。  
这类细节很小，但能减少后面排查“看起来一样但查不到”的问题。

### 3. 没有提前实现 UI

这一点继续是对的。  
当前代码只补数据链路和 helper，没有把历史航迹混入实时航迹状态。

## 当前状态判断

现在链路已经比较完整：

```text
后端归属 -> 查询 API -> 前端 helper -> 前端类型
```

具体包括：

- `tracks.task_id / tracks.device_id`
- `/api/tracks/history`
- `fetchTrackHistory()`
- `system_events.task_id / system_events.device_id`
- `/api/system-events`
- `fetchSystemEvents()`
- `LogEntry.taskId / LogEntry.deviceId`

也就是说，现在缺的不是基础设施，而是产品/交互策略。

## 下一步必须先定的策略

### 1. 激活任务时，是否清空实时显示？

我的建议：

**可以清空实时显示层。**

理由：

- 当前任务是业务上下文边界。
- 切换任务后，继续显示上一个任务的实时航迹，会让用户误以为这些点属于新任务。
- 清空实时层能保持语义干净。

但注意：

- 清空的是 UI 实时显示层，不是数据库历史数据。
- 后端历史表不受影响。

### 2. 是否自动加载该任务最近历史航迹？

我的建议：

**不要在激活任务时直接把历史航迹塞进实时 `tracks`。**

可以自动加载，但应该进入独立历史层，例如：

```text
historyTracks
```

而不是当前实时：

```text
tracks
```

这样用户能明确区分：

- 当前实时目标
- 当前任务历史轨迹

### 3. 实时航迹和历史航迹是否分层显示？

我的建议：

**必须分层。**

建议前端状态分成：

```ts
tracks: RadarTrack[]              // 实时层
historyTracks: TrackHistoryPoint[] // 历史层
systemEvents: SystemEvent[]        // 当前任务事件
```

地图层也应区分样式：

- 实时航迹：当前高亮样式
- 历史航迹：更淡、不可误认为实时目标

不要把历史点转换成 `RadarTrack` 后混入实时 `tracks`。

## 建议 Cursor 下一阶段实现顺序

### Step 1. 只做状态结构，不做复杂 UI

在 `useCoreApi` 或更合适的 task context hook 中增加：

```ts
historyTracks: TrackHistoryPoint[]
systemEvents: SystemEvent[]
historyLoading: boolean
historyError: string
```

### Step 2. 任务切换时加载上下文数据

当 active task 变化时：

1. 清空实时 `tracks`
2. 调用 `fetchTrackHistory({ taskId, limit, order: 'asc' })`
3. 调用 `fetchSystemEvents({ taskId, limit })`
4. 写入独立历史状态

注意：如果当前没有 active task，则清空历史状态。

### Step 3. UI 最小化展示

先不要做复杂回放。

可以先做到：

- 地图显示历史层，但样式明显区别于实时层
- 日志/事件面板能看到当前任务事件
- 不做拖动时间轴
- 不做复杂回放控制

### Step 4. 再考虑高级能力

后续再做：

- 时间范围筛选
- 历史回放
- 按设备筛选
- 事件联动航迹

## 代码质量评估

当前这轮代码质量没问题，属于干净的小修。

现在真正的风险不是代码，而是 UI 策略如果没定清楚，容易把历史数据和实时数据混在一起。  
这会破坏前面辛苦建立的任务归属语义。

## 给 Cursor 的明确建议

下一轮不要再补零碎 API。  
请先实现一个最小版本的“任务上下文加载”：

1. 实时 `tracks` 和历史 `historyTracks` 分开。
2. active task 切换时清空实时层。
3. active task 切换后加载该任务历史航迹和系统事件。
4. UI 上明确区分实时态和历史态。

如果不确定 UI 样式，先实现状态和最小展示，不要做复杂回放。
