# review-dc9dde1

## 结论

本轮 `dc9dde1` 修复质量明显好于上一轮，几个关键方向已经基本闭环：

- `AppController::stop()` 现在只停运行态的通信端点和 DPH，不再拆 Web、总线订阅、数据库。
- 新增 `shutdown()` 做进程级释放，生命周期语义比之前清楚很多。
- `DeviceRegistry::dedupeLegacyEndpointBindings()` 已经能处理非雷达重复 endpoint，和唯一索引范围基本一致。
- 后端 update 设备会返回完整 `device` DTO，前端也开始处理 `migratedDeviceIds`，UI 状态比上一版更一致。

这轮可以认为“生命周期大坑”基本填上了。剩下的问题更多是状态准确性和业务边界收口。

## 必须修复

### P1: `/api/status` 仍用 endpoint 总数当作 open 数，软停止后 UI 会误判 core connected

位置：

- `core/AppController.cpp:628-644`
- `core/AppController.cpp:40-53`
- `core/AppController.cpp:330-359`

`stopRuntime()` 已经只停止通信端点和 DPH，这个语义是对的。但 `healthReport()` 里仍然写着：

```cpp
report.comm.totalEndpoints = m_commManager->endpointCount();
report.comm.openEndpoints = m_commManager->endpointCount();
```

也就是说，执行 `stop()` 后端点实际已经 `close()`，但 `/api/status` 仍会返回：

```json
connected: true
endpoints: endpointCount
```

这会导致 UI 认为 core 仍处于连接/运行状态。现在 soft stop 保留 Web API 后，这个问题会更明显。

建议 Cursor 修改：

- `CommunicationManager` 增加：

```cpp
int openEndpointCount() const;
```

- 遍历 endpoint，统计 `isOpen()`。
- `healthReport()` 使用真实 open 数：

```cpp
report.comm.totalEndpoints = m_commManager->endpointCount();
report.comm.openEndpoints = m_commManager->openEndpointCount();
```

- `/api/status.connected` 继续使用 `openEndpoints > 0`。

这不是显示小问题，而是运行态状态源不准确。

### P1: 非雷达设备仍可写入 endpointId，和“通信 endpoint 只绑定雷达”规则不一致

位置：

- `core/web/WebApiController.cpp` 的 `deviceFromBody/validate`
- `core/business/DeviceRegistry.cpp:275-348`
- `core/business/DeviceRegistry.cpp:127-154`

本轮历史迁移已经优先保留 Radar、清理非 Radar，这说明业务规则已经很明确：通信 `endpointId` 应该只绑定雷达设备。

但当前 create/update 路径只在 `dev.kind == Radar` 时做 endpoint 迁移：

```cpp
if (!dev.endpointId.isEmpty() && dev.kind == DeviceKind::Radar)
    ok = migrateEndpointBinding(...)
```

如果 API 请求给非雷达设备传入 `endpointId`，后端 validate 没有拒绝，`DeviceRegistry` 也会照样写入数据库。唯一索引只能防重复，不能阻止非雷达占用 endpoint。

这会带来两个问题：

- 非雷达设备可能占住一个通信 endpoint，导致雷达绑定失败。
- `AppController::updateEndpointsConfig()` 删除 endpoint 时会检查所有设备的 `endpointId`，非雷达误绑定也会阻止删除通信端点。

建议 Cursor 修改：

- `WebApiController::validate()` 增加规则：

```cpp
if (d.kind != DeviceKind::Radar && !d.endpointId.isEmpty())
    return "只有雷达设备允许绑定通信端点";
```

- 或者在 `DeviceRegistry::create/update` 中强制：

```cpp
if (d.kind != DeviceKind::Radar)
    d.endpointId.clear();
```

更推荐 API 层拒绝，避免用户以为保存成功。

### P2: shutdown() 可以更幂等，避免未来手动调用后析构重复清理

位置：

- `core/AppController.cpp:34-38`
- `core/AppController.cpp:56-93`

当前 `shutdown()` 是 private，只在析构里调用，因此现在问题不大。但它没有 `m_shutdown` 状态保护，未来如果暴露或内部多处调用，可能重复执行：

- `m_busRouter->unsubscribe()`
- `m_dataStore->disconnect()`
- `m_webService->stop()`
- `SystemBus::shutdown()`

建议 Cursor 后续加一个简单防护：

```cpp
bool m_shutdown = false;

void AppController::shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    ...
}
```

这不是当前阻塞项，但生命周期代码刚整理完，顺手加会更稳。

## 本轮实现评价

### 做得好的地方

- `stopRuntime()` 和 `shutdown()` 的职责终于拆开了，这一步很关键。
- `stop()` 不再破坏 Web、SystemBus、数据库、TrackDisplay bridge，符合“运行态停止”的语义。
- DeviceRegistry 历史重复 endpoint 清理策略已经基本正确：优先保留雷达，清理非雷达和多余雷达。
- 前端 `updateDevice()` 使用后端返回 DTO，并处理 `migratedDeviceIds`，比本地猜状态可靠。

### 仍需收口的地方

- `AppController` 文件仍然过大，生命周期虽然清楚了，但数据库 schema、endpoint config、web service、业务装配都混在一起。
- `WebApiController` 继续承载太多 API 路由，设备 API 已经值得拆成 `DeviceApiRoutes`。
- 设备 endpoint 自动迁移虽然已有返回值，但还没有用户确认机制。短期可接受，长期建议做前端确认。

## 代码质量复盘

这轮后，代码质量可以从 `B` 提到 `B+`。

原因：

- 热路径缓存已经落地。
- 运行态/进程态生命周期已经基本分开。
- endpoint 唯一性开始有数据库约束和历史迁移。
- 前后端设备 DTO 同步更接近“以后端为准”。

目前最大的剩余风险不是架构方向，而是“状态源准确性”：UI 显示、健康状态、设备绑定状态必须严格来自后端真实状态，不能再用近似值。

## 建议 Cursor 下一步

1. 给 `CommunicationManager` 增加真实 `openEndpointCount()`，修正 `/api/status.connected`。
2. 禁止非雷达设备保存 `endpointId`。
3. 给 `AppController::shutdown()` 加幂等保护。
4. 下一步开始拆 API：
   - `DeviceApiRoutes`
   - `RuntimeSettingsApiRoutes`
   - `TrackInteractionApiRoutes`

## 给 Cursor 的一句话

生命周期这轮基本修到位了，别再大动它。下一步把“状态必须真实”钉住：open endpoint 要真实统计，endpointId 只能属于雷达设备。
