# Review 39bcae7

提交：`39bcae7 review-3e02f24修改完成`

## 总体结论

这轮把上一轮的前端协议收口问题基本修掉了：

- `protocol.ts` 已从旧单点 `track` 协议改为 `WsTrackDisplayPoint` + `WsTracksMessage { mode:'snapshot', tracks: [] }`。
- `useCoreApi.tsx` 不再用 `any`，开始使用 `WsTrackDisplayPoint[]`。
- 状态栏文案从“航迹 xx 条/秒”改成“显示点数 xx”，符合 snapshot 语义。
- `AppController::initialize()` 里无效的 `setDeviceRegistry()` 残留分支已删除。

Milestone 3 的主链路和前端 snapshot 消费已经基本通顺。  
现在剩下的是固定协议字段一致性和生命周期小风险。

## P1：固定 Web 协议缺少 `x/y/z`，C++ / 文档 / TS 没完全对齐

位置：

- `doc/business/航迹显示数据管理.md:70-74`
- `core/business/TrackTypes.h`
- `core/AppController.cpp:679-698`
- `ui/src/lib/protocol.ts:114-132`

业务文档里的 `TrackDisplayPoint` 包含：

```ts
x: number
y: number
z: number
```

C++ 的 `TrackDisplayPoint` 也有：

```cpp
double x = 0, y = 0, z = 0;
```

但当前 WebSocket 序列化没有输出 `x/y/z`：

```cpp
t["range"] = dp.range;
t["azimuth"] = dp.azimuth;
t["elevation"] = dp.elevation;
t["radialSpeed"] = dp.radialSpeed;
...
```

`ui/src/lib/protocol.ts` 的 `WsTrackDisplayPoint` 也没有定义 `x/y/z`。

这会让“固定 Web 协议”继续漂移：core 内部有字段、业务文档有字段，但 Web 实际协议没有。  
建议 Claude Code 补齐：

```cpp
t[QStringLiteral("x")] = dp.x;
t[QStringLiteral("y")] = dp.y;
t[QStringLiteral("z")] = dp.z;
```

并同步 TS：

```ts
x: number
y: number
z: number
```

即使当前 UI 暂时不用 `x/y/z`，固定协议也应该完整输出，避免后续地图层/三维层再绕回原始协议字段。

## P2：`startWebService()` 重复调用时可能重复连接 `snapshotReady`

位置：`core/AppController.cpp:663-704`

当前每次 `startWebService()` 都会：

```cpp
QObject::connect(m_trackDisplayManager.get(), &TrackDisplayManager::snapshotReady,
                 ws, [ws](const TrackDisplaySnapshot& snap) { ... });
m_trackDisplayManager->start();
```

如果未来支持 Web 服务重启、端口切换，或 `startWebService()` 被重复调用，同一个 `snapshotReady` 可能连接多次，导致一份 snapshot 被广播多次。

当前应用可能只启动一次，所以不是阻断问题。建议后续接生命周期时加一个连接 id 或拆出独立方法，确保只连接一次。

## 下一步建议

先补齐 `x/y/z` 协议字段。  
然后可以继续推进 interaction：

1. 后端增加 `PUT /api/devices/{deviceId}/tracks/{trackId}/interaction`。
2. API 调 `TrackDisplayManager::setInteraction()`。
3. 前端点击航迹/表格行时调用 interaction API。
4. snapshot 返回的 `interaction` 驱动样式和 UI 选中状态。

