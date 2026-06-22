# Review acca6ab

## 结论

这一版把任务管理开始后端化了，方向是对的：`TaskService` 负责任务台账，`TaskApiRoutes` 独立注册任务 API，前端优先通过 `/api/tasks` 加载任务，再 fallback 到 localStorage。整体上符合“业务 source of truth 逐步后端化”的路线。

但本轮需要优先处理一个运行时生命周期问题：UI 先启动、core 被关闭后重启，都不应该导致 core 退出。Vite 里的 `ECONNREFUSED/ECONNRESET` 本身只是开发代理在后端不可用时的噪声；真正不合理的是 core 面对 UI 的轮询、代理重连、半连接关闭时不够健壮。

## P0 / P1 必改

### 1. WebService 需要补齐 HTTP 连接异常生命周期

现象：

- UI 先启动时，Vite 代理请求 `/api/status`、`/api/logs`、`/api/devices`、`/api/tasks` 会报 `ECONNREFUSED`，这是 core 还没监听端口时的正常表现。
- core 关闭或重启时，已有代理连接会报 `ECONNRESET`，也是开发环境常见表现。
- 但 core 重新启动后被 UI 探测触发退出，这不是正常架构行为，必须修。

当前代码风险点：

- `core/web/WebService.cpp` 的 HTTP socket 只连接了 `readyRead` 和 `disconnected`，没有连接 `QTcpSocket::errorOccurred`。
- `sendJson()`、`sendError()`、`serveIndex()`、`serveStaticFile()` 直接 `sock->write(...)`，没有检查 socket 是否还有效、是否仍处于可写状态。
- `onHttpReadyRead()` 使用 `m_buffers[sock]`，如果 socket 已经被清理或 keep-alive 请求后 buffer 被 remove，下一次 readyRead 会重新创建空 buffer，生命周期语义不清晰。
- `dispatchRequest()` 遇到 malformed request 会先写 400 再断开；如果对端已经 reset，写入失败路径没有集中处理。

建议 Cursor 修改：

1. 在 `onHttpConnection()` 中连接 `QTcpSocket::errorOccurred`，统一走一个 `cleanupHttpSocket(QTcpSocket*)`。
2. 抽出 `cleanupHttpSocket()`，同时处理 idle timer、buffer、signal disconnect、`deleteLater()`，避免 `onHttpDisconnected()`、超时、错误路径重复清理。
3. `onHttpReadyRead()` 开头用 `m_buffers.find(sock)` 判断 socket 是否仍被管理；未管理则直接返回，不要用 `operator[]` 隐式创建。
4. keep-alive 请求处理完即使 raw 为空，也应该保留该 socket 的 buffer 条目为空，而不是 `m_buffers.remove(sock)`；否则“连接仍活着，但服务端状态已删除”。
5. 所有响应写入前增加 `isSocketWritable(sock)` 判断，至少要求 `sock && sock->state() == QAbstractSocket::ConnectedState`。
6. 对 `write()` 的返回值做基本判断；失败时清理连接即可，不要把客户端断开升级成服务端异常。

启动顺序结论：

- 开发期允许 `core -> UI` 启动更安静，但不应该成为系统约束。
- 架构上必须支持 `UI -> core`、core 重启、UI 自动恢复。
- UI 不可用时 core 也必须能独立运行；core 不应该因为 UI 代理轮询或断连退出。

### 2. Vite 代理错误可以降噪，但不能掩盖 core 稳定性问题

`ui/vite.config.ts` 现在仍是：

```ts
proxy: {
  '/api': 'http://localhost:8088',
}
```

这会在 core 未启动时持续打印代理错误。它不是业务 bug，但会干扰定位。

建议：

- 可以在 Vite proxy 的 `configure(proxy)` 里监听 `error`，对 `ECONNREFUSED` / `ECONNRESET` 做简化日志或静默。
- UI 页面层面应该显示“后端未连接 / 正在重连”的状态，而不是只在控制台刷错误。
- 但先修 core socket 生命周期；Vite 降噪是体验优化，不是根因修复。

## 任务后端化问题

### 3. TaskService 写操作缺少事务，可能留下半成功数据

`TaskService::create()` 先插入 `tasks`，再 `saveDeviceIds()`。如果设备关系写入失败，函数返回失败，但任务主表已经存在。

`TaskService::update()` 也类似：先更新任务主表，再保存设备关系；后半段失败会留下部分更新状态。

建议：

- 给 `DataStore` 补 `transaction/commit/rollback` 能力，或在 `TaskService` 内使用 SQLite transaction。
- `create()`、`update()`、`activate()`、`remove()` 都应以事务包住。
- `activate()` 中“其他 Active 任务置 Completed + 当前任务置 Active”必须是原子操作，否则可能出现多个 Active 或全部非预期 Completed。

### 4. 任务状态机边界还不够明确

当前状态大致有 `Draft / Active / Completed / Archived`，但服务端允许：

- archived/draft 直接 `complete()`
- completed 再 `activate()`
- 任意状态 `archive()`

这些未必全错，但必须成为明确业务规则。建议先定最小状态机：

- `Draft -> Active`
- `Active -> Completed`
- `Completed -> Archived`
- `Draft -> Archived` 可选，用于取消未执行任务
- `Archived` 终态，不允许再 activate/complete/update 关键字段

Cursor 应把非法状态迁移返回明确错误，例如 `invalid task status transition`，而不是统一 `操作失败`。

### 5. task_devices 缺少外键约束

`task_devices(task_id, device_id)` 目前只有联合主键，没有外键。

建议：

- `task_id` 外键引用 `tasks(id)`，删除任务时 cascade。
- `device_id` 是否强外键引用设备表，需要结合当前 `DeviceRegistry` 表结构决定；如果暂时不加外键，也要在 TaskService 保存 deviceIds 时校验设备存在。

这条和之前“设备 source of truth 在后端”一致：任务绑定的设备 id 不能只是前端传来的字符串。

## 前端问题

### 6. Draft 任务被前端映射成 active

`ui/src/lib/tasksApi.ts`：

```ts
function mapStatus(status: string): TaskStatus {
  switch (status) {
    case 'Active': return 'active'
    case 'Completed': return 'completed'
    case 'Archived': return 'archived'
    default: return 'active'
  }
}
```

后端默认创建 `Draft`，但前端 `TaskStatus` 没有 draft，未知状态会被当成 active。现在创建流程可能马上 activate，所以不一定立刻暴露，但架构上是错的。

建议二选一：

1. 前端 `TaskStatus` 增加 `'draft'`，UI 按 draft 展示。
2. 如果业务决定任务创建即激活，则后端 create API 不要默认 Draft，而是明确由 create-and-activate 用例处理。

我更建议保留 Draft，因为任务后端化后，创建、配置设备、激活任务会是天然的三个阶段。

### 7. PUT /api/tasks/{id} 不能清空字符串字段

`TaskApiRoutes.cpp` 中 update 逻辑用 “空字符串则保留旧值”：

```cpp
if (t.description.isEmpty()) t.description = existing->description;
if (t.operatorName.isEmpty()) t.operatorName = existing->operatorName;
```

这样前端无法主动清空描述或操作员。

建议：

- 用 `body.contains("description")` 判断字段是否由调用方提供。
- 字段存在但值为空，应按清空处理。
- `deviceIds` 已经用了 contains 判断，可以沿用同一风格。

## 架构复盘

### 当前进度

Milestone 1 设备后端化已基本进入正确轨道：设备台账由后端管理，前端使用后端确认后的 DTO。

本轮开始推进 Milestone 4 任务后端化：这是正确的下一步，因为任务是设备、航迹、日志、运行状态的业务容器。任务一旦稳定，后续历史数据、航迹回放、任务日志都会有归属。

### 下一步建议顺序

1. 先修 WebService 生命周期问题，确保 UI/core 可任意启动、core 可重启恢复。
2. 补 TaskService 事务和状态机，让任务成为可信业务边界。
3. 修前端 Draft 映射和后端 PUT 清空字段问题。
4. 再推进任务与设备绑定的后端校验：任务绑定的 deviceIds 必须存在于 DeviceRegistry。
5. 之后再进入 TrackDisplayManager / 固定航迹协议 / bus 上行链路，不要在任务状态未稳定前继续扩散。

## 给 Cursor 的最小实现清单

1. `WebService` 增加 HTTP socket error handler、统一 cleanup、write 前状态检查。
2. keep-alive socket 的 buffer 生命周期改为“连接存在则条目存在”，不要处理完空 buffer 就 remove。
3. Vite proxy 对 `ECONNREFUSED/ECONNRESET` 降噪，并在 UI 层增加后端连接状态提示。
4. `TaskService` 为 create/update/activate/remove 加事务。
5. `TaskService` 落实任务状态迁移规则。
6. `tasksApi.ts` 和 `taskTypes.ts` 支持 draft，或明确取消 Draft 语义。
7. `TaskApiRoutes.cpp` 的 PUT 使用字段存在性判断，不要用空字符串判断是否保留旧值。

