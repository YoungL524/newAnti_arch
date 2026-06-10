# Review 948df3c

提交：`948df3c review-16754f4修改完成`

## 总体结论

这版 `doc/business/航迹显示数据管理.md` 已经比上一版扎实很多，几个关键方向都补上了：

- 明确 WS `tracks` 是全量 snapshot。
- 明确显示缓存保留策略：每条 track 500 点 + 最近 60s。
- 明确 `TrackPoint` 内部模型。
- 明确 interaction 属于 `TrackDisplayManager` 后端状态。
- 明确 `WebApiController` 后续不再直接引用 `GeneralPackage/TargetType`。

但在正式写代码前，还有两个主键/边界问题必须先定死，否则实现会漂。

## P1：`endpointId -> deviceId` 映射责任仍然摇摆，必须二选一

位置：`doc/business/航迹显示数据管理.md:10-19`、`doc/business/航迹显示数据管理.md:35-36`、`doc/business/航迹显示数据管理.md:84-86`

文档现在同时写了：

```text
TrackPoint(内部通用格式，含 deviceId)
TrackDisplayManager(显示状态管理 + 端点→业务 id 映射)
```

以及：

```text
endpointId → deviceId 映射由 RadarDataProcessor 或 TrackDisplayManager 通过 DeviceRegistry 查询完成
deviceId 在 RadarDataProcessor 或 TrackDisplayManager 入口通过 endpointId → DeviceRegistry 映射完成
TrackDisplayManager 通过 endpointId → DeviceRegistry 查询业务 deviceId（如已在 RadarDataProcessor 完成则忽略）
```

这会让后续代码出现两种实现都“符合文档”的情况：

- `RadarDataProcessor` 输出已经带业务 `deviceId` 的 `TrackPoint`；
- `TrackDisplayManager` 又保留 endpoint 映射逻辑，接收半成品数据。

建议必须二选一。  
我建议选：

```text
RadarDataProcessor 负责 endpointId -> DeviceRegistry.device.id 映射。
TrackPoint.deviceId 必须已经是业务设备 id。
TrackDisplayManager 不依赖 endpointId，不查询 DeviceRegistry。
```

原因：

- `TrackDisplayManager` 的职责是显示状态管理，不是通信端点解析。
- `TrackPoint` 既然叫内部通用格式，就不应该把 endpoint 概念继续传下去。
- 后续如果接入其他雷达厂家，协议适配层只要输出同一个 `TrackPoint`，显示层不用变。

建议文档改成明确链路：

```text
endpointId
  -> RadarDataProcessor 查询 DeviceRegistry / endpoint binding
  -> TrackPoint.deviceId = 业务设备 id
  -> TrackDisplayManager 只接收 TrackPoint，不关心 endpointId
```

同时删除“或 TrackDisplayManager 查询”“如已在 RadarDataProcessor 完成则忽略”这类弹性表述。

## P1：interaction API 只用 trackId 会在多设备同航迹号时冲突

位置：`doc/business/航迹显示数据管理.md:96-113`

当前 API 草案：

```http
PUT /api/tracks/{trackId}/interaction
```

问题是 `trackId` 只来自目标航迹编号，不能保证全系统唯一。多台雷达、历史回放、不同任务里都可能出现相同 `trackId`。  
但文档中的 `TrackDisplayPoint` 已经包含：

```ts
trackId: number
deviceId: string
```

因此后端 interaction 状态的 key 也应该至少是：

```text
(deviceId, trackId)
```

建议 API 改为其中一种：

```http
PUT /api/devices/{deviceId}/tracks/{trackId}/interaction
```

或者：

```http
PUT /api/tracks/interaction
{
  "deviceId": "radar_001",
  "trackId": 123,
  "selected": true,
  "trackingEnabled": false
}
```

我更建议第一种，路径能天然表达设备边界。  
同时 `TrackDisplayManager` 内部 interaction map 也应使用复合 key：

```cpp
struct TrackKey {
    QString deviceId;
    uint32_t trackId;
};
```

不要只用 `uint32_t trackId`。

## P2：`pointIndex` 要定义为绝对序号，不能受 FIFO 裁剪影响

位置：`doc/business/航迹显示数据管理.md:76`

文档里 `pointIndex` 是“该目标第 N 个点”，同时缓存策略会丢弃旧点。  
这里建议明确：

```text
pointIndex 是该 (deviceId, trackId) 自接收以来的绝对递增序号，不是当前缓存数组下标。
```

否则当 FIFO 丢弃旧点后，前端看到的第一个点如果又变成 `pointIndex=0/1`，前 3 个点样式会被重复触发。

## P2：全量 snapshot 建议加全局上限或输出预算

位置：`doc/business/航迹显示数据管理.md:40-47`、`doc/business/航迹显示数据管理.md:82-88`

当前策略是“每条 track 最多 500 点 + 最近 60s”，但如果同时有很多 track，全量 snapshot 仍可能非常大。

建议先加一个全局预算，例如：

```text
单次 snapshot 最多输出 5000 个点。
超过时按 timestamp 丢弃更旧点，优先保留最新显示窗口。
```

这不一定第一版就实现得很复杂，但文档最好先定一个上限，避免 WebSocket 消息在高目标数场景下无限膨胀。

## 建议的下一步

这版文档已经接近可实现，但建议 Claude Code 先补上上述两条 P1 后再开始代码：

1. 映射责任固定为 `RadarDataProcessor` 输出业务 `deviceId`，`TrackDisplayManager` 不碰 endpoint。
2. interaction 和内部 track 状态统一使用 `(deviceId, trackId)` 复合 key。

这两件事定住后，就可以开始最小实现：

- `TrackTypes.h`：定义 `TrackPoint` / `TrackDisplayPoint` / `TrackDisplaySnapshot` / `TrackKey`。
- `TrackDisplayManager`：接收 `TrackPoint`、维护缓存、输出 snapshot。
- `WebApiController`：从 manager 获取 snapshot 并推送，不再解析 `GeneralPackage`。
- 前端协议：从 `track` 单点增量改成 `tracks` 全量 snapshot。

