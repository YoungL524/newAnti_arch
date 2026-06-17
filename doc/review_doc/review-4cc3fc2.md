# Review 4cc3fc2

## 结论

这一版把 `TrackInteractionCommand` 做进来了，方向正确。`WebApiController` 已不再直接持有 `TrackDisplayManager`，而是发布 `BusEnvelope(TrackInteractionCommand)`；`TrackDisplayManager` 订阅该命令后更新 interaction，并继续发布 `TrackDisplaySnapshot`。这说明航迹显示模块已经从“数据输出走总线”推进到“UI 交互命令也走总线”，架构闭环基本成立。

本轮没有发现需要推翻实现的大问题，但有两个建议尽快补齐：一个是停止/清理时已排队命令的护栏，另一个是 REST 返回语义要说清楚。

## 已完成得比较好的点

- `TrackInteractionCommand` 已加入 `TrackTypes.h`，字段包含 `deviceId / trackId / selected / trackingEnabled / timestamp`。
- `BusPayloadKind::TrackInteractionCommand` 和 `PAYLOAD_TYPE_TRACK_INTERACTION_COMMAND` 已补齐。
- `BusEnvelope::makeTrackInteractionCommand()` 已实现，source/target 清晰：

```cpp
SOURCE_WEB_API -> DISPATCH_TRACK_DISPLAY_MANAGER
```

- `WebApiController` 已移除 `TrackDisplayManager* m_trackMgr`，不再直接调用 `setInteraction()`。
- `TrackDisplayManager::setInteraction()` 已从 public 收回 private，边界更干净。
- `TrackDisplayManager::onInteractionCommand()` 在总线 worker 回调中只取 payload，然后用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回到 owner 线程更新状态，这点是对的。
- `doc/architecture/05-核心流程.md/drawio` 已补充“航迹交互命令流程”，和代码方向一致。

## 建议修改

### 1. TrackDisplayManager 停止后，已排队的 TrackInteractionCommand 仍可能执行

位置：

- `core/business/TrackDisplayManager.cpp:36`
- `core/business/TrackDisplayManager.cpp:43`
- `core/business/TrackDisplayManager.cpp:181`
- `core/business/TrackDisplayManager.cpp:190`

当前 `stop()` 会取消总线订阅：

```cpp
SystemBus::getInstance().unsubscribe(m_cmdSubId);
m_cmdSubId = 0;
```

但如果某个 `TrackInteractionCommand` 已经从总线 worker 进入 `onInteractionCommand()`，并且已经通过 `QMetaObject::invokeMethod()` 排队到主线程，那么即使随后调用了 `stop()` / `clear()`，这个 queued lambda 仍可能继续执行：

```cpp
guard->setInteraction(...);
```

这会导致停止/清理后又写入 `m_interactions`，甚至发布一次新的 `TrackDisplaySnapshot`。概率不高，但属于生命周期边界上的小口子。

修改建议：

- 增加一个命令接收状态，例如 `std::atomic_bool m_acceptCommands{false}`，`start()` 置 true，`stop()` 置 false。
- `onInteractionCommand()` 入口和 queued lambda 内都检查该状态。
- 如果不想引入 atomic，也可以在 queued lambda 中检查 `m_cmdSubId != 0`，因为 lambda 回到 owner 线程执行；但入口在总线 worker 线程，入口检查仍建议用 atomic。

示意：

```cpp
if (!m_acceptCommands.load(std::memory_order_acquire)) {
    return;
}

QMetaObject::invokeMethod(this, [guard, cmdPtr]() {
    if (!guard || !guard->m_acceptCommands.load(std::memory_order_acquire)) {
        return;
    }
    guard->setInteraction(...);
}, Qt::QueuedConnection);
```

这和之前 `TrackDisplayWebBridge::m_active` 的问题是同一类：跨线程回调 + queued lambda，需要一个明确的 stop gate。

### 2. WebApiController 的交互 API 返回语义需要明确：ok 代表“命令已发布”，不是“状态已应用”

位置：

- `core/web/WebApiController.cpp:124`
- `core/web/WebApiController.cpp:134`
- `doc/architecture/05-核心流程.md`

现在 REST 处理逻辑是：

```cpp
SystemBus::getInstance().publish(BusEnvelope::makeTrackInteractionCommand(...))
return { ok: true, interaction: ... }
```

由于命令是异步总线投递，`ok:true` 只能说明命令成功发布到总线队列，并不保证：

- `TrackDisplayManager` 已经收到；
- interaction 状态已经更新；
- snapshot 已经推送给 UI；
- 对应的 track 当前存在。

这在架构上可以接受，但 API 语义要写清楚，避免前端或后续开发把 REST response 当成最终状态。

修改建议：

- 返回字段可以更明确，例如：

```json
{
  "ok": true,
  "accepted": true,
  "mode": "queued",
  "interaction": {}
}
```

- 或者至少在 `05-核心流程.md` 的航迹交互命令流程里补一句：REST 返回表示命令已入队，最终显示状态以 WebSocket snapshot 为准。
- 如果后续需要强一致反馈，再设计 command ack，不要在当前 REST handler 里同步等待业务层处理。

### 3. TrackDisplayManager.h 对 bus 头文件依赖可以再轻一点

位置：

- `core/business/TrackDisplayManager.h`

当前头文件直接 include：

```cpp
#include "bus/BusEnvelope.h"
#include "bus/ISystemBus.h"
```

这是可工作的，但会让业务头文件暴露更多总线细节。现在 `TrackDisplayManager.h` 只需要：

- `BusEnvelope` 类型用于私有函数参数；
- `SubscriptionId` 用于成员字段。

建议后续可以考虑：

```cpp
struct BusEnvelope;
using SubscriptionId = uint32_t; // 或改成员为 uint32_t
```

然后把 `BusEnvelope.h / ISystemBus.h` 放到 `.cpp` 里。这个不是必须修，只是可以降低编译依赖和业务头文件的总线暴露面积。

## 可以暂时不改

### API 路径仍挂在 `/api/devices/{id}/tracks/{trackId}/interaction`

这个路径虽然看起来像设备 API 的子路径，但它表达的是“设备下某条航迹的交互状态”，当前可以接受。后续如果航迹 API 扩展很多，再考虑单独抽成 `/api/tracks/...`，现在不需要为路径重构打断主线。

## 建议 Cursor 下一步顺序

1. 给 `TrackDisplayManager` 增加命令接收 stop gate，避免 stop/clear 后已排队命令继续落地。
2. 明确航迹交互 REST response 的语义：发布成功，不代表已应用；最终状态以 WebSocket snapshot 为准。
3. 可选：降低 `TrackDisplayManager.h` 对 bus 头文件的直接依赖。

## 架构判断

`4cc3fc2` 是一个重要节点：航迹显示的数据输出和 UI 交互命令都已经进入 `SystemBus + BusEnvelope`。从架构上看，这条链路已经闭合。下一步不建议继续大改航迹主链路，而是补生命周期和 API 语义这类护栏，然后可以转向任务状态机或设备/任务关联这些更上层的框架能力。
