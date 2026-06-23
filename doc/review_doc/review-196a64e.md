# Review 196a64e

## 结论

这一版是一次正确的小步修复：上一轮指出的 `AppHealth.connected` 统计错误、`shutdown()` 重复调用风险、非雷达设备绑定通信端点的问题，代码层面都已经开始收敛。

当前整体方向符合既定架构：Web 常驻、运行态 stop 只停通信/DPH、SystemBus 保持进程级生命周期、设备台账由后端维护。  
但还需要补两个边界，否则后面任务驱动和设备状态会继续出现“看起来对，实际语义不稳”的问题。

## 本轮已修好的点

1. `CommunicationManager::openEndpointCount()` 已新增，`AppController::healthReport()` 不再把配置端点数量当作在线端点数量。
2. `AppController::shutdown()` 增加 `m_shutdown` 幂等保护，避免析构/显式关闭重复释放 Web、TrackDisplay、订阅等资源。
3. `/api/devices` 的 create/update 校验已经拒绝非雷达设备携带 `endpointId`，符合“通信端点只能绑定雷达业务设备”的架构约束。

## 必须继续处理的问题

### P1. 设备运行状态仍可能和 core 运行态不一致

位置：

- `core/web/WebApiController.cpp`
- `WebApiController::buildDeviceStatusArray()`
- `core/AppController.cpp`
- `AppController::stopRuntime()`

现状：

`buildDeviceStatusArray()` 对绑定 `radar_cpu` 的设备，直接使用 `RadarWorkModeController::state()` 生成：

- `state`
- `acquisition`

但 `AppController::stopRuntime()` 只停止：

- `DphPublisher`
- `CommunicationManager`

它不会重置或降级 `RadarWorkModeController` 的状态。

因此可能出现一种不一致状态：

1. UI 或外部调用让雷达进入 `Working`
2. core runtime 被 stop，通信端点已经全部关闭
3. `/api/status.connected` 已经是 false
4. `/api/status.devices[].acquisition` 仍可能显示 true

这会让前端误判“设备仍在采集”，也会影响后续任务驱动运行上下文。

建议 Cursor 修改：

1. `WebApiController` 不要只看 WMC 状态决定设备是否 acquisition。
2. `buildDeviceStatusArray()` 需要拿到 core runtime/communication 状态，至少满足：
   - 通信端点未打开时，设备显示 `Offline` 或 `Standby`，`acquisition=false`
   - WMC 状态只表示雷达工作模式，不单独代表上位机是否正在采集
3. 推荐做法：
   - 在 `AppHealth` 或 `WebApiController` 注入一个 `runtimeRunning/openEndpointCount` 查询函数
   - `device.acquisition = runtimeRunning && endpointOpen && (wmc == Starting || wmc == Working)`
4. 如果暂时不想引入 endpoint 级状态，也至少先用 `AppHealth.comm.openEndpoints > 0` 做总开关。

注意：

这里不是编译问题，而是状态语义问题。当前程序能运行，但 UI 展示的业务状态可能不可信。

### P2. “只有雷达设备允许绑定 endpoint”不应只放在 WebApiController

位置：

- `core/web/WebApiController.cpp`
- `core/business/DeviceRegistry.cpp`

现状：

Web API 层已经拒绝：

```cpp
if (d.kind != DeviceKind::Radar && !d.endpointId.isEmpty())
    return QStringLiteral("只有雷达设备允许绑定通信端点");
```

这对 UI 请求是有效的。

但 `DeviceRegistry::create()` 和 `DeviceRegistry::update()` 自身仍然允许传入：

- `kind != Radar`
- `endpointId != empty`

虽然 `refreshEndpointCache()` 只会缓存 Radar，暂时不会污染 `endpointId -> deviceId` 热路径，但数据库里仍可能沉淀非法业务数据。后续如果 TaskService、导入工具、迁移脚本、测试用例绕过 Web API 直接调用 Registry，就会破坏台账不变量。

建议 Cursor 修改：

1. 在 `DeviceRegistry` 层增加统一规范化/校验函数，例如：

```cpp
static DeviceInfo normalizeDevice(DeviceInfo dev)
{
    if (dev.kind != DeviceKind::Radar) {
        dev.endpointId.clear();
        dev.facesJson = QStringLiteral("[]");
    }
    return dev;
}
```

或者更严格：

```cpp
if (dev.kind != DeviceKind::Radar && !dev.endpointId.isEmpty()) {
    qWarning() << "[DeviceRegistry] 非雷达设备不允许绑定 endpoint";
    return false;
}
```

2. `create()`、`createAssigningId()`、`update()` 必须走同一套规则。
3. Web API 层可以继续保留友好错误提示，但 Registry 层必须兜底。

我更建议“严格拒绝”而不是静默清空，因为 endpoint 绑定属于业务约束，静默清空会让调用方误以为保存成功。

### P3. `AppController::shutdown()` 和 `SystemBus::shutdown()` 的边界需要写死

位置：

- `core/AppController.cpp`

现状：

析构函数是：

```cpp
AppController::~AppController()
{
    shutdown();
    SystemBus::getInstance().shutdown();
}
```

`shutdown()` 自身不关闭 SystemBus，只清理 Web、TrackDisplay、运行态、订阅等。

这个实现目前可以接受，因为它符合之前定下的原则：

- `stopRuntime()`：运行态停止，Web/Bus 保持
- `shutdown()`：AppController 资源释放
- `SystemBus::shutdown()`：进程退出级关闭

但后续如果有人把 `AppController::shutdown()` 当作“完整关闭系统”公开调用，容易误解为 Bus 也会关闭。

建议 Cursor 修改：

1. 在 `AppController.h` 给 `shutdown()` 加明确注释：
   - 它不是运行态 stop
   - 它不是 SystemBus shutdown
   - SystemBus 只在进程退出/析构最终阶段关闭
2. 如果未来 `shutdown()` 会被外部显式调用，则把 `SystemBus::shutdown()` 纳入一个更明确的 `shutdownProcess()` 或析构私有路径，避免语义混用。

这一项优先级低，但文档化后可以减少后续误改。

## 架构复盘

当前已经基本跑通了 Milestone 1 的主线：

- 前端不再自己生成最终设备 id
- 后端 `DeviceRegistry` 是设备台账 source of truth
- `endpointId` 是通信端点，不是业务设备 id
- `endpointId + sourceType -> deviceId -> taskId` 的数据归属链路已经进入代码
- core 运行态 stop 不再关闭 Web/Bus，符合管理系统式上位机的使用方式

现在不要继续扩功能，先把下面三个“边界钉子”钉牢：

1. runtime 状态和设备状态合成规则
2. DeviceRegistry 写入不变量
3. AppController 生命周期命名和注释

完成后再进入下一阶段会更稳。

## 接下来建议 Cursor 的实现顺序

1. 先修 P1：让 `/api/status.devices` 的 acquisition 受 core runtime/communication 状态约束。
2. 再修 P2：把非雷达 endpoint 约束下沉到 `DeviceRegistry`。
3. 最后补 P3 注释，不需要大改生命周期代码。

## 代码质量评估

当前代码质量比前几轮明显提高，可以评为 `B+`。

好的地方：

- 架构边界开始清晰：通信端点、业务设备、任务归属、Bus 生命周期不再混在一起。
- 小步修复方向正确，没有为了修 UI 把 backend source of truth 打穿。
- `DeviceRegistry` 的 endpoint 缓存只认 Radar，这一点和当前业务设计一致。

还没到 `A` 的原因：

- `WebApiController` 仍然承担过多业务编排，后续会越来越臃肿。
- 状态合成还缺统一模型，WMC 状态、通信状态、任务状态、设备展示状态之间仍是局部拼接。
- Registry 层业务不变量还不够硬，部分规则仍停留在 API 表层。

建议这一轮不要做大规模重构。等 P1/P2 补完后，再考虑把 `WebApiController` 拆成 Devices/Tasks/Runtime/Tracks 几个 route module。
