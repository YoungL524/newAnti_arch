# Review d7a5ad6

## 结论

这一版基本完成了 `review-d35c728.md` 里对总线载荷模型的修正：`SubscriptionInfo` 未初始化字段已处理，`BusEnvelopeCallback` 订阅接口也补齐了 by source、by target、source + target、all，`payloadAs()` 增加了 `payloadKind` 校验。整体方向正确，可以继续往下一步推进。

本轮没有发现需要立刻推翻当前实现的架构问题。下面是建议继续收口的点。

## 已完成得比较好的点

- `SubscriptionInfo` 的 `sourceType/target/hasSourceType/hasTarget/isWildcard` 已提供默认值，上一轮的未初始化字段风险已消除。
- `registerSubscription()` 不再提前读取未确认有效的 source/target 字段。
- envelope 订阅接口已经补齐：
  - `subscribeBySourceEnvelope()`
  - `subscribeByTargetEnvelope()`
  - `subscribeEnvelope()`
  - `subscribeAllEnvelope()`
- `BusEnvelope::makeTrackDisplaySnapshot()` 已收口为静态成员，不再散落在全局命名空间。
- `BusEnvelopeCallback` 的线程语义说明已经补上。
- `TrackDisplayWebBridge` 现在使用：

```cpp
env.payloadAs<TrackDisplaySnapshot>(BusPayloadKind::TrackDisplaySnapshot)
```

这比上一版手动判断 kind 再裸 `static_pointer_cast` 更稳。

## 建议修改

### 1. TrackDisplayWebBridge::m_active 存在跨线程读写，建议改成 atomic 或消除该状态

位置：

- `core/web/TrackDisplayWebBridge.cpp:27`
- `core/web/TrackDisplayWebBridge.cpp:46`
- `core/web/TrackDisplayWebBridge.cpp:55`
- `core/web/TrackDisplayWebBridge.cpp:64`
- `core/web/TrackDisplayWebBridge.h`

`TrackDisplayWebBridge` 的回调运行在 `SystemBus` worker 线程，而 `start()/stop()` 通常运行在 owner 线程。当前 `m_active` 是普通 `bool`：

```cpp
m_active = true;
m_active = false;
if (!m_active) return;
```

这在语义上是“停止后不要再处理 snapshot”，但实现上是跨线程读写普通 bool，属于数据竞争风险。现在大概率不容易显性出错，因为 `QPointer` 和 `unsubscribe()` 已经兜了一部分生命周期，但这个状态位本身还是不严谨。

修改建议：

- 简单方案：把 `m_active` 改成 `std::atomic_bool`。
- 更好的方案：减少状态位依赖，`stop()` 只负责 unsubscribe，queued lambda 里通过 `m_subId != 0 && m_ws` 判断是否仍有效。
- 如果保留 `m_active`，建议统一用 `load/store`，不要混用普通读写。

示意：

```cpp
std::atomic_bool m_active{false};
```

```cpp
m_active.store(true, std::memory_order_release);
m_active.store(false, std::memory_order_release);
if (!m_active.load(std::memory_order_acquire)) {
    return;
}
```

这个问题不影响当前架构方向，但建议尽快修掉，避免后续 Web 推送链路偶发难查问题。

### 2. SystemBus 已经形成 DataPacket callback 和 BusEnvelope callback 双轨，后续要定迁移规则

位置：

- `core/bus/ISystemBus.h`
- `core/bus/SystemBus.h`
- `core/bus/SystemBus.cpp`

现在总线同时支持：

```cpp
BusCallback = std::function<void(const DataPacket*)>;
BusEnvelopeCallback = std::function<void(const BusEnvelope&)>;
```

这符合当前兼容需求：DPH、通信转发等小包还可以继续用 `DataPacket*`，航迹 snapshot 这类大对象走 `BusEnvelope`。但如果不定规则，后续容易变成“想起来就用 Envelope，忘了就用 DataPacket”的混乱状态。

建议补一段总线使用约定到架构文档或代码注释：

- 小型控制包、固定协议包、通信下行包：允许继续用 `DataPacket`。
- 跨业务模块的大对象、显示 DTO、任务状态、告警批量、历史回放：必须使用 `BusEnvelope`。
- 新增面向 Web/UI 输出的数据：默认使用 `BusEnvelopeCallback`。
- 旧 `DataPacket*` callback 作为兼容层保留，但不再承载复杂业务对象。

这个规则最好写进 `doc/architecture/04-技术架构.md` 或 `05-核心流程.md`，否则代码接口虽然有了，团队使用习惯还是会漂。

### 3. 仍建议把 UI 航迹交互命令也接入总线

位置：

- `core/web/WebApiController.cpp`
- `core/web/WebApiController.h`
- `core/business/TrackDisplayManager.cpp`

当前本轮没有处理这点，可以放到下一阶段。现在链路是：

```text
React Web -> WebApiController -> TrackDisplayManager::setInteraction()
```

数据显示已经走总线，但 UI 操作命令还没有走总线。建议下一步新增：

```cpp
BusPayloadKind::TrackInteractionCommand
```

链路改成：

```text
React Web
  -> WebApiController
  -> SystemBus(TrackInteractionCommand)
  -> TrackDisplayManager
  -> SystemBus(TrackDisplaySnapshot)
  -> TrackDisplayWebBridge
  -> React Web
```

这样“UI 发命令”和“UI 收显示数据”都统一由总线治理，后面做操作日志、权限、回放、跨端同步都会更自然。

## 可以暂时不改

### snapshot 全量排序成本

`TrackDisplayManager::snapshot()` 当前仍然每 500ms 做全量候选构造和排序。这个确实会成为后续性能观察点，但现在还不用急着重构。因为当前已经有全局 `kMaxSnapshotPoints = 8000`，而且用户明确要求不要恢复单航迹点数上限。

后续如果要优化，方向应该是：

- 全局水位策略。
- 增量显示事件。
- 前端视窗裁剪。
- 历史轨迹落库后按需回放。
- 维护可直接输出的时间有序结构，减少周期性全量排序。

不要把“单航迹点数上限”重新加回来。

## 建议 Cursor 下一步顺序

1. 把 `TrackDisplayWebBridge::m_active` 改成线程安全状态，或消除该状态位。
2. 在架构文档里补充 `DataPacket` 和 `BusEnvelope` 的使用边界。
3. 设计并实现 `TrackInteractionCommand`，把 UI 航迹交互命令也纳入总线。

## 架构判断

`d7a5ad6` 可以认为总线大对象载荷这一小阶段基本完成。接下来不要继续在 `BusEnvelope` 本身上打转，重点应该转向“UI 命令入总线”和“任务/设备/航迹流程闭环”。
