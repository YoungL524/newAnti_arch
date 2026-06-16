# Review: ea058c0

## 背景结论
这次不只是修图，而是要把“航迹显示数据如何进入 UI”这条架构边界定死。

最终原则如下：

```text
React Web 不直接依赖业务对象。
WebApi / WebSocket 不直接监听 TrackDisplayManager。
UI 想获取航迹显示数据，必须经过后端总线事件出口。
```

也就是说，正确链路应调整为：

```text
CommunicationManager
  -> RadarDataProcessor
  -> TrackDisplayManager
  -> SystemBus
  -> WebApiController / WebPushAdapter
  -> React Web
```

当前代码中这条链路还不符合该原则。

## 问题 1
`TrackDisplayManager -> WebSocket` 当前是直连。

位置：
`core/AppController.cpp`

当前代码在 `startWeb()` 附近直接连接：

```cpp
TrackDisplayManager::snapshotReady
  -> AppController lambda
  -> WebService::broadcast(...)
```

这会让 Web 推送层直接依赖业务对象信号，绕过了总线，也破坏了“前后端分离 + 后端统一事件出口”的架构边界。

需要修改为：

```text
TrackDisplayManager::snapshotReady
  -> publish TrackDisplaySnapshot event to SystemBus
  -> WebPushAdapter / WebApiController subscribe SystemBus
  -> WebService::broadcast(...)
```

注意：Web 层可以订阅总线，但不能直接挂 `TrackDisplayManager`。

## 问题 2
`DataPacket` 当前设计不适合承载 `TrackDisplaySnapshot`。

位置：
`core/bus/GenericPackets.h`

当前 `DataPacket` 是固定 payload：

```cpp
uint8_t rawData[MAX_PAYLOAD_SIZE];
MAX_PAYLOAD_SIZE = 4096;
```

这适合小型协议包、控制包、状态包，但不适合航迹显示快照。  
`TrackDisplaySnapshot` 可能包含大量点，不能硬塞进 4096 字节 `DataPacket`。

需要优化 `DataPacket` / 总线载荷设计，建议方向：

### 方案 A：扩展 DataPacket 支持动态 payload

保留现有小包能力，同时增加动态载荷：

```cpp
struct DataPacket {
    ...
    QByteArray dynamicPayload;
    uint32_t payloadType;
}
```

小包仍可走 fixed payload，大对象走 `dynamicPayload`。  
注意要明确拷贝成本、生命周期、线程安全。

### 方案 B：引入业务事件总线类型

新增更适合 Qt/C++ 对象的业务事件：

```cpp
struct BusEvent {
    uint16_t sourceType;
    uint32_t eventType;
    uint64_t sequence;
    uint64_t timestamp;
    QVariant payload;
}
```

或更类型安全地使用 `std::shared_ptr<const TrackDisplaySnapshot>`。

这更适合 `TrackDisplaySnapshot`、未来任务上下文、设备状态快照等业务级数据。

### 建议

短期可以先做最小闭环：

- 新增 `PAYLOAD_TYPE_TRACK_DISPLAY_SNAPSHOT`。
- 允许总线事件携带 `std::shared_ptr<const TrackDisplaySnapshot>` 或动态 payload。
- 不要把 snapshot 序列化进固定 4096 字节数组。

长期再统一整理 `DataPacket`，区分：

- 协议包：面向设备协议、小而固定。
- 业务事件：面向 core 内部模块、可承载对象。
- 对外 DTO：面向 Web/API/第三方。

## 问题 3
需要新增 Web 推送桥接层，而不是继续把逻辑塞在 `AppController`。

建议新增一个类：

```text
TrackDisplayWebBridge
```

职责：

- 订阅总线中的 `TrackDisplaySnapshot` 事件；
- 将 `TrackDisplaySnapshot` 转换成固定 Web DTO；
- 调用 `WebService::broadcast()` 推送；
- 不参与 TrackManager 的业务状态管理。

这样边界会更清楚：

```text
TrackDisplayManager = 航迹显示业务状态管理
SystemBus = 后端业务事件出口
TrackDisplayWebBridge = 总线事件到 WebSocket DTO 的适配器
React Web = DTO 消费者
```

## 问题 4
`AppController` 应只负责装配，不应承载 snapshot DTO 转换细节。

位置：
`core/AppController.cpp`

当前 `AppController` 里直接构造：

```cpp
QJsonObject msg;
QJsonArray tracks;
...
ws->broadcast(msg);
```

这段应该移动到 `TrackDisplayWebBridge` 或类似 adapter 中。  
`AppController` 只保留：

```cpp
m_trackDisplayWebBridge = std::make_unique<TrackDisplayWebBridge>(...);
m_trackDisplayWebBridge->start();
```

## 问题 5
`05-核心流程.drawio` 需要同步调整。

当前图里虽然标出了直连性能通道，但根据最终原则，需要改成：

```text
RadarDataProcessor
  -> TrackDisplayManager
  -> SystemBus
  -> WebApi/WebPushAdapter
  -> React Web
```

图上可以保留说明：

- `RadarDataProcessor -> TrackDisplayManager` 可以直连，因为这是 core 内部业务处理链；
- `TrackDisplayManager -> Web` 必须经过总线，因为这是跨到 Web/UI 的后端事件出口；
- 当前唯一高频外发到设备的数据流是 DPH；
- UI 获取航迹显示数据必须从总线事件经 Web adapter 获取。

## 实现顺序建议

1. 先扩展总线事件载荷能力，解决 `TrackDisplaySnapshot` 无法通过 `DataPacket` 表达的问题。
2. 让 `TrackDisplayManager` 在 `snapshotReady` 时发布总线事件。
3. 新增 `TrackDisplayWebBridge`，从总线订阅 snapshot 并推 WebSocket。
4. 删除 `AppController` 中 `TrackDisplayManager::snapshotReady -> ws->broadcast` 的直连 lambda。
5. 更新 `05-核心流程.drawio`，让图和代码链路一致。

## 架构底线

不要让 React Web 或 WebSocket 推送层直接依赖 `TrackDisplayManager`。  
Web 只能消费后端通过总线事件出口整理后的固定 DTO。

