# Review for 8d02505

Full commit: `8d0250554e09821602aedc0869affe518e4cae9e`

Commit message: `review-66f0fb0修改完成`

## Findings

### P2 - 部分设备被拒绝时前端整体回滚，但后端可能已经执行了 accepted 设备

- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 106-123
- File: `core/web/WebApiController.cpp`
- Lines: 78-118

这版已经补上 `commandError` 和 tracks 回滚，上一轮的编译问题也处理了，方向是对的。

但当前前端逻辑是：只要 `rejectedDeviceIds.length > 0`，就调用 `rollback()`：

```ts
if (data.rejectedDeviceIds?.length > 0) {
  rollback('部分设备被拒绝: ' + JSON.stringify(data.rejectedDeviceIds))
}
```

后端语义却是：只要 `accepted` 非空，就先对 accepted 设备执行命令，然后返回 `ok:true + acceptedDeviceIds + rejectedDeviceIds`。也就是说一个请求如果包含：

```json
{
  "deviceIds": ["radar_cpu", "unknown_device"]
}
```

后端会实际执行 `radar_cpu` 的启停，但前端看到 `rejectedDeviceIds` 后整体回滚成旧状态。短时间内 UI 会显示“没启动/没停止”，而后端真实状态已经变化，只能等下一次 status/WS 再纠正。

建议 Claude Code 修改：

1. 明确契约：`ok:true + acceptedDeviceIds 非空 + rejectedDeviceIds 非空` 表示“部分成功”，不能按整体失败处理。
2. 前端不要 rollback accepted 部分；应该保留 command 状态，并显示 warning：`部分设备未执行`。
3. 只有 `ok:false` 或 `acceptedDeviceIds` 为空时才整体 rollback。
4. 后端也可以增加 `partial: true` 字段，让前端分支更清晰。

### P2 - `DeviceRegistry -> endpoint/controller` 映射仍停留在注释层，设备台账还没有真正驱动控制

- File: `core/web/WebApiController.cpp`
- Lines: 64-73
- File: `core/business/DeviceRegistry.h`
- Line: 34

`resolveControllable()` 目前仍然只接受 id 为 `radar_cpu` 的设备：

```cpp
if (id == QStringLiteral("radar_cpu"))
    accepted.append(id);
// Future: lookup DeviceRegistry → endpointId mapping
```

这意味着 Milestone 1 的核心链路还没真正完成。前端现在确实发送了 `checkedDeviceIds`，但只有用户刚好勾选 `radar_cpu` 才能控制成功。普通台账雷达即使有 `endpointId`，也不会被映射到当前 `RadarWorkModeController`。

建议 Claude Code 下一轮不要继续只修 UI 细节，应该把映射规则先定死一个最小版本：

1. 短期规则可以是：`DeviceInfo.id == "radar_cpu"` 或 `DeviceInfo.endpointId == "radar_cpu"` 映射到当前 `RadarWorkModeController`。
2. `acceptedDeviceIds` 返回用户选择的业务设备 id，不要返回内部 controller id，否则前端无法和台账设备对应。
3. `buildDeviceStatusArray()` 也用同一套映射规则 merge runtime state，避免控制和状态两边判断不一致。
4. 把这个规则写进架构/业务文档，后续多 controller 再扩展。

### P2 - `commandError` 和 `noSelectionHint` 可以同时显示并重叠

- File: `ui/src/components/layout/TitleBar.tsx`
- Lines: 76-96
- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 78-85

`TitleBar` 现在分别渲染 `noSelectionHint` 和 `commandError`，两者都是：

```ts
absolute top-full right-4 mt-1
```

如果先发生一个 commandError，再在未清除的情况下点击“开始采集”且没有勾选设备，`noSelectionHint` 会被置为 true，但 `commandError` 不会被清除；两个提示会出现在完全相同位置，互相覆盖。

建议 Claude Code 修改：

1. 在触发 `noSelectionHint` 时同步清空 `commandError`。
2. 或把二者合并成单个 `operationMessage`，包含 `type/message`。
3. UI 上只渲染一个命令提示层，避免两个绝对定位提示重叠。

### P3 - 启停请求没有处理非 JSON 响应，网络/代理错误时会统一显示“网络错误”

- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 113-126

当前：

```ts
const data = await res.json()
```

如果后端返回 500 HTML、空响应、代理错误页，都会进入 catch 并显示“网络错误”。这不会破坏主流程，但排查时信息不够。

建议后续统一做一个 core API helper，类似 `devicesApi.apiCall()`：

- 先检查 `res.ok`；
- 再安全解析 JSON；
- 返回 `{ ok, error, data }`；
- 启停、status、logs、devices 都复用同一套错误处理。

## Fixed Since Previous Review

- `WebApiController.h` 已 include `<QJsonArray>`，修掉了上一轮 C++ 头文件编译问题。
- `DeviceEditDialog` 删除了未使用的 `saving` 状态，修掉了前端 `noUnusedLocals` 问题。
- 启停失败时已恢复 `tracks` / `trackRate` 快照，不再只回滚 `acquisition`。
- 启停失败已通过 `commandError` 显示给用户，不再只写 console。
- `TitleBar` 已接入 `commandError` 的展示和关闭。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 78%。

设备台账已经进入前端加载、CRUD、勾选控制请求、状态展示链路。相比几轮前，这已经不是纯 UI local state 了。

但最核心的“设备台账驱动真实控制”仍差一步：

```text
业务 deviceId
  -> endpointId / controllerId
  -> RadarWorkModeController or future controller
  -> accepted/rejected 仍返回业务 deviceId
  -> status 按同一映射 merge runtime state
```

这一步不宜再拖。否则后续 RuntimeSettings、Task、TrackDisplay 都会基于一个还没定死的设备控制语义往上叠。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

仍建议等设备映射规则确定后再开。RuntimeSettings 需要按设备/任务归属，否则会变成全局参数。

### Milestone 3 - Track Display Data Management

进度：未开始。

WS 航迹仍直接从 `GeneralPackage` 输出。等 Milestone 1 映射闭环完成后，可以开始定义 `TrackDisplaySnapshot`。

### Milestone 4 - Task Backend Domain

进度：未开始。

任务仍在前端 localStorage。暂时可接受。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

后续建议和 TrackDisplayManager 一起推进。

## Suggested Next Step

下一轮建议聚焦一个小闭环：把 `resolveControllable()` 从硬编码 `radar_cpu` 改成使用 `DeviceRegistry` 的 `endpointId` 映射。

最小实现不需要多 controller，只要先支持：

```text
DeviceInfo.id == radar_cpu
或 DeviceInfo.endpointId == radar_cpu
  -> 当前 RadarWorkModeController
```

并保证 accepted/rejected/status 都使用业务设备 id。这样 Milestone 1 才算真正站住。

## Verification

- 本轮为源码审查。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
