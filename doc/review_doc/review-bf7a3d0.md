# Review bf7a3d0

## 结论

这一版把“单控制器 MVP”的文档语义补清楚了，方向是对的：`doc/business/雷达工作状态控制.md` 明确写出当前只有一个 `RadarWorkModeController`，`deviceIds` 是为多控制器预留的 API 契约，`mode: "single-controller"` / `controllerId: "radar_cpu"` 用于说明当前执行模式。

但代码里只把 `/api/reset` 的响应改成了单控制器语义；`/api/start` 和 `/api/stop` 仍然返回所有映射到 `radar_cpu` 的设备为 accepted。这和文档第 110 行附近的描述不一致。

## P1 必改

### 1. /api/start 和 /api/stop 的 acceptedDeviceIds 仍然不符合单控制器契约

文档现在写得很明确：

> 单控制器阶段，`acceptedDeviceIds` 仅包含实际执行的那一个 id；若请求含多个映射到 `radar_cpu` 的设备，其余进入 `rejectedDeviceIds`。响应含 `mode: "single-controller"` 与 `controllerId: "radar_cpu"`。

`/api/reset` 已经按这个语义实现：

- 只选择第一个映射到 `radar_cpu` 的 id 作为 `executedId`
- `acceptedDeviceIds` 只包含实际执行的一个 id
- 其余 mapped id 进入 `rejectedDeviceIds`
- 响应包含 `mode: "single-controller"` 和 `controllerId: "radar_cpu"`

但 `/api/start` / `/api/stop` 的 `handleCommand()` 仍然是：

```cpp
QJsonArray accepted;
if (ids.isEmpty()) {
    accepted.append("radar_cpu");
} else {
    for (const auto& v : ids)
        if (mappedIds.contains(v.toString()))
            accepted.append(v.toString());
}

...
for (const auto& v : accepted) {
    if (mappedIds.contains(v.toString())) {
        dispatched = true;
        cmdAccepted = work ? m_wmc->requestWork() : m_wmc->requestStandby();
        break;
    }
}

return { ok, acceptedDeviceIds: accepted, rejectedDeviceIds: rejected };
```

也就是说如果前端传入两个都映射到 `radar_cpu` 的设备，后端实际只执行一次，但 response 会说两个都 accepted。

建议 Cursor 修改：

1. 抽一个共用 helper，例如 `resolveSingleControllerDeviceIds(body)`：
   - 输入 `deviceIds`
   - 输出 `executedId`
   - 输出 `acceptedDeviceIds`，仅包含实际执行 id
   - 输出 `rejectedDeviceIds`，包含不支持 id 和多余 mapped id
2. `/api/start`、`/api/stop`、`/api/reset` 都使用同一套解析逻辑。
3. `/api/start`、`/api/stop` 响应也加：

```json
{
  "mode": "single-controller",
  "controllerId": "radar_cpu"
}
```

这样文档、start/stop/reset、前端理解才能统一。

## P2 建议

### 2. 前端 CommandResponse 类型未声明 mode/controllerId

`ui/src/hooks/useCoreApi.tsx` 的 `CommandResponse` 目前只有：

```ts
acceptedDeviceIds?: string[]
rejectedDeviceIds?: string[]
```

如果后端统一返回 `mode` / `controllerId`，建议前端类型同步增加：

```ts
mode?: 'single-controller' | 'multi-controller'
controllerId?: string
```

当前前端不使用这两个字段，所以不是功能 blocker。但类型补上以后，后续 UI 想提示“当前为单控制器模式”会更自然。

### 3. 文档示例仍容易让人误解为已支持多设备独立执行

文档前面已经写了“当前单控制器 MVP”，很好。但用户操作流程里仍然写：

```text
用户勾选 2 台 → 点击 [开始采集]
已勾选的 2 台进入工作模式
未勾选的 NODE_GAMMA_03 保持待机
```

这在目标架构里成立，但当前 MVP 里不完全成立：如果两台都映射到 `radar_cpu`，它们只是共享同一控制器状态；如果只有一台映射到 `radar_cpu`，另一台会被 rejected。

建议把这个示例标注为“目标架构示例”，或者补一段“当前 MVP 下，仅实际映射到 radar_cpu 的首个设备触发控制器命令”。

## 已修复确认

- 文档已明确当前只有单 `RadarWorkModeController`。
- 文档已说明 `deviceIds` 是多控制器扩展契约，不代表当前逐设备独立执行。
- `/api/reset` 已返回 `mode: "single-controller"`、`controllerId: "radar_cpu"`。
- 前端 reset 成功后会主动 `refreshStatus()`，减少纯乐观状态停留。

## 下一步建议

1. 先统一 `/api/start`、`/api/stop`、`/api/reset` 的单控制器响应契约。
2. 然后这块可以先停，不急着做多控制器。
3. 继续推进 Milestone 4：任务上下文绑定航迹、日志、历史数据。

## 给 Cursor 的最小修改清单

1. 抽一个 start/stop/reset 共用的单控制器 deviceIds 解析 helper。
2. `/api/start` 和 `/api/stop` 只 accepted 实际执行的一个 id，其余进入 rejected。
3. `/api/start` 和 `/api/stop` 响应补 `mode: "single-controller"`、`controllerId: "radar_cpu"`。
4. 前端 `CommandResponse` 类型补 `mode`、`controllerId`。
5. 文档用户操作流程标注“目标架构示例”或补当前 MVP 行为说明。

