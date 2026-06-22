# Review 086e44a

## 结论

这一版把 `review-a38cafe` 的主要问题基本修掉了：`WebService` 已接入 socket error handler，HTTP close/cleanup 开始拆分；`TaskService::create()` 已强制 Draft；删除设备时也开始清理 `task_devices`。整体已经从“架构边界搭建”进入“业务语义收口”阶段。

当前最重要的问题不是编译或接口数量，而是前后端业务语义还差一个闭环：后端有 Fault reset，UI 也显示“复位”，但 Web API 和前端 hook 没有真正的 reset 请求链路。

## P1 必改

### 1. Fault 状态的“复位”按钮没有后端 API 链路

后端状态机里已经有：

```cpp
void RadarWorkModeController::reset()
```

`TitleBar.tsx` 也在 Fault 状态把按钮文案改成：

```tsx
const btnLabel = isFault ? '复位'
```

但 `WebApiController` 只注册了：

```cpp
/api/start
/api/stop
```

没有 `/api/reset`。前端 `toggleAcquisition()` 也只会根据 `isWorking` 调 `/api/start` 或 `/api/stop`，不会在 `state === 'Fault'` 时调 reset。

实际后果：

- UI 显示“复位”，用户点击后并不会调用 `RadarWorkModeController::reset()`。
- Fault 状态下 `/api/start` 或 `/api/stop` 会被状态机拒绝，用户看到的“复位”按钮实际是无效按钮。
- 这会让状态机、Web API、UI 三层语义不一致。

建议 Cursor 修改：

1. `WebApiController` 增加 `POST /api/reset`。
2. handler 内调用 `m_wmc->reset()`，并返回当前状态。
3. 前端 `useCoreApi.toggleAcquisition()` 在 `stateRef.current === 'Fault'` 时调用 `/api/reset`，不要走 start/stop。
4. reset 成功后前端乐观更新到 `Standby`，或等待下一次 `/api/status` / WS status 回推。
5. 如果未来多设备状态机落地，reset API 要支持 `deviceIds`，当前单雷达阶段可以先复位 `radar_cpu` 映射设备。

### 2. RadarWorkModeController 超时即成功，需要明确为“临时乐观确认”

当前状态机：

```cpp
Starting 超时 -> Working
Stopping 超时 -> Standby
```

这和测试、业务文档目前是一致的。但从设备控制语义看，它表示“命令发出后没有收到明确失败，就按成功处理”。如果真实雷达没有 ACK，这个设计可以作为现阶段工程折中；但它不是严格的设备状态确认。

建议：

- 在 `doc/business/雷达工作状态控制.md` 明确写出：当前 300ms 超时是临时乐观确认，不代表雷达真实 ACK。
- 后续如果雷达协议能提供明确回显，应改为：
  - 收到匹配 ACK -> 成功状态
  - 超时无 ACK -> Fault 或 Unknown
- UI 上“Working”要理解为“上位机控制状态”，不是绝对设备物理状态。

这条不是要立刻改代码，而是要把语义定死，避免后续调试时把“命令状态”和“设备真实状态”混在一起。

## P2 建议

### 3. DeviceRegistry::remove() 清任务绑定和删设备应放进事务

当前删除设备：

```cpp
DELETE FROM task_devices WHERE device_id = ?
DELETE FROM devices WHERE id = ?
```

如果第一步成功、第二步失败，就会出现设备仍存在但任务绑定已被清掉的半成功状态。

建议：

- `DeviceRegistry::remove()` 使用 `DataStore::beginTransaction/commitTransaction/rollbackTransaction`。
- 或者更彻底地给 `task_devices.device_id` 加外键并选择 `ON DELETE CASCADE` / `RESTRICT`。

考虑当前系统已经有 TaskService 事务，这里也应该保持一致。

### 4. WebService error signal 写法需要确认 Qt 版本兼容

当前代码使用：

```cpp
connect(sock, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
        this, &WebService::onHttpSocketError);
```

如果当前项目使用 Qt5，这种写法一般可用；如果未来切 Qt6，`error` 已被 `errorOccurred` 替代。建议做一个版本兼容封装，避免后面升级时踩坑：

```cpp
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
connect(sock, &QTcpSocket::errorOccurred, this, &WebService::onHttpSocketError);
#else
connect(sock, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
        this, &WebService::onHttpSocketError);
#endif
```

这条不是当前功能 blocker，但 WebService 已经成为基础设施层，建议写得更稳。

### 5. closeHttpSocket() 对 RemoteHostClosedError 的处理可以再简化

`onHttpSocketError()` 对 `RemoteHostClosedError` 直接 return，等待 disconnected 清理，这是合理的。

但其他 error 走 `closeHttpSocket()`，如果 socket 不是 ConnectedState，会立即 `cleanupHttpSocket()`。整体可用，但 close/cleanup 的职责还可以更明确：

- `closeHttpSocket()`：只负责主动关闭。
- `cleanupHttpSocket()`：只负责资源释放。
- error handler 可以对非 connected socket 直接 cleanup，connected socket 再 close。

当前不是明显 bug，只是后续维护性建议。

## 已修复确认

- `WebService` 已有 `onHttpSocketError()`，并拆出 `closeHttpSocket()` / `cleanupHttpSocket()`。
- `TaskService::create()` 已强制 `Draft`，外部不能再通过 POST body.status 绕过状态机。
- `DeviceRegistry::remove()` 已清理 `task_devices.device_id`，不会再长期残留无效 deviceId。
- 前端 `TitleBar` 已使用 `commandPending` 禁用按钮，减少重复点击。
- `useCoreApi` 对 WS close 不再直接把 connected 置 false，避免刷新/StrictMode 引发误判。

## 下一步建议

1. 先补 `/api/reset` 和前端 Fault reset 调用，这是当前最明确的前后端语义缺口。
2. 给 `DeviceRegistry::remove()` 加事务。
3. 把工作模式“超时乐观成功”的语义写入业务文档；如果你决定严格 ACK，再反向改状态机和测试。
4. 这轮收完后，设备/任务/启动顺序这一小段可以暂时告一段落，继续往 Milestone 4 的“任务上下文绑定航迹、日志、历史数据”推进。

## 给 Cursor 的最小修改清单

1. `WebApiController` 增加 `POST /api/reset`。
2. `useCoreApi.toggleAcquisition()` 在 `stateRef.current === 'Fault'` 时调用 `/api/reset`。
3. reset 成功后清理 `commandError`，并让状态回到 Standby。
4. `DeviceRegistry::remove()` 加事务保护。
5. 在 `doc/business/雷达工作状态控制.md` 标注当前超时成功是临时乐观确认。

