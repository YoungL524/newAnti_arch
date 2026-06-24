# Review 662f1df

## 结论

这一版是一次干净的精修提交，基本落实了 `review-f65f462.md` 的建议：

1. `system_events` 补了 `device_id + ts_ms` 和 `task_id + device_id + ts_ms` 索引。
2. `/api/system-events` 支持 `category` 和受限 `limit` 参数。
3. `Logger` 文件日志和 `LogRecord` 使用同一个 `timestampMs`。

当前没有发现需要阻断的问题。  
任务归属第一阶段可以认为基本完成，下一步应该进入“历史航迹查询 API”。

## 本轮做对的点

### 1. system_events 索引补齐

位置：

- `core/AppController.cpp`
- `ensureSystemEventsSchema()`

新增：

```sql
CREATE INDEX IF NOT EXISTS idx_sysevt_device_ts
ON system_events(device_id, ts_ms)
```

以及：

```sql
CREATE INDEX IF NOT EXISTS idx_sysevt_task_device_ts
ON system_events(task_id, device_id, ts_ms)
```

这对后续按任务/设备追溯事件是必要的。

### 2. `/api/system-events` 查询参数更完整

位置：

- `core/web/SystemEventApiRoutes.cpp`

现在支持：

```text
taskId
deviceId
category
limit
```

`limit` 做了范围限制：

```cpp
1 <= limit <= 1000
```

这很好，避免前端一次性拉太多数据。

### 3. Logger 时间戳已经统一采样

位置：

- `core/log/Logger.cpp`
- `Logger::handle()`
- `Logger::formatLine()`

现在 `handle()` 先采样：

```cpp
const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
```

然后同时用于：

- 文件日志行
- `LogRecord.timestampMs`

这让文件日志和 API/WS 日志更容易对齐。

## 仍建议继续优化的问题

### P2. `/api/system-events` 还缺时间范围参数

位置：

- `core/web/SystemEventApiRoutes.cpp`

当前接口只能按：

```text
taskId/deviceId/category/limit
```

过滤，然后按 `ts_ms DESC` 取最近 N 条。

这对实时页面够用，但对历史中心不够。  
历史查询通常需要：

```text
startMs
endMs
```

建议 Cursor 后续支持：

```text
GET /api/system-events?taskId=xxx&startMs=...&endMs=...
```

注意：

1. `startMs/endMs` 必须按整数解析。
2. 非法值可以忽略或返回错误，但建议返回明确错误。
3. 如果 `startMs > endMs`，应返回错误。

这不是当前阻断点，但历史中心迟早需要。

### P2. 还没有历史航迹查询 API

位置：

- 当前未发现 `/api/tracks/history` 或类似接口。

现在数据归属链路已经具备：

- `tracks.task_id`
- `tracks.device_id`
- `tracks.ts_ms`

下一步应该把它暴露成后端查询接口。

建议 Cursor 新增一个独立 route module，不要继续塞进 `WebApiController`：

```text
core/web/TrackHistoryApiRoutes.h
core/web/TrackHistoryApiRoutes.cpp
```

建议接口：

```text
GET /api/tracks/history?taskId=xxx
GET /api/tracks/history?taskId=xxx&deviceId=yyy
GET /api/tracks/history?taskId=xxx&deviceId=yyy&startMs=...&endMs=...&limit=...
```

返回字段应至少包括：

- `taskId`
- `deviceId`
- `tsMs`
- `sceneNum`
- `targetId`
- `range`
- `azimuth`
- `elevation`
- `radialSpeed`
- `altitude`
- `x/y/z`

注意：

1. `taskId` 应该是必填，避免无意中全表扫历史航迹。
2. `limit` 要限制范围，例如 `1..5000`。
3. 查询需要使用参数绑定。
4. 建议默认按 `ts_ms ASC` 返回，方便前端回放；如果给实时列表用，可以额外支持 `order=desc`。

### P3. `/api/system-events` 的 `category` 可以考虑白名单

位置：

- `core/web/SystemEventApiRoutes.cpp`

当前 category 使用参数绑定，所以没有 SQL 注入问题。  
但如果以后 category 是固定枚举，可以考虑只允许：

```text
info / warn / error / operation / system
```

或者先保持开放，等事件分类稳定后再收紧。

这不是当前必须修改的问题。

## 架构判断

任务归属第一阶段现在已经比较扎实：

```text
设备归属：endpointId -> deviceId
任务归属：deviceId -> activeTaskId
航迹沉淀：tracks.task_id / tracks.device_id
日志沉淀：LogRecord.taskId / LogRecord.deviceId
事件沉淀：system_events.task_id / system_events.device_id
```

这条线已经符合我们之前定下的模型：

```text
SystemBus 负责数据去哪儿
TaskContext 负责数据属于谁
```

当前不要再继续在日志/事件上细碎打磨了。  
下一步应该让“已经沉淀的数据”可以被历史中心查询出来。

## 代码质量评估

当前质量继续稳定，属于 `A-` 附近的小步精修。

加分点：

- 精修范围克制，没有引入额外复杂度。
- `limit` 做了边界控制。
- 索引设计开始跟查询模式匹配。
- 日志时间戳一致性已补上。

剩余风险：

- 历史航迹 API 还没开始。
- system_events 没有时间范围查询。
- 前端任务切换过滤还没有进入实现阶段。

## 建议 Cursor 下一步

优先做：

1. 新增 `TrackHistoryApiRoutes`，按 `taskId/deviceId/timeRange/limit` 查询 `tracks`。
2. 给 `/api/system-events` 补 `startMs/endMs`。
3. 先不做 UI，先用后端 API 把历史数据链路打通。

做到这里，任务驱动上下文的后端闭环就会非常清楚。
