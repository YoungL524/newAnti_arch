# Review cec6fa4

## 结论

这一版是对上一轮状态模型的正式收口，方向是对的。

`AppHealth` 已经开始携带 endpoint 级快照，`WebApiController` 也彻底改成只读 `health`，不再通过外部 provider 实时查通信状态。  
这意味着 `/api/status` 和 WS 状态推送终于从“混合快照”走向了“单次采样、单次消费”。

当前没有发现必须阻断的严重问题。

## 本轮已修好的点

### 1. endpoint 状态已经进入 health 快照

位置：

- `core/AppTypes.h`
- `AppHealth::CommStatus::endpointOpen`
- `core/AppController.cpp`
- `AppController::healthReport()`

现在 `healthReport()` 会一次性采样：

```cpp
report.comm.endpointOpen = m_commManager->endpointOpenStates();
```

这一步很关键。  
它把“endpoint 当前是否打开”从运行时查询，变成了健康快照的一部分，后续 UI、日志、诊断都能基于同一份状态看问题。

### 2. WebApiController 已经只依赖 health

位置：

- `core/web/WebApiController.cpp`
- `buildDeviceStatusArray(const AppHealth& health)`

现在设备状态计算直接读：

```cpp
health.comm.endpointOpen.value(ep, false)
```

这比上一版的 provider 方案更稳，也更符合“状态快照一次采样”的思路。

### 3. CommunicationManager 补齐了快照接口

位置：

- `core/comm/CommunicationManager.h`
- `core/comm/CommunicationManager.cpp`

新增：

```cpp
QHash<QString, bool> endpointOpenStates() const;
```

这个接口本身没问题，和 `openEndpointCount()` 是互补关系：

- `openEndpointCount()` 适合健康概览
- `endpointOpenStates()` 适合设备级状态合成

### 4. legacy fallback 说明已经补上

位置：

- `core/web/WebApiController.cpp`

`resolveDeviceEndpointId()` 现在明确标注了 `radar_cpu` 是历史兼容逻辑。  
这点很好，避免后面的人把它误认为新规则。

## 仅有的轻量建议

### P3. `CommunicationManager::isEndpointOpen()` 可能逐渐变成冗余接口

位置：

- `core/comm/CommunicationManager.cpp`
- `core/comm/CommunicationManager.h`

现状：

这版真正被 `WebApiController` 使用的是：

```cpp
endpointOpenStates()
```

而 `isEndpointOpen()` 已经不再是主路径。

这不是 bug，只是一个小的维护信号：

- 如果后续没有其他调用点，`isEndpointOpen()` 可以保留
- 但如果长期不用，最好收敛掉，避免状态接口重复

我不建议现在删，因为它还挺有用，尤其是单元测试和局部查询场景。

## 架构判断

这一版的架构味道比前几版更稳定了：

- `AppHealth` 终于从“总览型指标”扩展成“总览 + endpoint 明细”
- `WebApiController` 不再自己到处查状态
- UI 的设备状态合成终于有了真正的单次快照来源

当前状态链条已经比较清楚：

1. `CommunicationManager` 采样 endpoint 状态
2. `AppController::healthReport()` 汇总成 `AppHealth`
3. `WebApiController` 基于 `AppHealth` 生成 `/api/status` 和 WS 推送

这条线已经接近我们想要的架构了。

## 代码质量评估

当前代码质量可以评为 `A- / B+` 之间，已经明显比前几轮更稳。

加分点：

- 状态模型终于闭合了。
- 责任边界更清晰，Web 层不再掺太多实时状态查询。
- `health` 成为统一快照入口，这对后续任务驱动和诊断非常有帮助。

可继续优化的点：

- `AppHealth` 现在开始承载更多细节，后续要注意别继续无限膨胀。
- `WebApiController` 仍然很重，但至少状态链条已经干净了。

## 下一步建议

这轮不需要再围绕状态快照大改了。  
下一步可以把精力放回任务驱动和数据归属：

1. 任务上下文与设备绑定规则再收一遍。
2. 航迹、日志、历史数据的任务归属链路再核一遍。
3. 再看 `WebApiController` 是否值得按路由域拆分。
