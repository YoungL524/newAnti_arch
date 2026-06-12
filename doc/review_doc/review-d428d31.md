# Review d428d31

提交：`d428d31 review-2cbe0e6修改完成`

## 总体结论

这轮开始接 Milestone 3 主链路，方向是对的：

- `RadarDataProcessor` 增加 `trackPointReady(TrackPoint)`。
- `RadarDataProcessor` 通过 `DeviceRegistry::findRadarByEndpointId()` 生成业务 `deviceId`。
- `AppController` 增加 `TrackDisplayManager`。
- `TrackDisplayManager::snapshotReady` 开始序列化成 WebSocket `tracks` snapshot。

但当前链路还没有真正闭合，有几个 P1 必须先修，否则前端会同时收到旧协议和新协议，甚至 `TrackPoint` 根本进不了 `TrackDisplayManager`。

## P1：`RadarDataProcessor -> TrackDisplayManager` 连接位置错误，当前很可能没有连接上

位置：`core/AppController.cpp:83-92`、`core/AppController.cpp:380-390`

当前连接写在 `AppController::initialize()`：

```cpp
if (m_radarProcessor && m_deviceRegistry) {
    m_radarProcessor->setDeviceRegistry(m_deviceRegistry.get());
    connect(m_radarProcessor.get(), &RadarDataProcessor::trackPointReady,
            m_trackDisplayManager.get(), &TrackDisplayManager::addPoint);
}
```

但 `m_radarProcessor` 是在后面的 `configureRadarProcessor()` 里创建的：

```cpp
m_radarProcessor = std::make_unique<RadarDataProcessor>(
    m_commManager.get(), m_dataStore.get(), this);
```

所以 `initialize()` 执行到这里时，`m_radarProcessor` 大概率还是空，导致：

- `setDeviceRegistry()` 没调用；
- `trackPointReady -> addPoint` 没连接；
- `RadarDataProcessor` 即使收到 `GeneralPackage`，也不会输出到 `TrackDisplayManager`。

建议把这段移动到 `configureRadarProcessor()` 创建 `m_radarProcessor` 之后：

```cpp
if (m_deviceRegistry) {
    m_radarProcessor->setDeviceRegistry(m_deviceRegistry.get());
}
if (m_trackDisplayManager) {
    connect(m_radarProcessor.get(), &RadarDataProcessor::trackPointReady,
            m_trackDisplayManager.get(), &TrackDisplayManager::addPoint);
}
```

## P1：旧的 WebApiController 单点 `tracks` 推送仍然存在，会和 snapshot 协议冲突

位置：`core/web/WebApiController.cpp:290-318`、`core/AppController.cpp:663-694`

现在有两条 WS tracks 推送：

旧路径：

```cpp
WebApiController:
SystemBus GeneralPackage -> msg["type"]="tracks"; msg["track"] = ...
```

新路径：

```cpp
AppController:
TrackDisplayManager snapshotReady -> msg["type"]="tracks"; msg["mode"]="snapshot"; msg["tracks"] = [...]
```

这会让前端同时收到两种同名消息：

```json
{ "type": "tracks", "track": {...} }
```

和：

```json
{ "type": "tracks", "mode": "snapshot", "tracks": [...] }
```

这正好违背了 Milestone 3 的目标：Web 不再直接吃 `GeneralPackage/TargetType`，只接收固定 snapshot 协议。

建议 Claude Code 删除或禁用 `WebApiController::connectWebPush()` 里的 `PAYLOAD_TYPE_GENERAL_PACKAGE` 旧订阅逻辑。  
保留日志和 status 推送即可，航迹只由 `TrackDisplayManager` 输出。

## P1：前端仍按旧单点增量协议消费 `tracks`

位置：`ui/src/hooks/useCoreApi.tsx:216-228`

当前前端仍然写的是：

```ts
const t = msg.track
if (t) {
  tracksRef.current = [...tracksRef.current, row].slice(-500)
  trackCountRef.current++
}
```

这不会消费新的 snapshot：

```ts
msg.mode === 'snapshot'
msg.tracks: TrackDisplayPoint[]
```

建议这轮或下一轮同步改前端：

```text
if msg.type === 'tracks' && msg.mode === 'snapshot':
  tracksRef.current = msg.tracks 映射后的显示数据
  trackRate = msg.tracks.length 或后端单独统计字段
```

注意这里是 replace，不是 append。

## P2：snapshot JSON 没按文档输出嵌套 `style` / `interaction`

位置：`core/AppController.cpp:673-688`

文档里的固定协议是：

```ts
style: {
  color: string
  size: number
  opacity: number
}
interaction: {
  selected: boolean
  trackingEnabled: boolean
}
```

当前实现展平成：

```cpp
t["color"] = dp.style.color;
t["size"] = dp.style.size;
t["opacity"] = dp.style.opacity;
```

而且没有输出 `interaction`。

建议严格按文档序列化：

```cpp
QJsonObject style;
style["color"] = dp.style.color;
style["size"] = dp.style.size;
style["opacity"] = dp.style.opacity;
t["style"] = style;

QJsonObject interaction;
interaction["selected"] = dp.interaction.selected;
interaction["trackingEnabled"] = dp.interaction.trackingEnabled;
t["interaction"] = interaction;
```

固定协议一旦开始落地，就尽量不要在 C++ 和 TS 之间漂。

## P2：TrackDisplayManager 生命周期需要接入 stop/clear

位置：`core/AppController.cpp:227-257`、`core/AppController.cpp:259` 之后的 stop 流程

当前 `startWebService()` 会：

```cpp
m_trackDisplayManager->start();
```

但 `AppController::stop()` 里还没看到对应的 `stop()` / `clear()`。  
建议至少在停止服务或停止采集时：

```cpp
if (m_trackDisplayManager) {
    m_trackDisplayManager->stop();
    m_trackDisplayManager->clear();
}
```

具体是 stop 还是任务切换时 clear，可以后续定；但不要让 timer 在应用停止/重启 Web 服务时重复连接或持续推送旧状态。

## 下一步建议

先修这轮 P1：

1. 把 `RadarDataProcessor -> TrackDisplayManager` 连接移动到 processor 创建之后。
2. 删除旧 `WebApiController` 的 GeneralPackage -> `tracks.track` 推送。
3. 前端改为消费 snapshot replace。

这三件事完成后，Milestone 3 主链路才算真正闭合。

