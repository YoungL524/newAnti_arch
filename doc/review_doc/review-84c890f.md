# Review 84c890f

## 结论

这一版基本把 `review-5b44998.md` 里的关键建议都补上了：

1. `tracks(task_id, device_id, ts_ms)` 复合索引已新增。
2. `/api/tracks/history` 的 `order` 参数已显式校验，只允许 `asc/desc`。
3. 前端 `LogEntry` 已保留 `taskId/deviceId`。
4. 新增 `ui/src/lib/tracksApi.ts`，提供 `fetchTrackHistory(...)`。

当前没有发现阻断问题。  
任务驱动上下文的“后端数据归属 + 历史查询 + 前端类型链路”已经基本打通。

## 本轮做对的点

### 1. tracks 历史查询索引补齐

位置：

- `core/AppController.cpp`
- `ensureTracksSchema()`

新增：

```sql
CREATE INDEX IF NOT EXISTS idx_tracks_task_device_ts
ON tracks(task_id, device_id, ts_ms)
```

这正好匹配当前历史查询的主要路径：

```sql
WHERE task_id = ?
  AND device_id = ?
  AND ts_ms >= ?
  AND ts_ms <= ?
ORDER BY ts_ms
```

这是必要补强。

### 2. order 参数校验更明确

位置：

- `core/web/TrackHistoryApiRoutes.cpp`

现在只允许：

```text
asc
desc
```

其他值会返回错误：

```text
order 仅支持 asc 或 desc
```

接口语义更清楚了。

### 3. 前端日志结构化字段没有再丢失

位置：

- `ui/src/lib/protocol.ts`
- `ui/src/hooks/useCoreApi.tsx`

新增：

```ts
export interface LogEntry {
  level: string
  message: string
  timestamp: number
  taskId?: string
  deviceId?: string
}
```

WS log 写入状态时也保留：

- `taskId`
- `deviceId`

这把后端结构化日志真正贯通到了前端状态层。

### 4. 历史航迹前端 helper 已新增

位置：

- `ui/src/lib/tracksApi.ts`

`fetchTrackHistory(...)` 参数和后端接口基本一致：

- `taskId`
- `deviceId`
- `startMs`
- `endMs`
- `limit`
- `order`

这一层先不接 UI 是对的，说明当前仍然在打数据链路，不是在做表面效果。

## 仍建议继续优化的问题

### P2. `fetchTrackHistory()` 可以前端提前校验 taskId

位置：

- `ui/src/lib/tracksApi.ts`

当前 helper 会无条件：

```ts
qs.set('taskId', params.taskId)
```

如果调用方传入空字符串，会发请求给后端，再由后端返回：

```text
taskId 不能为空
```

后端校验已经足够安全。  
但前端 helper 可以提前短路，减少无意义请求：

```ts
if (!params.taskId.trim()) {
  return { ok: false, error: 'taskId 不能为空', tracks: [] }
}
```

这不是阻断问题，只是 API helper 的基本防御。

### P2. `fetchTrackHistory()` 目前直接类型断言，缺少响应字段归一化

位置：

- `ui/src/lib/tracksApi.ts`

当前：

```ts
return { ok: true, tracks: (data.tracks ?? []) as TrackHistoryPoint[] }
```

这在内部项目中可以接受，但如果后端字段缺失或类型异常，前端不会兜底。

建议后续加一个轻量 normalize：

```ts
function normalizeTrackHistoryPoint(raw: any): TrackHistoryPoint | null
```

至少保证：

- `taskId/deviceId` 是 string
- `tsMs/targetId` 是 number
- 坐标和角度字段是 number

这不是当前必须做，但历史数据进入 UI 前最好有一层防护。

### P3. LogEntry 已保留归属字段，但 UI 暂时没有展示/过滤

位置：

- `ui/src/components/panels/BottomPanel.tsx`

这不是本轮问题。  
当前阶段只要求“不丢字段”，已经达成。

后续进入任务切换/历史中心时，可以再考虑：

- 按当前任务过滤日志
- 日志行展示设备名或设备 id
- 点击日志跳转关联航迹/事件

现在不建议提前做复杂 UI。

## 架构判断

任务驱动上下文后端链路已经形成闭环：

```text
Task <-> Device 显式绑定
endpointId -> deviceId
deviceId -> activeTaskId
tracks/logs/system_events 写入 taskId/deviceId
后端 API 按 taskId/deviceId/timeRange 查询历史数据
前端类型层保留 taskId/deviceId
```

这条主线是健康的。

下一步可以进入“任务切换时加载上下文数据”的设计，但要继续坚持：

```text
不要新增 Task <-> Track 人工绑定
不要让 UI 自己推断数据归属
UI 只消费 core 已经归属好的数据
```

## 代码质量评估

当前任务归属相关代码可以评为 `A-`。

加分点：

- 后端查询索引和查询模式已经匹配。
- 前端类型开始承接后端结构化字段。
- 新增 `tracksApi.ts` 没有强行接 UI，节奏克制。

剩余风险：

- 前端 helper 缺少输入校验和响应归一化。
- 任务切换时如何加载历史航迹/事件/日志还未实现。
- UI 是否显示实时数据和历史数据的边界还需要设计清楚。

## 建议 Cursor 下一步

建议进入下一阶段，但仍先做轻量链路，不做复杂界面：

1. `fetchTrackHistory()` 补空 `taskId` 校验。
2. 增加 `fetchSystemEvents(...)` 前端 helper，与后端 `/api/system-events` 对齐。
3. 设计任务切换行为：
   - 激活任务时清空当前实时显示？
   - 是否自动加载该任务最近历史航迹？
   - 实时航迹和历史航迹是否分层显示？
4. 在明确 UI 行为前，不要急着把历史航迹直接塞进当前实时 `tracks` 列表。

重点是先把“任务上下文数据加载策略”定清楚。
