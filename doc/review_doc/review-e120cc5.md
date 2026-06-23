# review-e120cc5

## 结论

本轮 `e120cc5` 继续修复 `review-339a7da` 提到的生命周期与历史数据兼容问题，整体有明显进步：

- `AppController::stop()` 已经不再断开数据库。
- `SystemBus` 注释已改成“进程级常驻，不可恢复”，和实现一致了。
- `DeviceRegistry::ensureSchema()` 在创建 endpoint 唯一索引前增加了历史重复绑定清理。
- `refreshEndpointCache()` 已改成只 `list()` 一次。
- 设备 create/update 会返回 `migratedDeviceIds`，比之前静默迁移更透明。

但“软停止后再次 start”的闭环仍然没有完成。现在不是数据库断开的问题了，而是多个总线订阅/桥接在 `stop()` 中被拆掉，但 `start()` 没有重新接回。

## 必须修复

### P1: stop/start 仍然不能完整恢复运行态订阅

位置：

- `core/AppController.cpp:277-306`
- `core/AppController.cpp:309-360`
- `core/AppController.cpp:800-849`

当前 `stop()` 会做这些运行态拆卸：

```cpp
m_webApi->shutdown();
m_webApi.reset();
m_trackDisplayManager->stop();
m_trackDisplayWebBridge->stop();
m_trackDisplayWebBridge.reset();
m_busRouter->unsubscribe();
SystemBus::unsubscribe(m_runtimeSettingsSubId);
m_runtimeSettingsSubId = 0;
```

但 `start()` 只做：

```cpp
m_commManager->startAll();
m_dphPublisher->start();
m_running = true;
```

它不会恢复：

- `BusToCommRouter` 对 SystemBus 的反向流订阅。
- `RuntimeSettings -> DphPublisher` 的桥接订阅。
- `TrackDisplayManager` 的 command 订阅和 snapshot 定时器。
- `TrackDisplayWebBridge`。
- `WebApiController` REST/WS 路由。

因此同进程执行 `stop(); start();` 后，表面上通信和 DPH 可能恢复，但控制链路、运行时参数、航迹显示/Web 推送都可能断掉。

建议 Cursor 修改：

- 先明确两层接口：

```text
stopRuntime(): 只停通信端点、DPH、采集类 timer；保留总线订阅、Web API、数据库、业务 manager。
shutdown(): 进程退出级清理，退订总线、关闭 Web、断 DB、shutdown SystemBus。
```

- 如果当前 `stop()` 要承担软停止语义，就不要在 `stop()` 里 reset `m_webApi/m_trackDisplayWebBridge`，也不要 `m_busRouter->unsubscribe()` 和取消 runtime bridge。
- 如果 `stop()` 要承担进程级停止语义，就不要宣称支持同进程再次 `start()`。
- 最推荐：新增 `shutdown()`，析构调用 `shutdown()`；`stop()` 只做运行态停止。

一句话：现在生命周期名字和动作仍然不匹配。

### P1: DeviceRegistry 历史 endpoint 去重只清雷达重复，可能漏掉非雷达重复导致唯一索引失败

位置：

- `core/business/DeviceRegistry.cpp:41-69`
- `core/business/DeviceRegistry.cpp:127-154`

`dedupeLegacyEndpointBindings()` 先查所有重复 endpoint：

```sql
SELECT endpoint_id FROM devices
WHERE endpoint_id != ''
GROUP BY endpoint_id HAVING COUNT(*) > 1
```

但后续只查询并清理 `kind = Radar` 的记录：

```sql
SELECT id FROM devices
WHERE endpoint_id = ? AND kind = ?
```

问题是唯一索引是对所有非空 `endpoint_id` 生效：

```sql
ON devices(endpoint_id) WHERE endpoint_id != ''
```

如果历史库里有：

```text
radar_001 endpoint_id = radar_cpu
opto_001  endpoint_id = radar_cpu
```

或者两个非雷达设备误填了同一个 endpoint，`dedupeLegacyEndpointBindings()` 不会清理完整，随后唯一索引仍可能创建失败。

建议 Cursor 修改：

- 去重逻辑和唯一索引约束必须一致：既然唯一索引约束所有非空 endpoint，就清理所有 kind。
- 或者如果业务上只有雷达可以绑定通信 endpoint，就先把非雷达设备的 `endpoint_id` 全部迁移为空，再建唯一索引。
- 迁移策略建议：

```text
同一个 endpoint_id 下：
1. 优先保留 kind=Radar 的设备；
2. 如果有多个 Radar，保留 updated_at 最新 / created_at 最早；
3. 非 Radar 的 endpoint_id 全部清空；
4. 写 warning 日志。
```

这样和“通信 endpoint 只绑定可控雷达设备”的业务规则一致。

### P2: migratedDeviceIds 已返回，但前端没有处理迁移后的本地设备状态

位置：

- `core/web/WebApiController.cpp:250-279`
- `core/web/WebApiController.cpp:281-309`
- `ui/src/lib/devicesApi.ts`
- `ui/src/App.tsx`

后端 create/update 现在会返回 `migratedDeviceIds`，这比静默迁移好。但前端当前更新设备时只更新当前设备，没有根据 `migratedDeviceIds` 把其他设备的 `endpointId` 清空。

结果是保存设备 A 绑定 `radar_cpu` 后，后端已经把设备 B 的 endpoint 清空，但 UI 本地列表里的设备 B 可能还显示旧绑定，直到下一次完整 reload 才一致。

建议 Cursor 修改：

- `devicesApi.updateDevice/createDevice` 的返回类型加入 `migratedDeviceIds`。
- `App.tsx` 在更新成功后：
  - 当前设备使用后端返回 DTO 或本地 updates。
  - 对 `migratedDeviceIds` 中的设备执行 `endpointId: undefined`。
- 更好的做法：后端 update 成功后也返回完整 `device` DTO，让前端始终以服务端返回为准。

## 本轮实现评价

### 做得好的地方

- `AppController` 数据库生命周期这次修对了：软停止不再断库。
- `SystemBus` 注释不再误导，这比假装支持 worker restart 更清晰。
- `DeviceRegistry` 的历史迁移思路是对的，先修旧数据再加唯一索引。
- `migratedDeviceIds` 的 API 反馈是正确方向，后续前端可以把隐式副作用变成可见状态更新。

### 仍需收口的地方

- `AppController` 的生命周期代码还没有分层。`stop()` 混合了运行停止和资源销毁，这就是反复修不干净的原因。
- `DeviceRegistry` 仍存在“自动迁移绑定”的业务副作用。短期可接受，但最终还是建议前端确认后迁移。
- `WebApiController` 继续膨胀，设备 API、运行时参数、航迹交互、状态、日志都在一个函数里，后续建议拆 routes。

## 代码质量复盘

本轮代码质量继续小幅提升，但还没有到可以放心继续堆新功能的程度。当前主要风险从“热路径性能”转移到了“生命周期与服务边界”。

建议当前质量仍评为 `B`：

- 核心方向正确。
- 关键热路径已经从 DB 查询改成缓存。
- API 能反馈 endpoint 迁移副作用。
- 但 AppController 运行态/销毁态边界还没钉死。

## 建议 Cursor 下一步

1. 先重构 AppController 生命周期：
   - `startRuntime()`
   - `stopRuntime()`
   - `shutdown()`
   - 析构只调用 `shutdown()`
2. 修正 `DeviceRegistry::dedupeLegacyEndpointBindings()`，确保迁移逻辑和唯一索引范围一致。
3. 让设备 update/create 返回完整 DTO，并让前端处理 `migratedDeviceIds`。
4. 开始拆 `WebApiController`：
   - `DeviceApiRoutes`
   - `RuntimeSettingsApiRoutes`
   - `TrackInteractionApiRoutes`

## 给 Cursor 的一句话

这轮修得不错，但 AppController 的 `stop()` 仍然像“半个 shutdown”。先把生命周期拆清楚，否则之后每次加 Web、总线订阅、定时器都会反复踩同一个坑。
