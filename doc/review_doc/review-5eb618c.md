# Review for 5eb618c

Full commit: `5eb618c9465a1ae0d29ec87c9ffb212498f12323`

## Findings

### P1 - `/api/start` 和 `/api/stop` 对不支持的设备 id 返回假成功

- File: `core/web/WebApiController.cpp`
- Lines: 90-116

这版把状态接口扩展到了设备台账，但控制接口仍然只真正支持 legacy `radar_cpu`。当前逻辑是：

- `deviceIds` 为空时控制 `radar_cpu`；
- `deviceIds` 包含 `radar_cpu` 时控制 `radar_cpu`；
- `deviceIds` 只包含设备台账里的真实设备 id 时，不发送任何控制命令；
- 但接口最后仍然返回 `{ "ok": true }`。

这会让前端和用户以为选中的真实设备已经开始/停止采集，实际后端什么都没做。设备台账一旦成为 source of truth，这类“假成功”会直接破坏任务控制可信度。

建议 Claude Code 修改：

1. 过渡期内，如果 `deviceIds` 非空且没有任何 id 能映射到当前可控设备，返回 `{ ok:false, error:"unsupported device id" }`。
2. 如果一个请求里部分设备可控、部分不可控，返回 `acceptedDeviceIds` / `rejectedDeviceIds`，不要只给全局 `ok:true`。
3. 尽快建立 `deviceId -> endpointId/controller` 的控制映射，让 UI 选中的业务设备 id 能落到真实后端控制对象，而不是继续依赖硬编码 `radar_cpu`。

### P1 - 状态接口会产生重复设备 id，`radar_cpu` 和设备台账状态没有合并

- File: `core/web/WebApiController.cpp`
- Lines: 43-66
- File: `core/web/WebApiController.cpp`
- Lines: 268-293

`GET /api/status` 和 WS status 都先 append legacy `radar_cpu`，再 append `DeviceRegistry::list()`。如果设备台账里存在 id 为 `radar_cpu` 的雷达，前端会收到两个相同 id 的设备：

- 一个来自 `RadarWorkModeController`，有真实 `state/acquisition`；
- 一个来自 `DeviceRegistry`，被标成 `Offline/acquisition=false`。

这会让列表、勾选状态、状态灯和任务设备过滤都进入不确定状态。React key、`checkedDeviceIds`、状态聚合都会依赖 id 唯一性。

建议 Claude Code 修改：

1. 状态输出必须保证同一个 response 内设备 id 唯一。
2. 如果 registry 中已有 `radar_cpu` 或绑定到了当前 legacy controller，应把 `RadarWorkModeController` 的运行状态 merge 到该 registry 设备上。
3. 如果 registry 中没有可映射设备，才输出一个 transitional legacy 设备。
4. 明确一条过渡规则：业务设备 id、endpointId、legacy controller id 三者如何对应。

### P2 - 编辑设备弹窗在后端保存完成前关闭，失败时用户看不到结果

- File: `ui/src/components/dialogs/DeviceEditDialog.tsx`
- Lines: 9-25
- File: `ui/src/App.tsx`
- Lines: 147-162
- File: `ui/src/App.tsx`
- Lines: 214-221

`App.updateDevice/updateSubDevice` 已经改成 async，并且只有后端返回成功才更新本地状态，这是对的。但 `DeviceEditDialog` 的 props 仍然声明为同步 `void`，保存时直接：

```ts
onUpdateDevice(device.id, updates)
onClose()
```

所以后端失败时，弹窗已经关闭，本地状态不变，用户只会感觉“刚才点了保存但没生效”。这是典型异步边界没有传回 UI 的问题。

建议 Claude Code 修改：

1. 将 `onUpdateDevice/onUpdateSubDevice` 类型改为 `Promise<boolean>` 或 `Promise<void>`。
2. 弹窗保存按钮 await 后端结果，成功再关闭。
3. 失败时在弹窗内显示错误或至少保持弹窗不关闭。
4. 保存过程中禁用按钮，避免重复提交。

### P2 - 新增/删除设备的 UI 仍然把 async 操作当同步操作处理

- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 22-26
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 255-285
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 461-463
- File: `ui/src/App.tsx`
- Lines: 103-139

`App` 中新增/删除设备已经 await 后端 API，失败时不更新本地状态。但 `Sidebar` 的 props 仍然是 `void`，调用侧没有等待：

- 新增设备后立即 `resetForm()`，即使后端创建失败；
- 删除设备时立即从 `checkedDeviceIds` 里移除 id，即使后端删除失败；
- 用户只能从 console.warn 间接看到失败。

这会导致 UI 表单状态、勾选状态和后端台账短时间或长期不一致。

建议 Claude Code 修改：

1. `SidebarProps` 中设备 CRUD 回调返回 `Promise<boolean>`。
2. 新增成功后再清空表单；失败时保留输入并显示错误。
3. 删除成功后再清理勾选状态。
4. 后续可以统一做一个轻量 toast/status message，但当前至少不要静默失败。

### P2 - 设备 API helper 没有处理网络错误和非 JSON 响应

- File: `ui/src/lib/devicesApi.ts`
- Lines: 42-103
- File: `ui/src/App.tsx`
- Lines: 39-46

`fetchDevices/createDevice/updateDevice/deleteDeviceApi` 直接 `await res.json()`，没有检查 `res.ok`，也没有捕获 JSON parse 失败。后端未启动、返回 404/500、代理错误页或空响应时，调用方会抛异常。

启动加载处 `.catch(() => {})` 会静默吞掉错误，CRUD 调用处则没有 catch，可能触发未处理 Promise rejection。

建议 Claude Code 修改：

1. helper 返回结构化结果，例如 `{ ok:boolean, error?:string, data?:T }`。
2. 先检查 HTTP status，再解析 JSON。
3. 对网络错误、JSON parse 错误提供可显示的 error。
4. 启动加载可以允许 fallback 默认设备，但应记录可见日志，不要完全静默。

### P3 - WS 航迹仍直接从协议包组 JSON，TrackDisplayManager 尚未接入

- File: `core/web/WebApiController.cpp`
- Lines: 228-253

这一点不是本次提交的回归，但和 `review-6bcbfc2` 的主线有关：当前 WS `tracks` 仍然在 `WebApiController` 中直接从 `GeneralPackage` / `TrackInfo` 组 JSON。也就是说 UI 收到的仍接近原始协议点，而不是固定的 `TrackDisplaySnapshot`。

建议 Claude Code 暂时不要在这里继续堆 UI 字段。下一步应引入独立的 `TrackDisplayManager` / `TrackPresentationManager`，由它输出固定 core-web 航迹显示协议，`WebApiController` 只负责序列化和推送。

## Fixed Since Previous Review

- `WebService` 的 prefix route suffix 已经做 URL decode，解决了带特殊字符设备 id 的路径匹配问题。
- `ui/src/lib/devicesApi.ts` 已经开始集中维护设备 API 映射，前端不再到处手写 REST payload。
- 前端 CRUD 已经改成“后端成功后再更新本地设备列表”，比上一版的乐观更新可靠。
- 设备更新已将 partial patch 合并成完整设备后再 PUT，适配后端当前 full-replace 语义。
- `/api/status` 和 WS status 已开始把 `DeviceRegistry` 设备输出给前端，说明设备台账正在接入运行状态面。

## Refactor Progress Recap From `review-6bcbfc2`

### Platform Base

进度：基本收口。

`review-6bcbfc2` 里提到的 WebService 生命周期和 HTTP parser 局部问题已经被逐步修掉。当前更重要的问题不再是底层能不能跑，而是业务对象之间的状态语义要统一。

### Milestone 1 - Device Registry As Source Of Truth

进度：约 65%。

已经完成：

- 后端有 `DeviceRegistry`；
- 已有 `/api/devices` CRUD；
- 前端启动时从后端加载设备；
- 前端新增、删除、编辑开始走后端 API；
- 设备台账文档和架构拆分已经建立。

仍缺：

- 业务设备 id 到真实 endpoint/controller 的绑定语义；
- `/api/start`、`/api/stop` 对台账设备的真实控制；
- `/api/status` / WS status 对 registry device 和 legacy state 的合并；
- 前端 CRUD 的错误反馈和 async 状态；
- 默认设备、台账初始化、端点绑定校验的明确策略。

优先建议：下一步不要继续扩 UI 字段，先把“设备台账 id 能控制真实设备，并能返回真实状态”打通。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

架构 owner 已确认：`DphPublisher` 不应 50ms 轮询总线；UI 改参数时发事件，后端 `RuntimeSettingsService` 合成完整有效参数，`DphPublisher` 订阅变更并缓存最新参数，50ms 只负责按缓存发送。

建议等 Milestone 1 的设备绑定语义稳定后再做，否则运行时参数不知道属于哪个设备/任务。

### Milestone 3 - Track Display Data Management

进度：未开始。

架构 owner 已确认：上位机不负责航迹起批、消失、融合等生命周期算法；这属于数据处理系统。上位机需要的是 `TrackDisplayManager`，负责航迹显示样式、用户交互状态、追踪使能后的数据分发，以及固定 core-web 显示协议。

当前代码仍是 `WebApiController` 直接推 `GeneralPackage` 字段。下一步应先定义 `TrackDisplaySnapshot`，再逐步替换 WS payload。

### Milestone 4 - Task Backend Domain

进度：未开始。

任务仍在前端 localStorage。这个阶段可以先不急，但当设备台账稳定后，任务应成为后端 domain：任务关联设备、历史航迹、日志、操作记录，并通过状态机驱动 UI 工作流。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

owner 已确认这条不需要复杂 schema/codegen：无论接入哪个厂家，core 和 web 之间的航迹协议固定即可。建议在 Milestone 3 开始前先写最小协议文档，再用 C++ DTO 和 TS type 对齐。

## Suggested Next Step

建议 Claude Code 下一轮集中做一个纵向闭环：

```text
DeviceRegistry device id
  -> endpoint/controller binding
  -> start/stop command dispatch
  -> status merge
  -> frontend async success/error feedback
```

这比继续增加设备字段更重要。因为只有这条链路打通，设备台账才真正从“配置列表”变成“后端业务对象”。

## Verification

本次按源码审查完成，没有运行本地构建。当前环境仍未确认有可用 `cmake` PATH；如 Claude Code 下一轮修控制链路，建议至少做一次 core 编译和前端 typecheck。
