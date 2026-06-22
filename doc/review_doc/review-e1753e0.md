# Review e1753e0

## 结论

这一版把 `review-eed4b11` 的两个 P2 都收掉了：

- 节流时间戳已经移动到命令被状态机接受之后再更新。
- `deviceIds` 也先保存为局部变量，再传入 `resolveSingleControllerDeviceIds()`，可读性更好。

当前没有发现需要继续修的细节问题。设备控制 / 单控制器 MVP 这条线可以先收口。

## 已修复确认

- `/api/start`、`/api/stop`、`/api/reset` 的单控制器响应契约已统一。
- `acceptedDeviceIds` 只包含实际执行的那个 id。
- `rejectedDeviceIds` 包含未映射 id 和多余 mapped id。
- 响应包含 `mode: "single-controller"` 与 `controllerId: "radar_cpu"`。
- 非法请求不会再占用节流窗口。

## 下一步建议

不要继续在单控制器细节上打补丁。后续如果要支持真正多雷达独立控制，应作为单独阶段设计 `WorkModeControllerRegistry`。

当前更建议继续推进 Milestone 4：任务上下文绑定航迹、日志、历史数据。

