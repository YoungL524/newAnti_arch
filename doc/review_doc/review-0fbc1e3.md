# review-0fbc1e3

## 结论

本轮 `0fbc1e3` 的方向基本正确：任务数据自动归属没有要求外部雷达携带业务 `deviceId`，而是按 `endpointId -> DeviceRegistry.deviceId -> activeTask.deviceIds -> taskId` 做后端归属；`tracks` 表也补了 `task_id/device_id` 和兼容旧库的 `ALTER TABLE`。

但代码质量已经进入“能跑，但需要收口”的阶段。当前最需要先处理的是两个会影响长期稳定性的点：

1. 航迹热路径里每包查数据库，后续高频数据会被 SQLite 和对象组装拖慢。
2. 前端保存单个雷达端点时会覆盖整个 `endpoints.json`，有删除其他通信端点的风险。

## 必须修复

### P1: 航迹归属每包查询 active task，数据库进入高频热路径

位置：

- `core/processor/RadarDataProcessor.cpp:130-143`
- `core/business/TaskService.cpp:129-147`
- `core/business/TaskService.cpp:88-107`

当前流程是每个 `GeneralPackage`：

```text
findRadarByEndpointId(endpointId)
-> resolveActiveTaskIdForDevice(deviceId)
-> activeTask()
-> rowToTask()
-> SELECT task_devices
```

这意味着每条航迹至少 2 次数据库查询，active task 还会额外加载设备列表。低频测试没问题，但后续多雷达/高点频时，数据库会被放进接收链路热路径，影响上行延迟和 UI 实时性。

建议 Cursor 修改：

- `DeviceRegistry` 增加内存映射缓存：`endpointId -> deviceId`，设备创建/更新/删除后刷新。
- `TaskService` 增加 active task 上下文缓存，例如：

```cpp
struct ActiveTaskContext {
    QString taskId;
    QSet<QString> deviceIds;
};
```

- `activate/complete/archive/updateDevices/remove` 后刷新缓存。
- `resolveActiveTaskIdForDevice()` 只查内存 `QSet`，不要每包查 SQLite。
- 启动时从数据库加载一次 active task。

架构上，这个缓存可以叫 `ActiveTaskContext`，但不要扩展成新数据源概念；它只是任务运行上下文。

### P1: 编辑一个雷达端点会覆盖整个 endpoints.json

位置：

- `ui/src/components/dialogs/DeviceEditDialog.tsx:19-30`
- `ui/src/lib/endpointsApi.ts:49-62`
- `core/web/EndpointApiRoutes.cpp:18-42`
- `core/AppController.cpp:707-752`

`handleRadarSave()` 当前调用：

```ts
saveEndpoints([endpoint])
```

后端 `PUT /api/endpoints` 的语义是替换整个 `endpoints` 数组。也就是说，编辑某个雷达时，前端只提交当前 endpoint，会把其他 endpoint 全部从 `endpoints.json` 删除。后端虽然会阻止删除“仍被设备绑定”的 endpoint，但未绑定端点、预留端点、未来其他通信端点都会丢。

建议 Cursor 修改：

- 前端保存前先拿完整 endpoints 列表，把当前 endpoint 按 id merge 进去，再整体 PUT。
- 或者后端新增 `PUT /api/endpoints/{id}`，只更新单个 endpoint。
- 更推荐后者：端点配置属于通信配置，不应该由设备编辑弹窗掌握整个 endpoints 文件。
- 后端接口需要保持删除端点时的绑定保护。

### P2: endpointId 唯一绑定靠“迁移清空”实现，缺少数据库约束和错误回滚

位置：

- `core/business/DeviceRegistry.cpp:9-23`
- `core/business/DeviceRegistry.cpp:189-219`

当前雷达设备绑定 endpoint 时，`migrateEndpointBinding()` 会把其他设备的 `endpoint_id` 清空。这个策略能避免多绑定，但有两个问题：

- 不是事务包裹。`create/update` 如果后续失败，旧设备的绑定可能已经被清空。
- 没有数据库唯一约束，其他路径或并发调用仍可能写出重复绑定。

建议 Cursor 修改：

- `create/update` 对“迁移旧绑定 + 写新设备”整体加事务。
- `devices(endpoint_id)` 增加条件唯一约束或应用层强校验。SQLite 可用唯一索引，但空字符串需要谨慎处理；可以考虑把未绑定 endpoint 存为 `NULL`，然后建 `UNIQUE(endpoint_id)`。
- API 层返回明确错误：endpoint 已被其他设备绑定时，是“自动迁移”还是“要求用户确认”。管理系统里更推荐显式确认，不要静默迁移。

### P2: SystemBus 不可重启，AppController stop 后同进程无法重新 start

位置：

- `core/bus/SystemBus.cpp:29-40`
- `core/bus/SystemBus.cpp:476-491`
- `core/AppController.cpp:269-357`

现在 `SystemBus::shutdown()` 是不可逆的，`AppController::stop()` 会调用它。这个设计适合进程退出，但不适合“core 内部软重启”或未来测试用例反复 `initialize/start/stop/start`。之前 UI/core 启动顺序问题已经暴露过类似生命周期敏感点。

建议 Cursor 修改：

- 区分 `stopProcessing()` 和 `shutdown()`：
  - 程序退出时才 `shutdown()`。
  - 普通 `AppController::stop()` 只停止业务订阅、通信端点、DPH，不永久关闭总线。
- 如果坚持不可重启，代码注释和调用点要明确：`stop()` 语义就是进程级停止，不支持再次 start。

## 本轮修改评价

### 做得好的地方

- 任务归属方向符合之前讨论：外部协议不需要携带业务 `deviceId`。
- `tracks` 表保留 `source`，新增 `task_id/device_id`，没有破坏原始来源排查能力。
- `ensureTracksSchema()` 对旧库做了列迁移，这一点比单纯 `CREATE TABLE IF NOT EXISTS` 稳。
- `EndpointApiRoutes` 把端点配置 API 拆出来，代码职责比继续塞进 `WebApiController` 更清晰。

### 仍需收口的地方

- `RadarDataProcessor` 同时做协议解析、总线发布、任务归属、入库、TrackPoint 输出，职责偏重。当前可以接受，但下一阶段应拆出轻量 resolver/cache，而不是继续往里面加业务规则。
- `AppController` 已经变成组合根加一部分业务服务实现，文件过大。后续新增模块时应优先用 `*Routes`、`*Service`、`*Bridge` 分出去。
- 前端 `App.tsx` 仍承担任务、设备、后端降级、本地状态合并等大量逻辑，已经接近“页面级上帝组件”。后续应拆 `useDevicesRegistry()`、`useTasks()`、`useTaskDeviceBinding()`。

## 全局代码质量评估

### 架构符合度

当前大方向仍然符合我们定下的基座：

```text
通信端点 -> 协议适配/处理 -> SystemBus -> 业务管理器 -> Web/API -> React UI
设备台账/任务台账由后端维护
UI 不生成最终业务 ID
任务是业务数据归属边界，不是基础设施开关
```

尤其是设备台账、任务台账、航迹显示管理、RuntimeSettings、Web routes 都已经逐步从纯 UI 状态迁到了后端，这是正确方向。

### 代码质量等级

建议按当前阶段评为：`B-`。

解释：

- 业务边界已经从“能演示”进入“有架构”的状态。
- 关键链路已有服务拆分和总线抽象。
- 但热路径性能、生命周期语义、前端状态分层、配置/台账边界还没有完全固化。
- 如果继续在现有文件里叠功能，后面会变成“每处都能改，但每处都不敢改”。

### 最大风险

1. **热路径混入数据库查询**：航迹、DPH、总线派发这些链路要坚持内存态优先，数据库只做持久化和低频查询。
2. **配置文件和业务台账混用**：`endpoints.json` 是通信端点配置，`DeviceRegistry` 是业务设备台账。UI 可以展示绑定关系，但不要让设备编辑弹窗直接替换整份通信配置。
3. **组合根继续膨胀**：`AppController` 是装配层，不应承载越来越多业务决策。
4. **前端本地降级逻辑滞留**：任务/设备已经后端化后，localStorage fallback 应逐步退出核心路径，否则调试时会出现“UI 看起来有数据但后端没有”的错觉。

## 建议 Cursor 下一步实现顺序

### Step 1: 修复 endpoint 保存覆盖问题

优先级最高。建议新增：

```text
GET /api/endpoints
PUT /api/endpoints/{id}
DELETE /api/endpoints/{id}
```

设备编辑只调用单 endpoint 更新；端点管理页再负责全量新增/删除。

### Step 2: 做 ActiveTaskContext 缓存

目标是让航迹包处理时不再查 SQLite：

```text
endpointId -> cached deviceId
deviceId -> cached activeTaskContext.contains(deviceId)
```

SQLite 只在任务激活、任务设备变更、设备绑定变更时参与刷新。

### Step 3: 收紧 DeviceRegistry endpoint 绑定语义

明确 endpoint 是“一个通信端点最多绑定一个业务雷达设备”。实现上加事务和唯一性约束，避免静默清空导致用户困惑。

### Step 4: 拆前端大组件

从 `App.tsx` 抽：

- `useDeviceRegistry()`
- `useTasksBackend()`
- `useTaskDeviceBinding()`

这一步不改变业务，只是降低前端维护成本。

### Step 5: 给核心链路补最小测试

不用铺很大，先补这些：

- TaskService 状态流转和设备绑定校验。
- DeviceRegistry endpoint 单绑定。
- RadarDataProcessor 归属规则：active task 命中写库，未命中不写库。
- Endpoint API 单 endpoint 更新不丢其他 endpoint。

## 给 Cursor 的一句话

这轮不要继续堆新业务。先把“端点保存不丢配置”和“航迹归属不查库”两个点修掉，再做任务历史查询/回放等上层功能。现在系统的架构方向是对的，但需要把热路径和配置边界钉牢。
