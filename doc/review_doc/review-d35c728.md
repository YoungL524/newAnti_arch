# Review d35c728

## 结论

这一版把 `review-b8f3486.md` 里的核心方向基本接住了：`TrackDisplaySnapshot` 已经不再通过 `DataPacket::rawData` 或 thread-local 旁路传递，而是升级为 `BusEnvelope + objectPayload + envelope callback`。这比上一版干净很多，也更接近我们要的“所有对 UI 输出的数据从总线出来”的架构。

本轮不再讨论编译问题。下面只看代码细节、架构边界和后续扩展风险。

## 已完成得比较好的点

- `build_err.txt` 和 `doc/.$系统架构.drawio.bkp` 已从仓库移除，`.gitignore` 也补了规则。
- `BusEnvelope` 不再写死 `trackSnapshot` 字段，改为 `payloadKind + std::shared_ptr<const void> objectPayload`。
- `SystemBus` 新增 `BusEnvelopeCallback`，`TrackDisplayWebBridge` 已改成 `subscribeBySourceEnvelope()`。
- `TrackDisplayWebBridge` 不再用 Qt slot 按值传递 `TrackDisplaySnapshot`，改成 lambda capture `shared_ptr`，避免了一次明显的大对象深拷贝。
- `TrackDisplayManager::deliverSnapshot()` 已改成右值参数，并用 `std::move` 构造 shared snapshot，方向正确。

## 必须修改

### 1. SubscriptionInfo 存在未初始化字段，后续会埋随机路由问题

位置：

- `core/bus/SystemBus.h:102`
- `core/bus/SystemBus.h:103`
- `core/bus/SystemBus.h:111`
- `core/bus/SystemBus.h:120`
- `core/bus/SystemBus.h:129`
- `core/bus/SystemBus.h:148`
- `core/bus/SystemBus.cpp:112`
- `core/bus/SystemBus.cpp:113`

`SubscriptionInfo` 里的 `sourceType` 和 `target` 是普通 `uint16_t`，但几个构造函数没有全部初始化它们：

- `BySource` 构造函数没有初始化 `target`。
- `ByTarget` 构造函数没有初始化 `sourceType`。
- wildcard 构造函数没有初始化 `sourceType` 和 `target`。

而 `registerSubscription()` 里会先读：

```cpp
const uint16_t sourceType = info.sourceType;
const uint16_t target = info.target;
```

即使后面通过 `hasSourceType/hasTarget` 判断是否使用，这个提前读取本身也已经读到了未初始化值。现在可能表现正常，但这是实打实的 C++ 未定义行为，后续很难排查。

修改建议：

```cpp
uint16_t sourceType = 0;
uint16_t target = 0;
BusCallback packetCallback;
BusEnvelopeCallback envelopeCallback;
bool hasSourceType = false;
bool hasTarget = false;
bool isWildcard = false;
```

同时把各构造函数里未用字段也显式初始化为 0。`registerSubscription()` 里也建议只在对应 flag 为 true 后再读对应字段。

验收标准：

- 任意一种订阅方式都不会读取未初始化字段。
- `subscribeBySourceEnvelope()`、`subscribeByTarget()`、`subscribeAll()` 的索引写入只依赖明确初始化的字段。

### 2. Envelope 订阅能力目前只支持按 source，接口不完整

位置：

- `core/bus/ISystemBus.h:65`
- `core/bus/SystemBus.h:42`
- `core/bus/SystemBus.cpp:305`

目前新接口只有：

```cpp
subscribeBySourceEnvelope(uint16_t sourceType, BusEnvelopeCallback callback)
```

这够 `TrackDisplayWebBridge` 使用，但从架构上看，它会让新老总线能力不对称：

- 旧 `DataPacket*` callback 支持 by source、by target、source + target、all。
- 新 `BusEnvelopeCallback` 只支持 by source。

短期没问题，但如果后面任务状态、告警、历史回放、设备状态也走大对象 payload，很快会出现“为了用 envelope，只能按 source 订阅”的绕路。

修改建议：

至少补齐这三个接口：

```cpp
SubscriptionId subscribeByTargetEnvelope(uint16_t target, BusEnvelopeCallback callback);
SubscriptionId subscribeEnvelope(uint16_t sourceType, uint16_t target, BusEnvelopeCallback callback);
SubscriptionId subscribeAllEnvelope(BusEnvelopeCallback callback);
```

也可以进一步把 `SubscriptionInfo` 抽象成统一 callback variant，但不建议现在大改。当前先补齐接口对称性就够。

### 3. payloadAs() 没有校验 payloadKind，调用方误用时会静默拿错类型

位置：

- `core/bus/BusEnvelope.h:28`
- `core/web/TrackDisplayWebBridge.cpp:55`
- `core/web/TrackDisplayWebBridge.cpp:58`

当前 `payloadAs<T>()` 只是：

```cpp
return std::static_pointer_cast<const T>(objectPayload);
```

`TrackDisplayWebBridge` 调用前手动判断了 `payloadKind`，所以当前路径是对的。但这个 API 本身没有把类型和 `payloadKind` 绑定起来，后续其他调用方一旦忘记判断，就会把任意 `shared_ptr<const void>` 静态转换成错误类型。

修改建议：

提供带 kind 校验的接口：

```cpp
template<typename T>
std::shared_ptr<const T> payloadAs(BusPayloadKind expected) const
{
    if (payloadKind != expected) {
        return {};
    }
    return std::static_pointer_cast<const T>(objectPayload);
}
```

然后调用方写成：

```cpp
auto snapPtr = env.payloadAs<TrackDisplaySnapshot>(BusPayloadKind::TrackDisplaySnapshot);
```

如果后面要更严谨，可以用 traits 绑定 `T -> BusPayloadKind`，但现在不必复杂化。

### 4. WebApiController 仍直接持有 TrackDisplayManager，控制命令链路还没进入总线

位置：

- `core/web/WebApiController.h:38`
- `core/web/WebApiController.h:62`
- `core/web/WebApiController.cpp:129`

这一点不是当前 bug，但属于下一步架构边界要收口的地方。

现在航迹“显示数据输出”已经走了：

```text
TrackDisplayManager -> SystemBus -> TrackDisplayWebBridge -> React Web
```

但 UI 对航迹的交互命令仍是：

```text
React Web -> WebApiController -> TrackDisplayManager::setInteraction()
```

这会让数据流和控制流分裂。短期能工作，但长期会有两个问题：

- `WebApiController` 仍然知道具体业务对象 `TrackDisplayManager`，Web 层和业务显示管理层没有完全隔开。
- 后续如果要记录用户操作、做权限校验、回放操作、跨端同步交互状态，这条直连链路不方便统一治理。

建议下一步不要直接把这个当小修补，而是设计一个总线命令：

```cpp
BusPayloadKind::TrackInteractionCommand
```

包含：

```cpp
deviceId
trackId
selected
trackingEnabled
operatorId / source 可后续补
timestamp
```

链路变成：

```text
React Web
  -> WebApiController
  -> SystemBus(TrackInteractionCommand)
  -> TrackDisplayManager
  -> SystemBus(TrackDisplaySnapshot)
  -> TrackDisplayWebBridge
  -> React Web
```

这样 UI 读数据和写命令都通过总线，架构会更闭合。

## 建议优化

### 1. BusEnvelope 工厂函数建议放进命名空间或静态成员

位置：

- `core/bus/BusEnvelope.h:35`
- `core/bus/BusEnvelope.cpp:5`
- `core/business/TrackDisplayManager.cpp:160`

当前是全局函数：

```cpp
BusEnvelope makeTrackDisplaySnapshot(...)
```

建议改成：

```cpp
BusEnvelope BusEnvelope::makeTrackDisplaySnapshot(...)
```

或者放进明确命名空间：

```cpp
namespace bus {
BusEnvelope makeTrackDisplaySnapshot(...);
}
```

原因是后续 payload 工厂函数会越来越多，全局命名空间容易散。

### 2. BusEnvelopeCallback 的线程语义也要写清楚

位置：

- `core/bus/ISystemBus.h:22`

`BusCallback` 已经写了“回调运行在 SystemBus 内部工作线程”的 warning，但 `BusEnvelopeCallback` 没有同等说明。建议补齐，避免后面有人在 envelope callback 里直接操作 QObject、SQLite 或 UI 相关对象。

### 3. TrackDisplayManager 的 snapshot 构造成本后续要单独评估

位置：

- `core/business/TrackDisplayManager.cpp:92`
- `core/business/TrackDisplayManager.cpp:124`
- `core/business/TrackDisplayManager.cpp:132`

这次已经减少了跨总线传递时的深拷贝，但 `snapshot()` 内部仍会：

- 遍历所有缓存点。
- 构造 `candidates`。
- 按时间倒序排序。
- resize 到全局上限。
- 再按时间正序排序。

这不是当前必须改的问题，因为全局 `kMaxSnapshotPoints = 8000` 还可控。但后续如果航迹数量上来，这里会成为显示链路的主要 CPU 成本。

注意：不要恢复“单航迹点数上限”，这是业务侧明确不要的。后续优化方向应该是：

- 全局 snapshot 水位。
- 增量显示事件。
- 前端视窗裁剪。
- 历史轨迹落库后按需回放。
- `TrackDisplayManager` 内维护可直接输出的时间有序结构，减少每 500ms 全量排序。

## 建议 Cursor 的下一步顺序

1. 先修 `SubscriptionInfo` 字段初始化问题，这是本轮最实的代码风险。
2. 给 `payloadAs()` 增加 `payloadKind` 校验，避免后续误用。
3. 补齐 envelope 订阅接口的 by target、source + target、all 版本。
4. 把 `makeTrackDisplaySnapshot()` 收口到 `BusEnvelope` 静态成员或 `bus` 命名空间。
5. 设计 `TrackInteractionCommand`，把 UI 航迹交互命令也接入总线。

## 架构判断

当前方向可以继续往下走。`d35c728` 已经把“UI 航迹显示数据必须来自总线”这条主原则落下来了；下一步重点不是继续抠 WebSocket，而是把“UI 发出的交互命令也通过总线进入业务层”补齐。这样航迹显示这条链路才算真正闭环。
