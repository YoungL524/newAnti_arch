# Review 5a73b6a

## 结论

这一版和修改报告基本一致：`TrackDisplayWebBridge::m_active` 已改为 `std::atomic_bool`，`DataPacket / BusEnvelope` 使用边界也补进了技术架构文档和 `ISystemBus` 注释。代码方向没有问题，可以认为 `review-d7a5ad6.md` 的前两项已经完成。

本轮主要剩下文档一致性和下一阶段任务设计问题。

## 已完成得比较好的点

- `core/web/TrackDisplayWebBridge.h` 已引入 `<atomic>`，`m_active` 改为 `std::atomic_bool m_active{false}`。
- `core/web/TrackDisplayWebBridge.cpp` 中 `start()/stop()/onBusEnvelope()/queued lambda` 都使用 `store/load`，跨线程状态读写已经收口。
- `doc/architecture/04-技术架构.md` 新增了 `SystemBus 载荷使用约定`，明确：
  - 小型控制包、固定协议包、通信下行继续用 `DataPacket + BusCallback`。
  - 大对象、显示 DTO、任务状态、告警批量、历史回放使用 `BusEnvelope + BusEnvelopeCallback`。
  - 新增面向 Web/UI 输出的数据默认用 `BusEnvelopeCallback`。
- `core/bus/ISystemBus.h` 的接口注释也补了同样的约定，代码接口和架构文档能互相指向。

## 建议修改

### 1. 05-核心流程.md 仍是旧航迹链路，和当前代码/架构冲突

位置：

- `doc/architecture/05-核心流程.md`
- `doc/architecture/05-核心流程.drawio`

当前 `05-核心流程.md` 的上行航迹显示流程仍写成：

```text
Adapter->>Track: TrackPoint direct signal
Track->>API: TrackDisplaySnapshot
API->>UI: WebSocket tracks
```

这已经不符合当前代码，也和我们刚刚固定的架构原则冲突。当前真实链路应该是：

```text
Radar/test_comm
  -> CommunicationManager
  -> RadarDataProcessor
  -> TrackDisplayManager
  -> SystemBus(BusEnvelope: TrackDisplaySnapshot)
  -> TrackDisplayWebBridge
  -> WebSocket
  -> React Web
```

需要把 `05-核心流程.md` 第 1 节更新掉：

- participant 里把 `WebApiController` 改成 `TrackDisplayWebBridge` 或 `WebBridge`。
- `Track->>API: TrackDisplaySnapshot` 改成 `Track->>Bus: publish BusEnvelope(TrackDisplaySnapshot)`。
- 增加 `Bus->>WebBridge: dispatch TrackDisplaySnapshot envelope`。
- `WebBridge->>UI: WebSocket tracks fixed DTO`。
- 关键原则里补一句：`TrackDisplayManager -> Web/UI` 必须经过 `SystemBus + TrackDisplayWebBridge`，不直接打 `WebApiController`。

`05-核心流程.drawio` 看起来已经比 md 新一些，里面出现了 `TrackDisplayWebBridge` 和 `BusEnvelope: TrackDisplaySnapshot`，但建议同步检查文字和图是否完全一致，避免 md 和 drawio 互相打架。

### 2. 02-应用架构.md / drawio 对内部事件层仍只写 DataPacket，建议同步为 DataPacket + BusEnvelope

位置：

- `doc/architecture/02-应用架构.md`
- `doc/architecture/02-应用架构.drawio`

当前应用架构里内部事件层写的是：

```text
SystemBus
DataPacket
```

drawio 里也是：

```text
内部事件通道
SystemBus / DataPacket
```

但现在总线基座已经明确升级为：

```text
SystemBus
DataPacket + BusEnvelope
```

建议同步修改：

- `02-应用架构.md` 的 Mermaid 节点改成 `SystemBus\nDataPacket + BusEnvelope`。
- `02-应用架构.drawio` 的节点文字改成 `SystemBus / DataPacket / BusEnvelope`。
- 内部事件层职责里补一句：`DataPacket` 用于小包和固定协议，`BusEnvelope` 用于大对象和 Web/UI 输出 DTO。

这不是代码 bug，但属于“架构定死”阶段必须避免的文档漂移。

### 3. 下一阶段建议正式设计 TrackInteractionCommand

位置：

- `core/web/WebApiController.cpp:105`
- `core/web/WebApiController.cpp:129`
- `core/web/WebApiController.h`
- `core/business/TrackDisplayManager.cpp`

当前 UI 航迹交互仍是：

```text
React Web
  -> WebApiController
  -> TrackDisplayManager::setInteraction()
```

这在修改报告里已经标为“未改，待下阶段设计总线命令后再改”，这个判断是合理的。建议下一阶段不要继续零散改 WebApiController，而是正式补一个命令模型：

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
```

目标链路：

```text
React Web
  -> WebApiController
  -> SystemBus(BusEnvelope: TrackInteractionCommand)
  -> TrackDisplayManager
  -> SystemBus(BusEnvelope: TrackDisplaySnapshot)
  -> TrackDisplayWebBridge
  -> React Web
```

这样 UI 读显示数据和写交互命令都通过总线，航迹显示这条链路才真正闭环。

## 可以暂时不改

### snapshot 全量排序性能

修改报告里把 snapshot 全量排序标为“review 标注可暂不改”，这个判断没问题。当前有全局 `kMaxSnapshotPoints = 8000`，而且单航迹点数不设上限是业务要求，不应回滚。

后续如果优化，方向仍然应该是全局水位、增量事件、视窗裁剪、历史落库和有序结构，而不是恢复单航迹硬上限。

## 建议 Cursor 下一步顺序

1. 同步修正 `doc/architecture/05-核心流程.md` 的航迹显示链路。
2. 同步修正 `doc/architecture/02-应用架构.md` 和 `02-应用架构.drawio` 的内部事件层表述。
3. 开始设计 `TrackInteractionCommand`，把 UI 航迹交互命令接入 `SystemBus + BusEnvelope`。

## 架构判断

`5a73b6a` 这一版可以认为完成了总线载荷模型的小收口。现在最大的风险不是代码，而是文档和代码表达不一致：`04-技术架构.md` 已经进入 `DataPacket + BusEnvelope` 新规则，但 `02-应用架构` 和 `05-核心流程.md` 还有旧链路。先把这些文档对齐，再进入 `TrackInteractionCommand` 会更稳。
