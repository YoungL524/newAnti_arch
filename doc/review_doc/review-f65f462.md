# Review f65f462

## 结论

这一版基本按 `review-0321c54.md` 的方向补齐了任务归属第一阶段的关键缺口：

1. 日志从纯文本上下文升级为结构化 `LogRecord`。
2. `/api/logs` 和 WS log 推送开始返回 `taskId/deviceId/timestamp`。
3. 新增 `/api/system-events`，支持按 `taskId/deviceId` 查询系统事件。
4. 空的 `compile_err.txt` 已删除。

整体方向正确，没有发现需要阻断的严重问题。

## 本轮做对的点

### 1. Logger 已经结构化

位置：

- `core/log/Logger.h`
- `core/log/Logger.cpp`

新增：

```cpp
struct LogRecord {
    QString level;
    QString message;
    QString taskId;
    QString deviceId;
    qint64  timestampMs = 0;
};
```

`Logger::handle()` 会从 `LogContext` 读取：

- `taskId`
- `deviceId`

并写入 recent buffer / external callback。

这一步是对的。  
后续日志不再只能靠字符串解析归属关系。

### 2. `/api/logs` 和 WS log 已经返回结构化字段

位置：

- `core/web/WebApiController.cpp`

`/api/logs` 已改为：

```cpp
Logger::instance().recentLogRecords(100)
```

WS 推送也改为：

```cpp
Logger::instance().setExternalCallback([ws](const LogRecord& rec) { ... })
```

这符合我们要的“日志可按任务/设备追溯”的基础。

### 3. system_events 查询 API 已补上

位置：

- `core/web/SystemEventApiRoutes.h`
- `core/web/SystemEventApiRoutes.cpp`
- `core/AppController.cpp`
- `core/CMakeLists.txt`

新增接口：

```text
GET /api/system-events
GET /api/system-events?taskId=xxx
GET /api/system-events?taskId=xxx&deviceId=yyy
```

SQL 使用参数绑定：

```cpp
task_id = ?
device_id = ?
```

这点是安全的，没有看到明显注入风险。

### 4. WebService GET handler 支持 queryString

位置：

- `core/web/WebService.h`
- `core/web/WebService.cpp`

`handleGet` 新增 `queryString` 参数后，现有 GET route 都已经同步改签名，没有发现漏改。

## 仍建议继续优化的问题

### P2. system_events 按 deviceId 单独查询时缺少索引

位置：

- `core/AppController.cpp`
- `ensureSystemEventsSchema()`
- `core/web/SystemEventApiRoutes.cpp`

现在已有索引：

```sql
idx_sysevt_ts
idx_sysevt_cat
idx_sysevt_task_ts(task_id, ts_ms)
```

但 `/api/system-events?deviceId=xxx` 会生成：

```sql
WHERE device_id = ?
ORDER BY ts_ms DESC
LIMIT 500
```

如果未来系统事件数量变多，单独按设备查会比较吃力。

建议 Cursor 补：

```sql
CREATE INDEX IF NOT EXISTS idx_sysevt_device_ts
ON system_events(device_id, ts_ms)
```

如果常用组合查询，也可以补：

```sql
CREATE INDEX IF NOT EXISTS idx_sysevt_task_device_ts
ON system_events(task_id, device_id, ts_ms)
```

这不是当前阻断问题，但属于数据归属链路的基础设施优化。

### P2. `/api/system-events` 建议支持 category 和 limit

位置：

- `core/web/SystemEventApiRoutes.cpp`

当前固定：

```sql
ORDER BY ts_ms DESC LIMIT 500
```

第一阶段可以接受。  
但后面历史中心/运维告警会自然需要：

```text
category=error
limit=100
```

建议下一步支持：

```text
GET /api/system-events?taskId=xxx&deviceId=yyy&category=error&limit=100
```

注意 limit 要做范围限制，例如：

```text
1 <= limit <= 1000
```

### P3. LogRecord 的时间戳和文件日志行时间戳不是同一次采样

位置：

- `core/log/Logger.cpp`
- `Logger::formatLine()`
- `Logger::handle()`

现在文件日志时间戳在 `formatLine()` 里生成，结构化 `LogRecord.timestampMs` 在 `handle()` 里重新取一次。

二者最多只差几毫秒，问题不大。  
但如果后续要精确关联文件日志和 API 日志，最好让 `handle()` 先采样一次 `timestampMs`，然后传给：

- `formatLine()`
- `LogRecord`

这属于一致性细节，不需要立刻改。

### P3. recentLogs 兼容接口仍保留，但后续可以逐步下线

位置：

- `core/log/Logger.h`
- `core/log/Logger.cpp`

`recentLogs()` 现在只是从 `recentLogRecords()` 转回旧的 pair。  
这对兼容没问题。

后续如果没有调用点，可以考虑删除旧接口，减少两套日志模型并存。

## 架构判断

任务驱动数据归属第一阶段现在已经比较完整：

```text
Task <-> Device 显式绑定
endpointId -> deviceId -> activeTaskId 自动归属
tracks 写入 task_id/device_id
TrackPoint 携带 taskId/deviceId
system_events 携带 task_id/device_id
logs 携带 taskId/deviceId
```

这条线已经符合我们之前的设计，没有走偏。

下一步可以进入第二阶段：

1. 历史数据查询 API 按 `taskId/deviceId` 拉航迹。
2. 前端切换任务时，只展示当前任务上下文的数据。
3. UI 展示日志/事件时优先使用结构化 `taskId/deviceId`，不要解析 message 字符串。

## 代码质量评估

当前代码质量继续提升，可以认为任务归属链路已经进入 `B+ ~ A-` 区间。

加分点：

- 日志上下文没有停留在文本层，而是进入 API/WS 结构化输出。
- system_events 查询独立成 `SystemEventApiRoutes`，没有继续塞进 WebApiController。
- `WebService` GET queryString 支持补得比较克制，现有 route 都同步改了。

主要扣分点：

- system_events 查询索引还不完整。
- 查询 API 还比较基础，缺少 category/limit 等常用参数。
- Logger 时间戳采样还有轻微不一致。

## 建议 Cursor 下一步

优先级建议：

1. 给 `system_events` 补 `device_id + ts_ms` 索引。
2. `/api/system-events` 增加 `category` 和受限 `limit` 参数。
3. 统一 `Logger::handle()` 的时间戳采样。
4. 开始设计历史航迹查询 API：按 `taskId/deviceId/timeRange` 查询 `tracks`。

这一轮不用回头大改任务归属模型，主线是对的。
