# Review for 66f0fb0

Full commit: `66f0fb059332402efee9d599e759dbc59ab9ed35`

Commit message: `review-6428334修改完成`

## Findings

### P1 - `WebApiController.h` 使用 `QJsonArray` 但没有包含声明，C++ 编译会失败

- File: `core/web/WebApiController.h`
- Lines: 3-4
- File: `core/web/WebApiController.h`
- Line: 42

这版在头文件里新增了：

```cpp
QJsonArray buildDeviceStatusArray() const;
```

但 `WebApiController.h` 只 include 了 `QJsonObject`，没有 include `QJsonArray`，也没有前置声明 `class QJsonArray;`。

因为返回类型出现在类声明里，任何包含 `WebApiController.h` 的编译单元都需要先认识 `QJsonArray`。当前 `WebApiController.cpp` 里 include `<QJsonArray>` 不能解决其他编译单元包含头文件时报错的问题。

建议 Claude Code 修改：

```cpp
#include <QJsonArray>
```

放在 `WebApiController.h` 中，或用合适的前置声明。不过这里直接 include 更清晰，和 `QJsonObject` 一致。

### P1 - `saving` 状态被定义但未使用，前端 typecheck 仍会失败

- File: `ui/src/components/dialogs/DeviceEditDialog.tsx`
- Line: 17
- File: `ui/tsconfig.app.json`
- Lines: 15-16

上一轮未使用 `useState` 已经被修成实际状态，但这版新增的：

```ts
const [saving, setSaving] = useState(false)
```

只写入、不读取。项目开启了 `noUnusedLocals: true`，因此 `npm run build` 的 `tsc -b` 阶段仍会失败。

建议 Claude Code 修改：

1. 如果暂时不做保存中 UI，就删除 `saving/setSaving`。
2. 更建议把 `saving` 真正传给 `RadarEditForm` / `SubDeviceEditForm`，禁用保存/取消按钮并显示“保存中”。
3. 最小可行改法：在 `DeviceEditDialog` 中用 `saving` 阻止遮罩关闭和 Escape 关闭，但保存按钮本身仍然需要后续补 disabled，否则用户仍可重复点击。

### P2 - 启停失败回滚只恢复 `acquisition`，没有恢复 `tracks/trackRate`

- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 86-112

开始采集时当前逻辑先清空：

```ts
tracksRef.current = []
trackCountRef.current = 0
setState(... tracks: [], trackRate: 0 ...)
```

如果后端拒绝或网络失败，只回滚：

```ts
setState(prev => ({ ...prev, acquisition: isWorking }))
```

这会导致一次失败的“开始采集”把原本页面上的航迹列表清掉。虽然实际工作中开始前可能没有旧航迹，但在任务切换、历史回放、失败重试、后续 TrackDisplaySnapshot 接入后，这种局部回滚会越来越容易制造 UI 状态错乱。

建议 Claude Code 修改：

1. 在 optimistic 更新前保存 `prev.tracks` / `prev.trackRate` 的快照，失败时一起恢复。
2. 或者更稳：不要在后端确认前清空航迹，只在 start 成功后清空。
3. 后续任务/历史回放接入后，实时航迹和历史航迹应拆状态，避免控制命令失败影响历史展示。

### P2 - 启停错误仍没有用户可见反馈

- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 98-111
- File: `ui/src/components/layout/TitleBar.tsx`
- Lines: 76-87

这版已把 `deviceIds` 发给后端，并在失败时回滚 `acquisition`，主链路比上一版明显更接近闭环。

但失败原因仍然只 `console.warn`：

- 不支持的设备 id；
- 被节流；
- 网络错误；
- 部分设备 rejected。

用户可见层只保留了“未勾选设备”提示。结果是用户点击开始后按钮跳一下又回去，却不知道是设备不支持、后端拒绝还是网络问题。

建议 Claude Code 修改：

1. 在 `CoreState` 增加 `commandError` 或 `operationMessage`。
2. `TitleBar` 复用当前 `noSelectionHint` 样式显示启停失败原因。
3. 对 `throttled:true` 给出更温和提示，例如“操作太频繁”。
4. 对 `rejectedDeviceIds` 显示具体被拒绝的设备 id，后续可映射成设备名称。

### P2 - `buildDeviceStatusArray()` 只保留最小台账字段，状态契约仍不完整

- File: `core/web/WebApiController.cpp`
- Lines: 298-335
- File: `ui/src/lib/protocol.ts`
- Lines: 3-7

这版把状态输出抽成 `buildDeviceStatusArray()`，并改为先遍历 `DeviceRegistry`，再把 `radar_cpu` 的运行状态 merge 到同 id 设备上。这是正确方向。

但目前 status device 只输出：

- `id`
- `name`
- `kind`
- `state`
- `acquisition`

而 `DeviceStatus` TS 类型仍只声明了 `id/state/acquisition`。这导致 REST `/api/devices` 和 `/api/status` 的设备对象语义继续分裂：一个是台账，一个是状态。短期可接受，但从架构上看，前端现在需要在多个来源之间自己拼设备元数据和运行状态。

建议 Claude Code 后续不要继续让 UI 自己拼。可以定义明确 DTO：

```text
DeviceRegistryItem     // 台账
DeviceRuntimeState     // 运行状态
DeviceStatusView       // 台账 + runtime merged 的前端状态视图
```

当前 `buildDeviceStatusArray()` 可以先补齐 `endpointId/status` 等关键字段，并同步更新 `ui/src/lib/protocol.ts`。

## Fixed Since Previous Review

- 前端 `/api/start` / `/api/stop` 已发送 `checkedDeviceIds` 到后端。
- 前端已读取后端 `ok`，失败时不再永久相信 optimistic `acquisition` 状态。
- 未勾选设备校验已经移到 pending/throttle 之前，解决了“未勾选后 1.5 秒内无法再次操作”的交互问题。
- 后端节流响应已改为 `ok:false, throttled:true`，不再伪装成普通成功。
- 后端 status 输出已抽成 `buildDeviceStatusArray()`，并以 `DeviceRegistry` 为主 merge legacy `radar_cpu` 状态。
- `DeviceEditDialog` 已增加保存失败提示，方向正确。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 75%。

这版是 Milestone 1 里比较关键的一步：前端勾选设备已经进入控制请求，后端也开始按 accepted/rejected 返回控制结果。设备台账不再只是 CRUD 列表，正在进入控制链路。

但还没完全闭环：

```text
DeviceRegistry device id
  -> endpoint/controller binding
  -> real command dispatch
  -> status merge
  -> visible command result
```

当前仍然只有 `radar_cpu` 是真实可控设备。下一步核心不是继续修 UI 小状态，而是把 `DeviceRegistry.endpointId` / legacy controller / 后续多设备 controller 的映射规则定下来。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

仍建议等设备控制映射明确后开始。RuntimeSettings 必须知道参数属于哪个设备/任务，否则会退化成全局参数对象。

### Milestone 3 - Track Display Data Management

进度：未开始。

WS 航迹仍直接由 `WebApiController` 从 `GeneralPackage` 组 JSON。等 Milestone 1 编译和控制闭环稳定后，可以开始定义 `TrackDisplaySnapshot`。

### Milestone 4 - Task Backend Domain

进度：未开始。

任务仍在前端 localStorage。暂时不是最急，但设备控制链路稳定后，任务后端化会成为下一条主线。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

建议和 Milestone 3 一起推进，先把 core-web track payload 固定下来。

## Suggested Next Step

下一轮建议先做两件小而关键的事：

1. 修掉当前 C++/TS 编译硬错误：`QJsonArray` include、`saving` 未使用。
2. 明确设备控制映射规则：短期可以规定 `DeviceInfo.id == "radar_cpu"` 或 `endpointId == "radar_cpu"` 映射到当前 `RadarWorkModeController`，但必须写进代码和文档，不能只靠注释里的 Future。

这两步之后，Milestone 1 才值得继续往 RuntimeSettings 推。

## Verification

- 已通过源码确认 `ui/tsconfig.app.json` 开启 `noUnusedLocals` / `noUnusedParameters`。
- 当前环境 PATH 中没有 `npm`，无法运行 `npm run build`。
- 未运行 CMake；C++ 编译问题基于头文件 include 检查。
