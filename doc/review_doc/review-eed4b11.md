# Review eed4b11

## 结论

这一版把 `review-bf7a3d0` 的核心问题修掉了：`/api/start`、`/api/stop`、`/api/reset` 已统一使用单控制器 deviceIds 解析逻辑，响应也统一包含 `mode: "single-controller"`、`controllerId: "radar_cpu"`，`acceptedDeviceIds` 只包含实际执行的那个 id。文档和前端类型也同步了。

当前没有阻塞性问题。这一小段可以收口，后续不建议继续在单控制器细节上打转，可以转向任务上下文绑定航迹、日志、历史数据。

## P2 建议

### 1. 节流时间戳应在命令被接受后再更新

现在 `/api/start`、`/api/stop`、`/api/reset` 都是在状态机真正接受命令之前更新节流时间：

```cpp
if (now - lastMs < 500)
    return { ok:false, throttled:true };
lastMs = now;

...
const bool cmdAccepted = ...
if (!cmdAccepted)
    return { ok:false, error: ... };
```

这会导致一次非法请求也占用 500ms 节流窗口。例如：

- 当前不是 Fault，用户误点 reset，后端拒绝；
- 随后很快状态进入 Fault，用户再次点 reset；
- 第二次可能先被 throttle 拦住。

建议：

- `lastMs = now` 放到 `cmdAccepted == true` 之后。
- 或者区分“请求节流”和“命令执行节流”，但当前系统没必要复杂化。

这不是大问题，只是交互细节会更顺。

### 2. resolveSingleControllerDeviceIds 的输入可以只解析一次

当前 `handleCommand()` 里：

```cpp
const auto resolution = resolveSingleControllerDeviceIds(
    body["deviceIds"].toArray());
if (!body["deviceIds"].toArray().isEmpty() && !resolution.hasExecutableTarget)
```

`body["deviceIds"].toArray()` 被调用两次。可以先保存：

```cpp
const auto ids = body[QStringLiteral("deviceIds")].toArray();
const auto resolution = resolveSingleControllerDeviceIds(ids);
if (!ids.isEmpty() && !resolution.hasExecutableTarget) ...
```

reset 分支也同理。只是可读性优化。

## 已修复确认

- start/stop/reset 统一使用 `resolveSingleControllerDeviceIds()`。
- `buildSingleControllerCommandResponse()` 统一返回 `mode/controllerId/acceptedDeviceIds/rejectedDeviceIds`。
- 前端 `CommandResponse` 已补 `mode`、`controllerId`。
- 文档已补“目标架构示例”和“当前 MVP”说明，单控制器语义基本清楚。

## 下一步建议

1. 可选修一下节流时间戳位置。
2. 工作模式/设备控制这块先暂停继续细抠。
3. 开始推进 Milestone 4：任务上下文绑定航迹、日志、历史数据。

