# review-339a7da

## 结论

本轮 `339a7da` 主要是在修 `review-0fbc1e3` 提到的几个关键问题，整体方向是对的：

- 航迹热路径已经从 `TaskService::activeTask()` 查库改成了 `ActiveTaskContext` 内存缓存。
- `RadarDataProcessor` 改用 `DeviceRegistry::cachedDeviceIdForEndpoint()`，避免每包查设备表。
- 单个雷达编辑不再 `saveEndpoints([endpoint])` 覆盖整份 `endpoints.json`，而是走 `PUT /api/endpoints/{id}`。
- `AppController::stop()` 不再直接 `SystemBus::shutdown()`，方向上更接近软停止。

但还有几个需要继续收口的问题，尤其是“软停止后再启动”的语义现在仍然不完整。

## 必须修复

### P1: AppController stop 后断开数据库，但再次 start 不会重连

位置：

- `core/AppController.cpp:307-363`
- `core/AppController.cpp:275-304`

这轮把 `SystemBus::shutdown()` 从 `stop()` 移到了析构，方向是对的。但 `stop()` 仍然执行：

```cpp
if (m_dataStore) {
    m_dataStore->disconnect();
}
```

问题是 `start()` 只启动通信端点和 DPH，不会重新 `connectDatabaseFromConfig()`，也不会刷新：

- `DeviceRegistry` cache
- `TaskService` active task cache
- `RadarDataProcessor` 的写库能力

结果是同进程内 `stop(); start();` 后，总线还活着，但数据库相关业务处于断开状态，航迹不会入库，任务归属缓存也可能是旧的。

建议 Cursor 修改：

- 如果 `stop()` 是“软停止”，不要断开数据库；数据库生命周期跟 `initialize()/destructor` 走。
- 如果必须释放数据库，则 `start()` 需要显式重连并刷新缓存。
- 更清晰的接口语义：

```text
stopRuntime(): 停 DPH / 通信端点 / Web bridge / 订阅，不断 DB，不 shutdown bus
shutdown(): 进程退出级清理，断 DB，shutdown bus
```

当前代码注释说支持同进程再次 start，但实现还没有完全支持。

### P1: SystemBus 的 startProcessing 仍然不可恢复，注释和实现不一致

位置：

- `core/bus/SystemBus.h:269-277`
- `core/bus/SystemBus.cpp:476-491`

头文件注释写了：

```cpp
停止业务处理线程（可再次 startProcessing）
```

但实现中 `stopProcessing()` 会：

```cpp
m_messageQueue.shutdown();
delete m_worker;
m_worker = nullptr;
```

`startProcessing()` 只有在 `m_worker` 已存在时才 `start()`，不会重新创建 worker，也不会恢复已经 shutdown 的队列。因此这个“可再次 startProcessing”目前并不成立。

建议 Cursor 修改：

- 如果总线工作线程只在进程生命周期内启动一次，就删掉“可再次 startProcessing”的注释，避免误导。
- 如果真的要支持 bus worker 软重启，需要：
  - `ThreadSafeQueue` 支持 reset/reopen。
  - `startProcessing()` 在 `m_worker == nullptr` 时创建新 worker。
  - 明确 State 不进入 `ShutDown`。

目前更建议保持简单：SystemBus 进程级常驻，不做 worker 软重启；AppController 的软停止只退订业务订阅，不停总线 worker。

### P1: devices endpoint 唯一索引可能让历史库初始化失败

位置：

- `core/business/DeviceRegistry.cpp:82-107`

`ensureSchema()` 现在直接创建：

```sql
CREATE UNIQUE INDEX IF NOT EXISTS idx_devices_endpoint_id_unique
ON devices(endpoint_id) WHERE endpoint_id != ''
```

这对新库没问题，但如果历史库里已经有多个雷达绑定同一个 `endpoint_id`，创建唯一索引会失败，导致 `DeviceRegistry::ensureSchema()` 返回 false，设备台账不可用。

这和旧代码里的“发现重复 endpoint 时拒绝返回映射/缓存跳过”的兼容策略冲突：还没来得及进入兼容逻辑，schema 初始化就失败了。

建议 Cursor 修改：

- 在创建唯一索引前先做迁移检查：

```sql
SELECT endpoint_id, COUNT(*)
FROM devices
WHERE endpoint_id != ''
GROUP BY endpoint_id
HAVING COUNT(*) > 1
```

- 对重复数据执行确定性修复：
  - 保留 `updated_at` 最新或 `created_at` 最早的一条绑定。
  - 其他设备 `endpoint_id=''`。
  - 记录 warning 日志。
- 然后再创建唯一索引。

这样既能兼容历史库，又能把未来数据约束钉牢。

### P2: DeviceRegistry 仍然采用“静默迁移 endpoint 绑定”，业务语义不够清晰

位置：

- `core/business/DeviceRegistry.cpp:9-23`
- `core/business/DeviceRegistry.cpp:220-291`

本轮已经把迁移和 create/update 放进事务，这是进步。但语义上仍然是：新设备绑定 endpoint 时，自动把旧设备 endpoint 清空。

这会导致 UI 用户没有确认的情况下，另一个设备突然失去通信绑定。对管理系统来说，这类动作最好显式可见。

建议 Cursor 修改：

- 短期：保留自动迁移，但 API 返回字段说明发生过迁移，例如 `migratedDeviceIds`。
- 中期：改成默认拒绝，返回“endpoint 已被设备 X 绑定”，前端要求用户确认后再迁移。
- 后端服务层可以提供明确方法：

```cpp
bindEndpoint(deviceId, endpointId, ConflictPolicy policy)
```

其中 `ConflictPolicy::Reject` 为默认，`ConflictPolicy::MoveBinding` 需要 UI 确认。

## 本轮实现评价

### 做得好的地方

- `TaskService::resolveActiveTaskIdForDevice()` 已经不再查库，热路径明显改善。
- `AppController::initialize()` 中调用了 `refreshActiveTaskCache()` 和 `refreshEndpointCache()`，启动态缓存是有加载的。
- 单 endpoint API 的引入是正确的，设备编辑弹窗不再掌握整份 endpoints 配置。
- `DeviceRegistry` 增加唯一索引，说明开始把架构约束下沉到数据库层，这是好方向。

### 还需要细化的地方

- `DeviceRegistry::refreshEndpointCache()` 当前调用了 3 次 `list()`，虽然不在每包热路径，但可以一次 list 后复用，减少无意义 DB 查询。
- `TaskService` 缓存目前没有线程保护。当前大概率都在 Qt 主线程/事件线程内调用，暂时问题不大；但如果后续把解析处理 `moveToThread`，这个缓存需要加锁或改成线程归属明确的 queued 调用。
- `AppController` 仍然承担了太多职责。端点配置更新、数据库 schema、服务装配、运行生命周期都在一个文件里，继续变大后维护成本会明显上升。

## 代码质量复盘

当前质量比上一轮有所提升，尤其是热路径和 endpoint API 两个点修得比较对。代码质量可以从 `B-` 提到 `B`。

但目前最大的架构债变成了“生命周期语义不够明确”：

```text
initialize 是一次性装配？
start/stop 是运行态开关？
shutdown 是进程退出？
数据库属于运行态，还是装配态？
SystemBus 属于进程级基础设施，还是 AppController 运行态资源？
```

这些概念现在代码里混在一起。建议下一轮不要急着加新功能，先把生命周期边界命名和实现对齐。

## 建议 Cursor 下一步

1. 修正 `AppController::stop()`：软停止不要断数据库，或者补完整重连流程。
2. 修正 `SystemBus` 注释/实现不一致：总线建议进程级常驻，不做软重启。
3. 给 `DeviceRegistry::ensureSchema()` 加历史重复 endpoint 清理，再创建唯一索引。
4. 把 `refreshEndpointCache()` 改成只 `list()` 一次。
5. 开始拆 `AppController`：
   - `DatabaseSchemaInitializer`
   - `EndpointConfigService`
   - `RuntimeLifecycleController`

## 给 Cursor 的一句话

这轮修得方向没问题，但不要让“软停止支持再次 start”停留在注释层面。先把 AppController / SystemBus / DataStore 的生命周期语义钉死，再继续往任务历史和回放走。
