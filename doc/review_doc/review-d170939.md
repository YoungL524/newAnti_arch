# Review d170939

## 结论

这一版继续沿着正确方向推进，主要落实了 `review-90a0da1.md` 里的两个建议：

1. `CommunicationManager` 增加了 endpoint 级 `isEndpointOpen(endpointId)`。
2. `WebApiController::buildDeviceStatusArray()` 改成接收 `AppHealth` 参数，调用处不再自己重复调用 `m_health()`。
3. `deviceStatusFromWmc()` 重命名为 `composeRadarDeviceStatus()`，语义比上一版更准确。

当前没有发现需要阻断的严重问题。  
但这版有一个“实现完成了一半”的地方：`buildDeviceStatusArray(const AppHealth& health)` 传入了 health，却没有真正使用 health，仍然通过 `m_isEndpointOpen()` 实时查询 endpoint 状态。

这不影响当前单雷达运行，但说明状态快照模型还没完全闭合。

## 本轮已修好的点

### 1. endpoint 级在线判断已经补上

位置：

- `core/comm/CommunicationManager.h`
- `core/comm/CommunicationManager.cpp`

新增：

```cpp
bool CommunicationManager::isEndpointOpen(const QString& endpointId) const
{
    Endpoint* ep = endpoint(endpointId);
    return ep && ep->isOpen();
}
```

这是正确的。  
设备状态不再只能依赖 `openEndpoints > 0` 这种全局通信状态，后续扩展多雷达、多 endpoint 会更顺。

### 2. WebApiController 通过 provider 查询 endpoint 状态

位置：

- `core/AppController.cpp`
- `core/web/WebApiController.h`
- `core/web/WebApiController.cpp`

`AppController` 构造 `WebApiController` 时注入：

```cpp
[cm = m_commManager.get()](const QString& endpointId) {
    return cm && cm->isEndpointOpen(endpointId);
}
```

这个依赖方向是可以接受的：WebApiController 不直接持有 CommunicationManager，而是通过查询函数获取 endpoint 状态。  
在当前对象生命周期里，`m_webApi` 的析构早于 `m_commManager`，这个 raw pointer 捕获不会形成明显悬空风险。

### 3. 设备状态合成函数命名更清楚

位置：

- `core/web/WebApiController.cpp`
- `composeRadarDeviceStatus()`

现在函数名不再暗示“设备状态只来自 WMC”，而是表达“合成雷达设备状态”。  
这符合当前架构：UI 展示状态 = WMC 状态 + endpoint 状态 + 设备映射关系。

## 仍需优化的问题

### P2. `buildDeviceStatusArray(const AppHealth& health)` 传入了 health，但完全没有使用

位置：

- `core/web/WebApiController.cpp`
- `WebApiController::buildDeviceStatusArray(const AppHealth& health)`

现状：

```cpp
QJsonArray WebApiController::buildDeviceStatusArray(const AppHealth& health) const
{
    Q_UNUSED(health);
    ...
    return m_isEndpointOpen && m_isEndpointOpen(ep);
}
```

上一轮建议传入 `health` 的核心目的，是让同一次 `/api/status` 或 WS status 推送使用同一个状态快照。  
现在虽然函数签名接收了 `health`，但实际 endpoint 状态仍然是实时查询出来的。

这意味着理论上仍可能出现同一次响应内的状态不一致：

- `connected` 来自 `health.comm.openEndpoints`
- `devices[].acquisition` 来自稍后实时查询的 `isEndpointOpen(endpointId)`

在当前场景中概率很低，不算严重 bug。  
但如果后续 endpoint 状态变化更频繁，或者多个 endpoint 同时存在，这个问题会变明显。

建议 Cursor 后续修改：

1. 不要只在 `AppHealth::CommStatus` 里放总数。
2. 增加 endpoint 级快照，例如：

```cpp
struct EndpointStatus {
    QString endpointId;
    bool open = false;
};
QVector<EndpointStatus> endpoints;
```

或者先简单使用：

```cpp
QHash<QString, bool> endpointOpen;
```

3. `AppController::healthReport()` 一次性采样所有 endpoint 状态。
4. `buildDeviceStatusArray(const AppHealth& health)` 只读取 `health` 中的 endpoint 快照，不再实时调用 `m_isEndpointOpen()`。

短期如果不想改 `AppHealth`，可以接受当前 provider 方案；但函数参数就不要叫 `health` 或者不要保留 `Q_UNUSED(health)`，否则容易让维护者误以为已经实现了严格快照一致性。

### P2. `/api/status.acquisition` 仍然是全局单控制器语义

位置：

- `core/web/WebApiController.cpp`
- `/api/status`
- WS status timer

现状：

顶层字段仍然使用：

```cpp
const bool commActive = health.comm.openEndpoints > 0;
s["acquisition"] = commActive && wmcAcquisitionActive(st2);
```

这对当前单 radar_cpu 控制器是合理的。  
但从长期架构看，顶层 `acquisition` 不是某个设备的状态，而是一个全局概览。

建议：

1. 保留顶层 `acquisition` 作为兼容字段可以。
2. 前端实际设备展示应优先使用 `devices[].acquisition`。
3. 后续多设备阶段，顶层 `acquisition` 需要明确语义：
   - 是否任意雷达采集中？
   - 是否当前任务绑定设备采集中？
   - 是否 radar_cpu 控制器采集中？

这条不要求马上改代码，但要在接口文档里写清楚，避免前端继续把顶层 acquisition 当成所有设备状态。

### P3. `resolveDeviceEndpointId()` 仍是 legacy 兼容逻辑，后续应收口

位置：

- `core/web/WebApiController.cpp`

现状：

```cpp
if (!endpointId.isEmpty())
    return endpointId;
if (deviceId == "radar_cpu")
    return "radar_cpu";
```

这个兼容逻辑目前可以保留，因为历史上确实存在 `deviceId == radar_cpu` 的 legacy 设备。  
但从新架构看，业务设备应该通过 `DeviceRegistry.endpointId` 显式绑定通信端点。

建议：

1. 保留兼容逻辑，但加注释说明这是 legacy fallback。
2. 新增/编辑设备时继续推动所有可控雷达都显式写入 `endpointId`。
3. 等历史数据迁移稳定后，再考虑移除 `deviceId == radar_cpu` 的特殊判断。

## 架构判断

这一版仍然符合我们当前定下的架构方向：

- 通信 endpoint 是基础设施入口。
- 业务设备由 `DeviceRegistry` 管理。
- UI 设备状态由 core 合成，不由前端自行推断。
- WebApiController 通过 provider 查询底层状态，没有直接接管 CommunicationManager 生命周期。

现在真正要补齐的是“状态快照模型”：

1. `AppHealth` 目前只有 endpoint 总数，没有 endpoint 明细。
2. `WebApiController` 已经开始需要 endpoint 明细。
3. 因此 endpoint 状态应该正式进入 `AppHealth`，而不是继续通过额外 provider 零散查询。

我的建议是：不要马上大拆 WebApiController，下一步先把 `AppHealth::CommStatus` 补完整。  
等状态数据模型稳定后，再拆 route module 会更自然。

## 代码质量评估

当前代码质量继续维持 `B+`，局部接近 `A-`。

加分点：

- 这轮改动很克制，基本只围绕上一轮 review 的两个点。
- endpoint 级查询是正确方向。
- WebApiController 没有直接依赖 CommunicationManager，依赖注入方式还算干净。

扣分点：

- `buildDeviceStatusArray(const AppHealth& health)` 的签名和实现不一致，`Q_UNUSED(health)` 是一个明显的设计味道。
- `AppHealth` 的通信状态模型还偏粗，只适合全局健康概览，不足以支撑设备级 UI 状态。
- legacy `radar_cpu` 特殊逻辑还分散在 WebApiController 内，后续需要逐步收口。

## 建议 Cursor 下一步

1. 给 `AppHealth::CommStatus` 增加 endpoint 级状态快照。
2. `AppController::healthReport()` 负责一次性采样 endpoint 状态。
3. `WebApiController::buildDeviceStatusArray(health)` 只使用传入的 health，不再实时查 `m_isEndpointOpen()`。
4. 给 `resolveDeviceEndpointId()` 的 `deviceId == radar_cpu` fallback 加 legacy 注释。

这几步做完后，设备状态这条线就比较稳了，可以继续往任务驱动运行上下文推进。
