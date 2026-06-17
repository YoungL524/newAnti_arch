# Review 701dda7

## 结论

这一版主要是架构文档对齐，方向正确。`02-应用架构` 已统一到 `SystemBus / DataPacket / BusEnvelope`，`05-核心流程` 也把航迹显示链路改成了 `TrackDisplayManager -> SystemBus -> TrackDisplayWebBridge -> React Web`，和当前代码及前面定下的原则一致。

本轮没有发现新的架构阻塞问题。下一步可以进入 `TrackInteractionCommand` 总线闭环设计。

## 已完成得比较好的点

- `doc/architecture/05-核心流程.md` 已把 participant 从 `WebApiController` 改成 `TrackDisplayWebBridge`。
- 上行航迹显示流程已改为：

```text
Track -> Bus: publish BusEnvelope(TrackDisplaySnapshot)
Bus -> WebBridge: dispatch TrackDisplaySnapshot envelope
WebBridge -> UI: WebSocket tracks fixed DTO
```

- 关键原则已补充：`TrackDisplayManager -> Web/UI` 必须经过 `SystemBus + TrackDisplayWebBridge`，不直接打 `WebApiController`。
- `doc/architecture/05-核心流程.drawio` 已同步为 `TrackDisplayWebBridge` 和 `BusEnvelope: TrackDisplaySnapshot`。
- `doc/architecture/02-应用架构.md` 和 `02-应用架构.drawio` 已把内部事件层更新为 `DataPacket + BusEnvelope`。
- 内部事件层职责里已经说明：`DataPacket` 用于小包和固定协议，`BusEnvelope` 用于大对象和 Web/UI 输出 DTO。

## 建议修改

### 1. BusEnvelope.h 末尾有多余空白且缺少文件末尾换行

位置：

- `core/bus/BusEnvelope.h`

这次 `BusEnvelope.h` 只多了一行尾部空白，并且文件末尾没有正常 newline：

```diff
 };
+ 
\ No newline at end of file
```

这不是架构问题，也不影响业务，但属于很容易积累的文件卫生问题。建议 Cursor 顺手修掉：

- 删除末尾多余空格。
- 保证文件以单个换行结束。

### 2. TrackInteractionCommand 可以进入正式设计阶段

位置：

- `core/web/WebApiController.cpp:129`
- `core/business/TrackDisplayManager.cpp`
- `core/bus/BusEnvelope.h`
- `core/business/TrackTypes.h` 或新的 command 类型文件

当前文档和数据输出链路已经对齐，下一步可以做 UI 航迹交互命令入总线。现在仍然是：

```text
React Web
  -> WebApiController
  -> TrackDisplayManager::setInteraction()
```

目标应改成：

```text
React Web
  -> WebApiController
  -> SystemBus(BusEnvelope: TrackInteractionCommand)
  -> TrackDisplayManager
  -> SystemBus(BusEnvelope: TrackDisplaySnapshot)
  -> TrackDisplayWebBridge
  -> React Web
```

建议新增命令结构：

```cpp
struct TrackInteractionCommand {
    QString deviceId;
    uint32_t trackId = 0;
    bool selected = false;
    bool trackingEnabled = false;
    qint64 timestamp = 0;
};
```

并新增：

```cpp
BusPayloadKind::TrackInteractionCommand
BusEnvelope::makeTrackInteractionCommand(...)
```

`TrackDisplayManager` 订阅该 envelope 后再调用内部的 interaction 更新逻辑。这样 Web 层只负责校验请求和发布命令，不再直接调用业务对象方法。

## 可以暂时不改

### Processor -> TrackDisplayManager 直连

`05-核心流程.md` 保留了 `Adapter -> TrackDisplayManager: TrackPoint direct signal`，这是目前我们允许的性能优化点：高频内部点流可以在 core 内部直连，关键是 `TrackDisplayManager -> Web/UI` 不能直连，必须走 `SystemBus + TrackDisplayWebBridge`。当前文档已经把这个边界写清楚了，可以保留。

## 建议 Cursor 下一步顺序

1. 清理 `core/bus/BusEnvelope.h` 文件末尾空白和 newline。
2. 设计 `TrackInteractionCommand` 类型和 BusEnvelope 工厂函数。
3. 让 `WebApiController` 发布 `TrackInteractionCommand`，而不是直接调用 `TrackDisplayManager::setInteraction()`。
4. 让 `TrackDisplayManager` 订阅 `TrackInteractionCommand`，内部更新 interaction 后继续发布 `TrackDisplaySnapshot`。
5. 同步更新 `05-核心流程.md/drawio`，补一条“航迹交互命令流程”。

## 架构判断

`701dda7` 可以认为完成了“航迹显示数据输出链路”的文档和代码对齐。现在系统的下一块拼图不是继续改 snapshot，而是把 UI 交互命令也纳入总线，这样航迹显示模块才从数据输出到控制输入都闭环。
