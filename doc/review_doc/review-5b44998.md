# Review 5b44998

## 结论

这一版已经把“任务驱动上下文”的后端历史查询主线打通了：

1. `/api/system-events` 增加 `startMs/endMs`。
2. 新增 `TrackHistoryApiRoutes`。
3. `/api/tracks/history` 支持按 `taskId/deviceId/startMs/endMs/limit/order` 查询历史航迹。
4. `taskId` 必填，避免无意全表扫。

整体方向正确，没有发现需要推翻的设计问题。  
但还有两个必须补的细节：`tracks` 查询索引还不完全匹配，前端日志类型没有承接结构化 `taskId/deviceId`。

## 本轮做对的点

### 1. `/api/system-events` 时间范围补齐

位置：

- `core/web/SystemEventApiRoutes.cpp`

现在支持：

```text
startMs
endMs
```

并且处理了：

- 非法数字
- `startMs > endMs`

这符合历史查询的基础要求。

### 2. 历史航迹 API 已独立成 route module

位置：

- `core/web/TrackHistoryApiRoutes.h`
- `core/web/TrackHistoryApiRoutes.cpp`
- `core/AppController.cpp`
- `core/CMakeLists.txt`

没有继续塞进 `WebApiController`，这是对的。

当前接口：

```text
GET /api/tracks/history?taskId=xxx
GET /api/tracks/history?taskId=xxx&deviceId=yyy
GET /api/tracks/history?taskId=xxx&deviceId=yyy&startMs=...&endMs=...&limit=...&order=...
```

基本符合预期。

### 3. taskId 必填

位置：

- `core/web/TrackHistoryApiRoutes.cpp`

代码中明确拒绝空 `taskId`：

```cpp
if (taskId.isEmpty())
    return fail(QStringLiteral("taskId 不能为空"));
```

这很好。  
历史航迹数据量会很大，不能允许默认全表扫。

### 4. SQL 使用参数绑定

位置：

- `core/web/TrackHistoryApiRoutes.cpp`

查询条件都通过 `QVariantList params` 绑定，没有看到明显 SQL 注入风险。

`order` 也没有直接拼接用户原文，而是转成 bool：

```cpp
const bool desc = (orderRaw == "desc");
```

这一点是安全的。

## 需要继续处理的问题

### P1. tracks 缺少匹配历史查询的复合索引

位置：

- `core/AppController.cpp`
- `ensureTracksSchema()`
- `core/web/TrackHistoryApiRoutes.cpp`

当前已有索引：

```sql
idx_tracks_task_ts(task_id, ts_ms)
idx_tracks_task_device(task_id, device_id)
```

但历史查询最常见的 SQL 是：

```sql
WHERE task_id = ?
  AND device_id = ?
  AND ts_ms >= ?
  AND ts_ms <= ?
ORDER BY ts_ms ASC/DESC
LIMIT ?
```

当前 `idx_tracks_task_device(task_id, device_id)` 缺少 `ts_ms`，不能很好覆盖时间范围和排序。

建议 Cursor 补：

```sql
CREATE INDEX IF NOT EXISTS idx_tracks_task_device_ts
ON tracks(task_id, device_id, ts_ms)
```

这个应作为下一轮优先修。  
否则数据量上来后，历史查询会变慢。

### P2. 前端日志类型没有承接结构化 taskId/deviceId

位置：

- `ui/src/lib/protocol.ts`
- `ui/src/hooks/useCoreApi.tsx`

后端现在 `/api/logs` 和 WS log 已经返回：

```json
{
  "level": "...",
  "message": "...",
  "timestamp": 123,
  "taskId": "...",
  "deviceId": "..."
}
```

但前端类型仍然是：

```ts
logEntries: { level: string; message: string; timestamp: number }[]
```

WS 收到 log 时也只保存：

```ts
{ level: msg.level, message: msg.message, timestamp: msg.timestamp }
```

这会把后端刚补上的结构化归属字段丢掉。

建议 Cursor 修改：

1. 在 `protocol.ts` 定义统一日志类型，例如：

```ts
export interface LogEntry {
  level: string
  message: string
  timestamp: number
  taskId?: string
  deviceId?: string
}
```

2. `LogsResponse.logs` 使用 `LogEntry[]`。
3. `WsLogMessage` 继承或复用 `LogEntry` 字段。
4. `CoreState.logEntries` 改成 `LogEntry[]`。
5. WS log 保存时保留：

```ts
taskId: msg.taskId,
deviceId: msg.deviceId,
```

暂时不要求 UI 展示这两个字段，但不能在状态层丢掉。

### P2. 历史航迹 API 还没有前端调用封装

位置：

- `ui/src/lib`
- `ui/src/hooks`

当前后端已有 `/api/tracks/history`，但前端还没有看到对应 API helper。

建议先加一个轻量封装，不做 UI：

```ts
fetchTrackHistory({
  taskId,
  deviceId,
  startMs,
  endMs,
  limit,
  order,
})
```

这样后面历史中心/任务切换回放可以直接复用。

### P3. `/api/tracks/history` 的 order 非 desc 时默认 asc，可以显式校验

位置：

- `core/web/TrackHistoryApiRoutes.cpp`

当前逻辑：

```cpp
const bool desc = (orderRaw == "desc");
```

也就是说：

```text
order=abc
```

会被当作 asc。

这不是安全问题，但接口语义略松。  
可以考虑：

- 空值默认 asc
- 只允许 `asc/desc`
- 其他值返回错误

不是必须，但接口会更清晰。

## 架构判断

这一版标志着后端历史链路基本闭合：

```text
实时数据进入 core
  -> endpointId 映射 deviceId
  -> active task 映射 taskId
  -> tracks/system_events/logs 带 taskId/deviceId 沉淀
  -> 后端 API 可按 taskId/deviceId/timeRange 查询
```

这已经符合我们想要的任务驱动上下文模型。

接下来不要再发明新的绑定关系。  
仍然保持：

```text
显式绑定只有 Task <-> Device
数据归属全部由写入时自动推导
```

## 代码质量评估

当前后端任务归属链路质量已经接近 `A-`。

加分点：

- route module 拆分合理。
- `taskId` 必填，避免历史航迹全表扫。
- 参数校验和 SQL 绑定基本到位。
- system_events 和 tracks 的查询方向统一了。

扣分点：

- tracks 查询索引还差一个关键复合索引。
- 前端状态层仍丢弃结构化日志归属字段。
- 历史航迹 API 还没有前端 helper。

## 建议 Cursor 下一步

优先级：

1. 补 `idx_tracks_task_device_ts(task_id, device_id, ts_ms)`。
2. 前端 `LogEntry` 类型保留 `taskId/deviceId`。
3. 增加 `fetchTrackHistory(...)` 前端 API helper。
4. 暂时不做复杂 UI，只把数据链路和类型链路打通。

做完这些后，就可以进入“任务切换时加载历史航迹/事件/日志”的前端行为设计。
