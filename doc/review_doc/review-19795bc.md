# Review 19795bc

提交：`19795bc review-74c67ae修改完成`

## 总体结论

这轮只是把上一轮的 P2 做了代码整理：

- `handleTrackInteraction` 已从 `shared_ptr<std::function<...>>` 简化为普通 lambda。
- 路径校验、`selected` / `trackingEnabled` bool 校验、`setInteraction()` 调用都保持不变。
- 普通设备 update 路径仍然不会被误拦截。

当前没有发现阻断问题。  
后端 interaction API 这块可以认为已经收口。

## 下一步建议

下一轮可以正式接前端 interaction：

1. snapshot 点数据需要保留 `deviceId` / `trackId` 到可点击对象。
2. 点击航迹点或表格行时调用：

```http
PUT /api/devices/{deviceId}/tracks/{trackId}/interaction
```

3. body 全量传：

```json
{
  "selected": true,
  "trackingEnabled": false
}
```

4. 前端不要直接改本地样式，等待下一帧 snapshot 里的 `interaction/style` 回推。

