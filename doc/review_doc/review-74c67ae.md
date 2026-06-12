# Review 74c67ae

提交：`74c67ae review-2548afb修改完成`

## 总体结论

这轮把上一轮 P2 修掉了：

- interaction 解析/校验/执行逻辑已经抽成 `handleTrackInteraction`。
- `PUT /api/devices/{deviceId}/tracks/{trackId}/interaction` 的路径校验仍然保留。
- `selected` / `trackingEnabled` 仍然要求存在且为 bool。
- 普通 `/api/devices/{id}` 更新路径没有被 interaction handler 误拦截。

当前没有发现阻断问题。interaction 后端 API 可以继续进入前端调用联调。

## P2：`shared_ptr<std::function<...>>` 可以简化

位置：`core/web/WebApiController.cpp:104-139`

当前写法：

```cpp
auto handleTrackInteraction = std::make_shared<std::function<std::optional<QJsonObject>(...)>>(
    [this](...) -> std::optional<QJsonObject> { ... }
);
```

功能没问题，但这层 `shared_ptr<std::function<...>>` 有点重。这里的 lambda 只捕获 `this`，可以直接定义为普通 lambda 并按值捕获到两个 handler 里：

```cpp
auto handleTrackInteraction =
    [this](const QString& suffix, const QJsonObject& body) -> std::optional<QJsonObject> {
        ...
    };
```

然后：

```cpp
[this, validate, deviceFromBody, handleTrackInteraction](...) {
    if (auto r = handleTrackInteraction(suffix, body)) return *r;
    ...
}
```

这不是行为问题，只是基础路由代码更轻、更容易读。

## 下一步建议

可以接前端 interaction：

1. 在前端从 snapshot 点保留 `deviceId` / `trackId`。
2. 点击航迹或表格行时调用：

```http
PUT /api/devices/{deviceId}/tracks/{trackId}/interaction
```

3. 前端不要本地直接改样式，等待下一帧 snapshot 的 `interaction/style` 回推。

