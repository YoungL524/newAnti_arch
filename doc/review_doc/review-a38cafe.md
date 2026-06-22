# Review a38cafe

## 结论

这一版基本完成了 `review-acca6ab` 的大部分修复：任务后端化继续向正确方向推进，`TaskService` 增加事务、任务状态机、设备 id 校验，前端也补了 `draft` 状态和后端连接状态提示。整体比上一版稳很多。

但还有几个必须继续收口的点，尤其是 WebService 的异常 socket 生命周期还没完全闭环；另外任务状态机现在已经进入业务定规则阶段，不能再让 API 任意绕过状态机。

## P1 必改

### 1. WebService 仍未接入 QTcpSocket::errorOccurred

上一轮最关键的建议是“异常连接统一清理”。这一版做了：

- `isSocketWritable()`
- `writeToSocket()`
- `scheduleHttpClose()`
- keep-alive buffer 不再处理完就删除
- Vite proxy 对 `ECONNREFUSED/ECONNRESET` 降噪

这些都对。

但 `core/web/WebService.cpp` 的 `onHttpConnection()` 仍然只连接了：

```cpp
connect(sock, &QTcpSocket::readyRead, this, &WebService::onHttpReadyRead);
connect(sock, &QTcpSocket::disconnected, this, &WebService::onHttpDisconnected);
```

没有连接 `QTcpSocket::errorOccurred`。这意味着 UI 先开、core 重启、Vite proxy 半连接关闭、浏览器刷新等场景下，连接错误仍然只能依赖 disconnected 间接清理，不够完整。

建议 Cursor 修改：

```cpp
connect(sock, &QTcpSocket::errorOccurred, this,
        [this, sock](QAbstractSocket::SocketError) {
            scheduleHttpClose(sock);
        });
```

更理想的是把现在的 `scheduleHttpClose()` 改名或拆成两个方法：

- `closeHttpSocket()`：发起关闭，停止 timer，必要时 flush/disconnect。
- `cleanupHttpSocket()`：真正清理 `m_buffers`、`m_idleTimers`、signal、`deleteLater()`。

否则 error、timeout、disconnected、stop 四条路径仍然容易出现重复清理或半清理。

### 2. scheduleHttpClose() 只 remove buffer，不 take timer

当前 `scheduleHttpClose()`：

```cpp
m_buffers.remove(sock);
if (auto* t = m_idleTimers.value(sock))
    t->stop();
```

这里 timer 只是 stop，没有从 `m_idleTimers` 中移除。正常情况下后续 `onHttpDisconnected()` 会 `take()`，但如果 socket 迟迟不进入 disconnected，map 中会残留已停止 timer。

建议：

- 发起关闭时如果决定不再管理该 socket，就 `take()` timer。
- 或者保持 buffer/timer 到 disconnected，再由 `cleanupHttpSocket()` 统一释放。
- 不建议现在这种“buffer 先删、timer 留着”的中间态。

### 3. TaskService::create() 不应接受外部 status 绕过状态机

现在 `TaskInfo::fromJson()` 会读 body 里的 `status`，`TaskService::create()` 只在 `task.status.isEmpty()` 时默认 Draft。也就是说调用方可以直接 POST 一个 `Active`、`Completed` 或 `Archived` 任务。

这会绕开服务端状态机：

- Active 应该通过 `/api/tasks/{id}/activate`
- Completed 应该通过 `/complete`
- Archived 应该通过 `/archive`

建议：

- `POST /api/tasks` 强制创建 `Draft`，忽略 body.status。
- 如果未来需要导入历史任务，单独开 `importTask()` 或管理接口，不要复用普通创建接口。

### 4. activate() 只允许 Draft，需要确认业务语义

当前 `activate()` 只允许：

```cpp
Draft -> Active
```

这符合“任务创建、配置、激活”的标准流程。但它也意味着：

- Completed 任务不能重新激活
- 历史任务不能再次作为当前任务加载
- 后续如果有“复盘/再次执行/恢复任务”，需要新建任务或增加 clone/reopen 动作

建议先把规则写死在架构文档或接口注释里：

- 如果任务是一次性业务单元，当前规则可以保留。
- 如果任务可能被再次执行，应增加 `cloneTask()` 或 `reopen()`，不要直接让 Completed -> Active 混入 activate。

## P2 建议

### 5. task_devices 没有 device_id 外键，删除设备后任务关系可能残留

这一版 `TaskService::validateDeviceIds()` 会在保存任务设备时校验设备存在，这很好。

但 `task_devices` 只加了：

```sql
FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE
```

没有 `device_id -> devices(id)` 外键。`DeviceRegistry::remove()` 直接删除设备时，已有任务里的 `task_devices.device_id` 可能残留。

建议二选一：

1. 给 `device_id` 增加外键并按业务决定 `ON DELETE CASCADE` 或 `RESTRICT`。
2. 如果暂时不加外键，`DeviceRegistry::remove()` 删除设备前由业务层清理所有任务绑定关系，或拒绝删除仍被任务引用的设备。

从“设备 source of truth 在后端”的原则看，不建议长期保留悬挂 deviceId。

### 6. DataStore 事务接口没有嵌套保护

现在 `DataStore` 增加了 `beginTransaction/commitTransaction/rollbackTransaction`，足够支撑当前 TaskService。

但要注意：`insertBatch()` 内部也会开事务，未来如果业务服务在事务里调用 `insertBatch()`，Qt/SQLite 对嵌套事务不会按预期工作。

建议暂时不急着重构，但要约定：

- DataStore 不提供透明嵌套事务。
- Service 层不要在已开启事务的上下文里调用会自行开事务的方法。
- 后续可以加一个轻量 `TransactionGuard`，统一 rollback/commit 语义。

## 已修复确认

- `TaskService` 已接入 `DeviceRegistry`，任务绑定设备 id 会校验。
- `create/update/activate/remove/updateDevices` 已使用事务，半成功风险明显下降。
- `PUT /api/tasks/{id}` 已改成按字段存在性更新，允许清空 `description/operatorName`。
- 前端 `TaskStatus` 已增加 `draft`，未知状态默认 draft，不再误当 active。
- `/api/tasks/active` 不再 fallback 到第一个 active 状态，避免后端 active source of truth 被前端猜测覆盖。
- Vite proxy 已对常见后端不可用错误降噪。
- TitleBar 增加后端连接状态，对 UI 先启动场景更友好。

## 下一步建议

1. 先补 WebService `errorOccurred` 和统一 cleanup，这仍然是启动顺序/重启稳定性的最后一块。
2. 立刻修 `POST /api/tasks` 外部 status 绕过状态机的问题。
3. 明确任务状态机是否一次性：如果一次性，保留 Draft -> Active -> Completed -> Archived；如果可复用，增加 clone/reopen，不要让 activate 语义变混。
4. 处理设备删除后的任务绑定关系，避免 `task_devices` 残留无效 deviceId。
5. 这几个收完后，可以继续推进任务上下文与航迹/日志的绑定，也就是 Milestone 4 往历史数据中心靠。

## 给 Cursor 的最小修改清单

1. `WebService::onHttpConnection()` 连接 `QTcpSocket::errorOccurred`。
2. 抽 `cleanupHttpSocket(QTcpSocket*)`，让 disconnected/error/timeout/stop 共享清理逻辑。
3. `scheduleHttpClose()` 不要留下 “buffer 已删除、timer 仍在 map” 的中间态。
4. `TaskService::create()` 强制 `task.status = Draft`。
5. 决定并实现设备删除时 task-device 关系策略：外键 cascade/restrict，或业务层清理/拒绝。

