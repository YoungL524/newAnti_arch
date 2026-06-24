# Review 6bb00e1

## 结论

这一版和 Cursor 总结一致：只补前端 API helper，没有提前实现日志 UI 过滤，也没有把历史航迹直接塞进实时 `tracks`。

本轮完成了：

1. `fetchTrackHistory()` 增强：空 `taskId` 前端短路、响应归一化。
2. 新增 `fetchSystemEvents()`，对齐 `/api/system-events`。
3. 暂不做任务切换 UI 行为，节奏是对的。

当前没有阻断问题。

## 本轮做对的点

### 1. `fetchTrackHistory()` 增加前端防御

位置：

- `ui/src/lib/tracksApi.ts`

现在空 `taskId` 会直接返回：

```ts
{ ok: false, error: 'taskId 不能为空', tracks: [] }
```

这很好。  
后端仍然保留强校验，前端只是减少无意义请求。

### 2. 历史航迹响应已归一化

位置：

- `ui/src/lib/tracksApi.ts`
- `normalizeTrackHistoryPoint()`

现在不会再直接：

```ts
data.tracks as TrackHistoryPoint[]
```

而是逐条检查并转换字段。  
这对后面历史数据进入 UI 很重要。

### 3. `fetchSystemEvents()` 已新增

位置：

- `ui/src/lib/systemEventsApi.ts`

参数与后端接口基本一致：

- `taskId`
- `deviceId`
- `category`
- `startMs`
- `endMs`
- `limit`

返回也做了归一化，结构清楚。

### 4. 没有提前做复杂 UI

这一点值得肯定。  
当前还没定清楚：

- 激活任务时是否清空实时显示
- 是否自动加载历史航迹
- 实时航迹与历史航迹是否分层显示

所以现在只补 helper，不碰 UI，是正确节奏。

## 仍建议继续优化的问题

### P2. `TrackHistoryPoint.deviceId` 类型必填，但 normalize 允许空字符串

位置：

- `ui/src/lib/tracksApi.ts`

当前类型定义：

```ts
export interface TrackHistoryPoint {
  taskId: string
  deviceId: string
  ...
}
```

但归一化逻辑只校验了：

```ts
if (!taskId) return null
```

没有校验 `deviceId`。  
这会导致一种不一致：类型上 `deviceId` 是可靠字段，但运行时可能是空字符串。

建议 Cursor 二选一：

1. 如果历史航迹必须属于设备，则补：

```ts
if (!deviceId) return null
```

2. 如果允许历史航迹没有设备归属，则把类型改为：

```ts
deviceId?: string
```

按当前架构，我更建议方案 1。  
因为航迹本来就应该来自业务设备，缺 `deviceId` 的历史航迹不应该进入正常 UI 链路。

### P3. `num/str` 在两个 API helper 中重复

位置：

- `ui/src/lib/tracksApi.ts`
- `ui/src/lib/systemEventsApi.ts`

两个文件都定义了：

```ts
function num(...)
function str(...)
```

当前重复很小，可以接受。  
如果后面还会继续增加历史日志、任务回放等 API helper，可以考虑抽到：

```text
ui/src/lib/apiNormalize.ts
```

不建议现在为了两处重复就重构。

### P3. `fetchSystemEvents()` 可以考虑 trim deviceId/category

位置：

- `ui/src/lib/systemEventsApi.ts`

当前只对 `taskId` 做了：

```ts
params.taskId?.trim()
```

`deviceId/category` 没有 trim。  
这不是大问题，但参数处理可以统一。

## 架构判断

当前任务驱动上下文的数据链路已经很清楚：

```text
后端归属字段：taskId/deviceId
后端查询接口：tracks history / system events
前端 API helper：fetchTrackHistory / fetchSystemEvents
前端状态层：LogEntry 保留 taskId/deviceId
```

下一步真正要讨论的是“任务切换加载上下文”的产品/交互策略，而不是继续补基础设施。

建议先定这三个问题：

1. 激活任务时，是否清空当前实时显示？
2. 是否自动加载该任务最近历史航迹？
3. 实时航迹和历史航迹是否分层显示？

我的倾向：

- 实时航迹和历史航迹不要混在同一个 `tracks` 状态里。
- 任务切换时可以清空实时显示层，但历史层单独加载。
- UI 上应明确区分“实时态”和“历史态”，避免用户把回放点当成当前实时目标。

## 代码质量评估

这一版质量稳定，属于小而正确的补链路提交。

加分点：

- 没有抢跑 UI。
- API helper 有错误返回和响应归一化。
- 和后端接口字段基本一致。

剩余风险：

- `deviceId` 类型/归一化不完全一致。
- 任务切换策略还没定，下一步如果直接写 UI 容易混乱。

## 建议 Cursor 下一步

1. 先补 `normalizeTrackHistoryPoint()` 的 `deviceId` 校验。
2. 然后暂停代码实现，先把“任务切换加载上下文策略”写成简短设计：
   - 实时层数据如何处理
   - 历史层数据如何加载
   - 日志/事件如何过滤
   - 用户如何区分实时和历史
3. 策略确认后，再做 UI。

这一步最好先设计，不要边写边猜。
