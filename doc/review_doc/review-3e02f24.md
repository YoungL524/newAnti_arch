# Review 3e02f24

提交：`3e02f24 review-d428d31修改完成`

## 总体结论

这轮把上一轮的三个 P1 基本修掉了：

- `RadarDataProcessor -> TrackDisplayManager` 连接已经移到 `configureRadarProcessor()` 创建 processor 之后。
- 旧的 `WebApiController` 中 `GeneralPackage -> tracks.track` 单点推送已删除。
- 后端 snapshot 序列化已经包含嵌套 `style` / `interaction`。
- 前端 `useCoreApi.tsx` 已开始按 `mode === 'snapshot'` 走 replace，不再 append 单点。
- `AppController::stop()` 已调用 `TrackDisplayManager::stop()` / `clear()`。

Milestone 3 主链路现在已经基本闭合。  
当前没有看到阻断继续推进的后端架构问题，但前端协议类型还没同步，建议尽快修掉，避免后续继续靠 `any` 绕过固定协议。

## P2：前端 `protocol.ts` 仍是旧单点 tracks 协议，和后端 snapshot 不一致

位置：

- `ui/src/lib/protocol.ts:114-130`
- `ui/src/hooks/useCoreApi.tsx:216-230`

`protocol.ts` 里仍然定义：

```ts
export interface WsTracksMessage {
  type: 'tracks'
  deviceType: number
  track: WsTrackItem
}
```

但后端现在发的是：

```json
{
  "type": "tracks",
  "mode": "snapshot",
  "timestamp": 123,
  "tracks": [
    {
      "trackId": 1,
      "deviceId": "radar_001",
      "style": { ... },
      "interaction": { ... }
    }
  ]
}
```

前端消费处因此只能写：

```ts
(msg.tracks as any[]).map((t: any) => ...)
```

这会让“固定 Web 协议”在 TypeScript 侧失去约束。建议同步更新 `protocol.ts`：

```ts
export interface WsTrackDisplayPoint {
  trackId: number
  deviceId: string
  timestamp: number
  range: number
  azimuth: number
  elevation: number
  altitude: number
  x: number
  y: number
  z: number
  radialSpeed: number
  rcs: number
  pointIndex: number
  style: {
    color: string
    size: number
    opacity: number
  }
  interaction: {
    selected: boolean
    trackingEnabled: boolean
  }
}

export interface WsTracksMessage {
  type: 'tracks'
  mode: 'snapshot'
  timestamp: number
  tracks: WsTrackDisplayPoint[]
}
```

然后 `useCoreApi.tsx` 不再使用 `any`。

## P2：`trackRate` 现在不是“条/秒”，UI 文案或字段语义需要调整

位置：

- `ui/src/hooks/useCoreApi.tsx:230`
- `ui/src/components/layout/StatusBar.tsx:35`

前端现在：

```ts
trackCountRef.current = msg.tracks.length
```

但状态栏显示的是：

```tsx
航迹 {trackRate} 条/秒
```

snapshot 模式下，`msg.tracks.length` 是当前显示窗口内的点数，不是每秒新增点数。  
建议二选一：

1. 改 UI 文案为“显示点数 {trackRate}”或“当前航迹点 {trackRate}”。
2. 后端 snapshot 增加真实 rate 字段，前端继续显示“条/秒”。

当前阶段我建议先选第 1 种，别为了一个状态栏数字提前复杂化后端统计。

## P2：`initialize()` 里残留的 `setDeviceRegistry()` 分支可以删除

位置：`core/AppController.cpp:86-88`

现在仍有：

```cpp
if (m_deviceRegistry && m_radarProcessor)
    m_radarProcessor->setDeviceRegistry(m_deviceRegistry.get());
```

但 `m_radarProcessor` 是后面 `configureRadarProcessor()` 创建的，这个分支正常情况下不会执行。真正有效的设置已经在 `configureRadarProcessor()` 里。

建议删除这段残留代码，避免以后读代码的人误以为 processor 在 initialize 阶段已经存在。

## 下一步建议

先同步前端协议类型和状态栏语义，然后可以继续做：

1. interaction API：`PUT /api/devices/{deviceId}/tracks/{trackId}/interaction`。
2. 前端点击航迹后调用 interaction API。
3. DataTable 是否按 `(deviceId, trackId)` 聚合最后一个点，而不是展示所有点。

