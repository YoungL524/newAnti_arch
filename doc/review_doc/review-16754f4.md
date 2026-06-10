# Review 16754f4

提交：`16754f4 review-96d4dac修改完成`

## 总体结论

这轮新增了 `doc/business/航迹显示数据管理.md`，正式开始 Milestone 3。  
整体方向是对的，尤其是这几个边界判断非常重要：

- 上位机不负责航迹起批、消失、融合。
- `RadarDataProcessor` 继续做协议适配。
- Web 只接收固定显示协议，不直接吃厂家协议结构。
- UI 订阅的是“当前应该显示的航迹状态”，不是原始 `GeneralPackage/TargetType`。

不过这份文档现在还停在概念层。进入代码前建议先补 3 个关键契约，否则实现很容易漂回 `WebApiController` 直接解析协议包的旧模式。

## P1：`deviceId` 来源必须明确，否则 Web 固定协议无法和设备台账对齐

位置：`doc/business/航迹显示数据管理.md:26-27`

文档里的 Web 协议包含：

```ts
trackId: number
deviceId: string
```

但当前代码链路里，`RadarDataProcessor` 只有：

- `endpointId`
- `sourceType`
- `GeneralPackage`
- `TargetType`

现有 `WebApiController` 推送 tracks 时也只发：

```cpp
msg["deviceType"] = static_cast<int>(pkt->sourceType);
```

这和 Milestone 1 已经定下来的设备台账契约不一致。前端真正需要对齐的是后端 `DeviceRegistry` 里的业务设备 id，例如 `radar_001`，而不是 endpoint id 或 device type。

建议 Claude Code 在文档里明确：

```text
TrackPoint.deviceId 必须是 DeviceRegistry 业务设备 id。
endpointId 只是通信端点 id，不能直接暴露给 Web 作为业务设备 id。
sourceType/deviceType 只是设备类型，不能替代 deviceId。
```

同时需要定义映射入口：

```text
endpointId -> DeviceRegistry device.id
```

可选方案：

1. `RadarDataProcessor` 在生成 `TrackPoint` 前通过只读映射拿到业务 `deviceId`。
2. `TrackDisplayManager` 接收 `endpointId`，内部通过 `DeviceRegistry` 映射成业务 `deviceId`。

我更建议第 1 种：`RadarDataProcessor` 输出的 `TrackPoint` 就已经是内部标准业务数据，不再携带通信端点概念给后续显示层。

## P1：需要定死 WS `tracks` 消息形态：全量 snapshot 还是单点增量

位置：

- `doc/business/航迹显示数据管理.md:23-55`
- `doc/business/航迹显示数据管理.md:61-63`

文档命名为 `TrackDisplaySnapshot`，但 TypeScript 示例实际是单个 `TrackDisplayPoint`。同时文档写了“1s 节流输出”，这更像全量 snapshot；而现有代码是每个 `GeneralPackage` 推一条单点增量：

```ts
interface WsTracksMessage {
  type: 'tracks'
  deviceType: number
  track: WsTrackItem
}
```

这会影响前后端实现方式：

- 如果是单点增量：前端继续 append，TrackDisplayManager 主要负责补 style/interaction。
- 如果是全量 snapshot：前端应替换当前 tracks 视图，不再简单 append。
- 如果是按 trackId 分组快照：前端数据结构要从数组追加改为 map/series。

建议先把协议定死，例如：

```ts
interface WsTracksMessage {
  type: 'tracks'
  mode: 'snapshot'
  timestamp: number
  tracks: TrackDisplayPoint[]
}
```

或者：

```ts
interface WsTrackPointMessage {
  type: 'track-point'
  point: TrackDisplayPoint
}
```

二者选一个，不要混用。  
如果文档继续叫 `TrackDisplaySnapshot`，我建议采用全量 snapshot：每 1s 推当前显示窗口内所有点，前端按 `tracks` 替换渲染数据。

## P1：显示缓存保留策略必须定义，避免误伤“上位机不负责航迹消失”的边界

位置：`doc/business/航迹显示数据管理.md:57-81`

文档说 `TrackDisplayManager` 按 `trackId` 维护点序列和 `pointIndex`，但没有定义缓存什么时候清理。  
这里要小心：上位机不负责“航迹消失管理”，但显示层仍然必须有自己的**显示缓存保留策略**，否则：

- 内存会随着点数无限增长；
- 全量 snapshot 会越来越大；
- 前端可能永远显示历史点；
- `pointIndex` 和样式计算会越来越难控。

建议文档明确区分：

```text
不做航迹消失判断：不决定某条航迹在数据处理意义上是否终止。
做显示缓存保留：只决定前端当前画面保留多少历史点。
```

建议先定一个简单策略：

- 每条 track 最多保留 N 个点，例如 500。
- 或按时间窗口保留，例如最近 60s。
- 或二者同时限制：最近 60s 且最多 500 点。

这个策略属于显示数据管理，不属于数据处理的起批/消失算法，可以放心放在 `TrackDisplayManager`。

## P2：`TrackPoint` 内部模型需要单独定义，不要只定义 Web DTO

位置：`doc/business/航迹显示数据管理.md:10-18`

文档提到：

```text
GeneralPackage → RadarDataProcessor → TrackPoint → TrackDisplayManager
```

但目前只定义了 Web 侧 `TrackDisplayPoint`，没有定义 core 内部 `TrackPoint`。  
建议补一段 C++ 结构草案，至少包括：

```cpp
struct TrackPoint {
    QString deviceId;
    uint32_t trackId;
    qint64 timestampMs;
    double range;
    double azimuth;
    double elevation;
    double radialSpeed;
    double rcs;
    double altitude;
    double x;
    double y;
    double z;
    uint32_t sceneNumber;
};
```

这样可以明确：

- `RadarDataProcessor` 输出的是内部通用格式；
- `TrackDisplayManager` 输入不是厂家协议；
- `WebApiController` 不再接触 `GeneralPackage/TargetType`。

## P2：交互状态命令入口需要补一笔

位置：`doc/business/航迹显示数据管理.md:42-48`

文档定义了：

```ts
interaction: {
  selected: boolean
  trackingEnabled: boolean
}
```

但没有说明用户点击/追踪使能如何进入后端。建议补充最小 API 或事件：

```text
PUT /api/tracks/{trackId}/interaction
{
  "selected": true,
  "trackingEnabled": false
}
```

或者通过 WebSocket command 进入后端。  
重点是：interaction 是 `TrackDisplayManager` 的状态，不应该只存在前端，否则后端输出的 snapshot 就无法真正表达“当前状态下每条航迹应该显示的样子”。

## 建议的下一步

先不要直接开写 `TrackDisplayManager.cpp`。建议 Claude Code 下一轮先把 `doc/business/航迹显示数据管理.md` 补到可实现级别：

1. 定义 `TrackPoint` 内部模型。
2. 定义 `endpointId -> deviceId` 映射责任。
3. 定义 WS 消息是 snapshot 还是 point event。
4. 定义显示缓存保留策略。
5. 定义 interaction 更新入口。

这些定住后，再开始写代码会稳很多。

