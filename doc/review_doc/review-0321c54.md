# Review 0321c54

## 结论

这一版正式开始实现“任务驱动运行上下文 / 数据归属”后端链路，整体方向是对的。

最重要的是：代码没有引入 `Task <-> Track`、`Task <-> Log` 之类的人工绑定表，而是继续坚持：

```text
Task <-> Device 是唯一显式绑定关系
endpointId -> deviceId -> activeTaskId 是数据归属推导链路
```

这一点符合我们之前讨论的架构原则。

当前没有发现需要推翻的设计问题，但有两个需要 Cursor 下一步补齐的点：日志归属还停留在文本层，系统事件/日志查询还没有真正变成可按 `taskId + deviceId` 追溯。

## 本轮做对的点

### 1. TaskService 增加 active task 热路径查询

位置：

- `core/business/TaskService.h`
- `core/business/TaskService.cpp`

新增能力：

- `getActiveTask()`
- `isDeviceInActiveTask(deviceId)`
- `resolveTaskIdForDevice(deviceId)`

并且保留了旧接口：

- `resolveActiveTaskIdForDevice(deviceId)`，标记为 deprecated

这符合预期。  
热路径不需要每条数据都查库，而是读 `m_activeTaskCache`。

### 2. 航迹归属链路基本正确

位置：

- `core/processor/RadarDataProcessor.cpp`
- `core/business/TrackTypes.h`

现在链路是：

```text
endpointId
  -> DeviceRegistry::cachedDeviceIdForEndpoint(endpointId)
  -> TaskService::resolveTaskIdForDevice(deviceId)
  -> TrackPoint.taskId / tracks.task_id
```

这是正确方向。

`TrackPoint` 也新增了：

```cpp
QString taskId;
```

这样后续 TrackDisplayManager / Web snapshot 如果需要按任务过滤，也有字段可用。

### 3. system_events 表开始支持 task/device 字段

位置：

- `core/AppController.cpp`
- `ensureSystemEventsSchema()`
- `insertSystemEvent()`

新增字段：

- `task_id`
- `device_id`

并且补了：

- `idx_sysevt_task_ts`

这一步是必要的。  
系统事件未来才能按任务追溯。

### 4. 通信错误能自动归属任务和设备

位置：

- `core/AppController.cpp`
- `AppController::onCommError()`

现在通信错误会走：

```text
endpointId -> deviceId -> taskId
```

然后写入：

```cpp
insertSystemEvent("error", endpointId, message, deviceId, taskId);
```

这是符合业务归属模型的。

## 需要继续处理的问题

### P1. 日志归属目前只是拼到了文本里，不是真正的结构化归属

位置：

- `core/log/LogContext.h`
- `core/log/LogContext.cpp`
- `core/log/Logger.cpp`
- `core/web/WebApiController.cpp`

现状：

`LogContext::Scope` 用 thread_local 保存：

- `taskId`
- `deviceId`

`Logger::formatLine()` 会把它们拼到日志行末尾：

```text
[task=xxx device=yyy]
```

这对人工看文件日志有帮助，但还不等于“日志可按 taskId + deviceId 追溯”。

原因：

1. `Logger::recentLogs()` 仍然只保存：

```cpp
QVector<QPair<QString, QString>>
```

也就是：

- level
- message

没有结构化的 `taskId/deviceId`。

2. `/api/logs` 返回的仍然是：

```json
{
  "level": "...",
  "message": "...",
  "timestamp": ...
}
```

没有：

```json
{
  "taskId": "...",
  "deviceId": "..."
}
```

3. `Logger::setExternalCallback()` 推送给 WS 的也是原始 `msg`，不是带上下文的结构化日志。

所以当前日志归属只能靠字符串解析，后续如果要按任务筛选日志会很痛苦。

建议 Cursor 下一步修改：

1. 定义结构化日志记录，例如：

```cpp
struct LogRecord {
    QString level;
    QString message;
    QString taskId;
    QString deviceId;
    qint64 timestampMs = 0;
};
```

2. `Logger::recentLogs()` 改为返回 `QVector<LogRecord>`，或者新增 `recentLogRecords()` 兼容旧接口。
3. `Logger::handle()` 在记录 recent buffer 和调用 external callback 时，一并带上 `LogContext::currentTaskId()` / `currentDeviceId()`。
4. `/api/logs` 和 WS log 推送都返回结构化字段：

```json
{
  "level": "WARN",
  "message": "...",
  "taskId": "task_xxx",
  "deviceId": "radar_001",
  "timestamp": 123456789
}
```

注意：文件日志可以继续保留文本后缀，方便人眼排查；但 API/WS 必须结构化。

### P2. system_events 已写入 task/device，但还缺查询入口

位置：

- `core/AppController.cpp`
- `core/web/WebApiController.cpp`

现状：

`system_events` 表已经有：

- `task_id`
- `device_id`

但当前 Web API 里没有看到按任务查询 system_events 的接口。

这意味着“数据已经沉淀了归属字段”，但前端/历史中心还没法使用。

建议 Cursor 下一步增加后端查询能力，先不用做 UI：

```text
GET /api/system-events?taskId=xxx
GET /api/system-events?taskId=xxx&deviceId=yyy
```

返回字段至少包括：

- `tsMs`
- `category`
- `source`
- `detail`
- `taskId`
- `deviceId`

这一步可以先做后端 API，不做页面。

### P2. active task 缓存需要明确初始化时机

位置：

- `core/business/TaskService.cpp`
- `TaskService::refreshActiveTaskCache()`
- `core/AppController.cpp`

`TaskService` 已经在 create/update/activate/complete/archive/remove 等路径刷新缓存，方向正确。

需要确认一点：系统启动时，`TaskService` 构造后是否一定调用了 `refreshActiveTaskCache()`。

如果启动时数据库里已经存在 active task，但缓存没有刷新，那么第一批航迹会拿不到 `taskId`。

建议 Cursor 检查并保证：

```cpp
TaskService 初始化 / ensureSchema 完成后，必须 refreshActiveTaskCache()
```

如果已经有，就不需要改；如果没有，要补上。

### P3. `compile_err.txt` 是空文件，建议不要长期留在仓库

位置：

- `compile_err.txt`

这个文件长度为 0。  
如果只是临时记录编译错误，建议删除；如果要保留，则至少写明用途。

这不是功能问题，但空文件会增加仓库噪音。

## 架构判断

这一版符合任务驱动上下文的核心设计：

```text
显式绑定：Task <-> Device
自动归属：endpointId/sourceType -> deviceId -> activeTaskId
```

目前后端链路已经开始成型：

```text
RadarDataProcessor:
endpointId -> deviceId -> taskId -> tracks.task_id / TrackPoint.taskId

AppController:
comm error endpointId -> deviceId -> taskId -> system_events.task_id

Logger:
LogContext scope -> 文本日志附带 task/device
```

但日志这条线还没有完全达到“可查询、可过滤、可追溯”的标准。  
下一轮应该优先把日志从文本上下文升级成结构化上下文。

## 下一步建议 Cursor 做什么

优先级如下：

1. 把 Logger recent buffer / WS log / `/api/logs` 改成结构化 `LogRecord`，带 `taskId/deviceId/timestampMs`。
2. 增加 system_events 查询 API，支持 `taskId/deviceId` 过滤。
3. 确认 TaskService 启动时一定刷新 active task cache。
4. 删除或说明 `compile_err.txt`。

这几项做完后，任务驱动的“数据归属”第一阶段就比较扎实了，可以再进入历史数据查询和前端任务切换过滤。
