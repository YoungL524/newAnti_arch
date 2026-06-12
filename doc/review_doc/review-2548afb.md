# Review 2548afb

提交：`2548afb review-4ce4b3c修改完成`

## 总体结论

这轮把上一轮两个 P1 修掉了：

- interaction API 已改为文档约定的资源路径：

```http
PUT /api/devices/{deviceId}/tracks/{trackId}/interaction
```

- `selected` / `trackingEnabled` 已显式校验必须存在且为 bool，不再因为缺字段默认为 false。
- 旧的 `POST /api/tracks/interaction` 已删除。
- `WebApiController` 注释也移除了旧的 SystemBus 航迹推送描述。

当前没有发现阻断问题。  
interaction 后端 API 可以进入前端联调阶段。

## P2：interaction handler 逻辑重复，建议抽成私有 helper

位置：`core/web/WebApiController.cpp:185-214`、`core/web/WebApiController.cpp:242-273`

当前有两份几乎完全相同的 interaction 解析和执行逻辑：

1. `m_deviceReg` 存在时，挂在设备 `PUT /api/devices/` prefix 里；
2. `!m_deviceReg && m_trackMgr` 时，单独注册同一个 prefix。

功能上没问题，但后续如果改字段、错误文案、返回结构，很容易只改一处。建议抽成一个私有方法或局部 lambda，例如：

```cpp
auto handleTrackInteraction = [this](const QString& suffix, const QJsonObject& body) -> std::optional<QJsonObject> {
    // 如果 suffix 不是 tracks interaction 路径，返回 nullopt
    // 如果是该路径，则完成校验、setInteraction、返回 response
};
```

然后设备 update prefix 里先尝试：

```cpp
if (auto r = handleTrackInteraction(suffix, body)) return *r;
```

这样既不影响 `/api/devices/{id}` 的 update，又避免 interaction 逻辑复制。

## P2：建议前端下一步直接接 API，不要本地维护 selected 状态

下一轮如果做前端点击航迹/表格行：

- 前端点击只发 `PUT /api/devices/{deviceId}/tracks/{trackId}/interaction`。
- 不要在前端本地直接改 selected/tracking 样式。
- 由后端 `TrackDisplayManager` 更新 interaction，再通过下一帧 snapshot 回推样式。

这样才能保持“interaction 是后端显示状态”的架构边界。

