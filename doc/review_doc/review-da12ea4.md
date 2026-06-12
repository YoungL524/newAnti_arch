# Review da12ea4

提交：`da12ea4 review-39bcae7修改完成`

## 总体结论

这轮补齐了固定 Web 协议中的 `x/y/z`：

- `core/AppController.cpp` 的 snapshot JSON 已输出 `x/y/z`。
- `ui/src/lib/protocol.ts` 的 `WsTrackDisplayPoint` 已定义 `x/y/z`。
- 业务文档、C++ DTO、WebSocket JSON、TypeScript 类型现在基本对齐。

当前没有发现阻断问题。  
Milestone 3 的“航迹显示 snapshot 主链路第一阶段”可以认为已经闭合：

```text
RadarDataProcessor
  -> TrackPoint
  -> TrackDisplayManager
  -> TrackDisplaySnapshot
  -> WebSocket tracks snapshot
  -> 前端 replace 渲染数据
```

## P2：`snapshotReady` 重复连接风险仍可后续处理

上一轮提到的生命周期小风险仍存在：如果未来 `startWebService()` 支持重复调用，`snapshotReady` 可能被重复 connect，导致 snapshot 重复广播。

当前应用路径看起来是启动一次 WebService，所以不阻断。  
后续如果做 Web 服务重启/端口切换，再补连接句柄或连接保护即可。

## 下一步建议

可以进入 interaction 闭环：

1. 后端实现：

```http
PUT /api/devices/{deviceId}/tracks/{trackId}/interaction
```

2. API 调用：

```cpp
TrackDisplayManager::setInteraction(deviceId, trackId, selected, trackingEnabled)
```

3. 前端点击航迹点/表格行后调用 API。
4. 下一个 snapshot 里的 `interaction` 和 `style` 驱动 UI 高亮。

这个阶段要继续坚持一个边界：interaction 是 `TrackDisplayManager` 的后端状态，不只存在前端。

