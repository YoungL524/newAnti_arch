# Review 4ce4b3c

提交：`4ce4b3c review-da12ea4修改完成`

## 总体结论

这轮开始实现 interaction API，方向是对的：

- `WebApiController` 注入了 `TrackDisplayManager*`。
- API 已能调用 `TrackDisplayManager::setInteraction(deviceId, trackId, selected, trackingEnabled)`。
- interaction 状态仍然落在后端 `TrackDisplayManager`，没有退回成纯前端状态。

但当前 API 形态和前面定下的业务契约不一致，并且字段校验太宽，会产生误清状态风险。建议先修，再接前端点击。

## P1：interaction API 路径/方法和业务文档不一致

位置：

- `core/web/WebApiController.cpp:278-294`
- `doc/business/航迹显示数据管理.md:126-149`

业务文档定的是：

```http
PUT /api/devices/{deviceId}/tracks/{trackId}/interaction
```

当前实现是：

```http
POST /api/tracks/interaction
```

并把 `deviceId` / `trackId` 放在 body 里。

功能上可以工作，但这会让我们刚刚定住的 `(deviceId, trackId)` 复合 key 又从 URL 资源层退回成普通 body 参数，和文档、前面 review 的契约不一致。

建议改成文档里的路径：

```cpp
m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), ...);
```

解析：

```text
/api/devices/{deviceId}/tracks/{trackId}/interaction
```

这样资源边界更清晰：interaction 是某个业务设备下某条航迹的显示状态。

## P1：`selected` / `trackingEnabled` 缺失时会默认为 false，可能误清状态

位置：`core/web/WebApiController.cpp:287-288`

当前实现：

```cpp
bool selected = body.value("selected").toBool();
bool trackingEnabled = body.value("trackingEnabled").toBool();
```

如果前端只想改 `selected`，漏传了 `trackingEnabled`，后端会把 `trackingEnabled` 强制变成 false。  
这和文档里“`selected` 与 `trackingEnabled` 独立存储”容易冲突。

建议第一版先采用全量替换，但必须显式要求两个字段都存在且是 bool：

```text
selected 缺失或不是 bool -> 400
trackingEnabled 缺失或不是 bool -> 400
```

如果后续想支持局部更新，再把 API 语义明确为 PATCH，而不是当前这种隐式 false。

## P2：WebApiController 注释仍写“SystemBus 航迹”

位置：`core/web/WebApiController.h:45-48`

旧航迹推送已经从 `WebApiController` 删除了，但注释仍写：

```cpp
/// 连接 WS 推送（SystemBus 航迹、Logger 日志、1s 状态定时器）
```

建议改成：

```cpp
/// 连接 WS 推送（Logger 日志、1s 状态定时器）
```

航迹 snapshot 现在由 `AppController` 连接 `TrackDisplayManager::snapshotReady` 推送，不再是 WebApiController 的 SystemBus 航迹订阅。

## 下一步建议

先修 interaction API 契约：

1. 改为 `PUT /api/devices/{deviceId}/tracks/{trackId}/interaction`。
2. 显式校验 `selected` / `trackingEnabled`。
3. 返回 `{ ok:true, interaction:{...} }`。

然后再接前端点击航迹/表格行调用该 API。

