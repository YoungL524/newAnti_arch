# Review for 4a0d801

Full commit: `4a0d80135167aa864b08fe587bb840c558fdd79b`

Commit message: `review-98495af修改完成`

## Findings

### P1 - `seedIfEmpty()` 只处理空表，已有不可控旧设备的库仍会继续“不支持的设备 id”

- File: `core/business/DeviceRegistry.cpp`
- Lines: 77-94
- File: `core/AppController.cpp`
- Lines: 60-66

这版新增了默认设备 seed：

```cpp
if (list().isEmpty()) {
    d.id = "radar_cpu";
    d.endpointId = "radar_cpu";
    ...
    return create(d);
}
```

这能解决“全新数据库第一次启动”时的默认设备问题。但它解决不了已经运行过几轮的本地数据库：如果表里已经有一条前端生成的 `dev_...` 设备，`list().isEmpty()` 为 false，seed 不会执行，旧设备仍然没有 `endpointId=radar_cpu`，UI 勾选后仍然可能提示“不支持的设备 id”。

这就是现在最容易让项目失控的地方：代码逻辑看起来补了，但运行数据状态没有被迁移，用户体验仍然复现旧问题。

建议 Claude Code 修改：

1. 把 `seedIfEmpty()` 升级成 `ensureDefaultRadar()` 或 `ensureControllableRadar()`。
2. 逻辑不是“表为空才 seed”，而是“如果不存在任何 `mapsToRadarCpu` 的雷达设备，则创建/修复一条默认可控雷达”。
3. 对已有 `NODE_ALPHA_01` 或第一个 radar 设备，如果 `endpointId` 为空，可以迁移为 `endpointId=radar_cpu`，但要记录日志。
4. 如果存在多个雷达，避免盲目修改全部，只补一条明确默认设备。
5. 这类迁移应是幂等的：每次启动执行都安全，不重复插入。

### P1 - 创建设备仍由前端生成最终 id，POST 不返回完整后端设备，source of truth 还没真正回到后端

- File: `ui/src/App.tsx`
- Lines: 103-114
- File: `ui/src/App.tsx`
- Lines: 126-136
- File: `core/web/WebApiController.cpp`
- Lines: 151-158
- File: `ui/src/lib/devicesApi.ts`
- Lines: 88-96

上一轮已经明确：前端展示、勾选、任务绑定、启停控制所使用的设备 id，必须对应后端 `DeviceRegistry` 的业务设备 id。

但当前新增设备仍然是：

```ts
const id = nextDeviceId()
const r = await createDevice(radarToApiPayload(id, ...))
setDevices(prev => [...prev, { id, ...d }])
```

后端 `POST /api/devices` 也只返回：

```cpp
return { "ok": ok };
```

因此创建成功后，前端并没有以后端返回的完整 `DeviceInfo` 为准，而是继续相信本地对象。这会让后端 source of truth 被削弱：

- 后端如果未来改 id 生成规则，前端无法同步；
- 后端如果补默认字段、endpoint 绑定、状态字段，前端拿不到；
- task.deviceIds 仍可能绑定到前端生成 id；
- 新增子设备的 linkedRadarId 也会依赖前端本地状态。

建议 Claude Code 修改：

1. `POST /api/devices` 返回创建后的完整 DTO：`{ ok:true, device:{...} }`。
2. 前端 `createDevice()` 返回该 device，而不是 boolean。
3. `App.addDevice/addSubDevice` 用后端返回的 device 更新本地状态和 task.deviceIds。
4. 如果短期仍允许前端传 id，也必须由后端 echo 确认最终 id，前端以后端返回为准。
5. 后续更推荐：前端创建请求可不传 id，由后端分配业务 id。

### P1 - 新增雷达虽然 DTO 支持 `endpointId`，但 UI 表单仍没有 endpoint 绑定入口

- File: `ui/src/components/map/types.ts`
- Lines: 16-25
- File: `ui/src/lib/devicesApi.ts`
- Lines: 75-82
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 324-464

这版给 `RadarDevice` 和 `radarToApiPayload()` 补了 `endpointId`，这是正确方向。但新增雷达表单没有 endpoint 选择，也没有把默认 `endpointId=radar_cpu` 明确暴露出来。

当前 `RADAR_DEFAULTS` 来自 `Sidebar.tsx`，并没有 endpointId 字段。用户新增雷达时，`radarToApiPayload()` 会发送：

```ts
endpointId: d.endpointId || ''
```

也就是空 endpoint。结果是：新建设备能出现在列表里，但不可控。

建议 Claude Code 修改：

1. `RADAR_DEFAULTS` 增加 `endpointId: 'radar_cpu'`，至少让当前单雷达 demo 可控。
2. 更合理的是在表单中增加“端点绑定”字段，当前选项可以只有 `radar_cpu`。
3. UI 应标识没有 endpointId 的设备为“未绑定端点”，不要让用户误以为它可采集。
4. 后端校验也应在 `DeviceKind::Radar` 时检查 endpointId 是否存在于可用 endpoints 中；短期至少允许空但返回“不可控”状态。

### P2 - WebApiController 已经承载过多业务逻辑，开始吞掉业务服务层边界

- File: `core/web/WebApiController.cpp`
- Lines: 63-98
- File: `core/web/WebApiController.cpp`
- Lines: 120-194
- File: `core/web/WebApiController.cpp`
- Lines: 280-331
- File: `doc/architecture/02-应用架构.md`
- Lines: 52-75

现在 `WebApiController` 同时负责：

- REST route 注册；
- 设备 CRUD DTO；
- 设备校验；
- deviceId -> endpoint/controller 映射；
- start/stop command result；
- status runtime merge；
- WS 航迹协议组装；
- WS 日志和状态推送。

这就是“项目有点脱离掌控”的主要代码原因：Web adapter 正在吸收业务规则。架构文档里业务服务层才应该承载设备台账、工作状态控制、运行时参数和航迹显示管理。

建议不要继续在 `WebApiController` 里补业务逻辑。下一步应收束成：

```text
WebApiController
  -> DeviceRegistry / DeviceCommandService / DeviceStatusService
  -> DTO serialization only
```

最小重构建议：

1. 新增 `DeviceCommandService` 或先在 `DeviceRegistry` 中提供 `resolveControlBinding(deviceIds)`。
2. `WebApiController` 不再自己判断 `mapsToRadarCpu`，而是调用服务返回 `CommandPlan`。
3. `buildDeviceStatusArray()` 改为读取一个 `DeviceStatusView`，不要在 controller 中拼业务状态。
4. WS 航迹仍直接从 `GeneralPackage` 组 JSON，是下一阶段要迁走的边界债。

### P2 - 业务/数据文档没有同步“后端设备 id 为唯一身份”的新规则

- File: `doc/business/设备台账与端点映射.md`
- Lines: 1-69
- File: `doc/architecture/03-数据架构.md`
- Lines: 49-69

代码和 review 里已经明确：前端不能长期持有与后端不一致的设备 id。但正式业务文档还只写了“前端启动时 GET /api/devices”“添加/编辑/删除调 API”，没有写清楚设备身份规则。

建议补到正式架构文档，而不是只留在 review：

```text
设备业务 id 由后端 DeviceRegistry 确认。
前端临时 id 只允许存在于未保存草稿。
任务、运行时参数、控制命令、航迹归属都引用后端 deviceId。
```

这条规则应该进入 `doc/business/设备台账与端点映射.md` 和 `doc/architecture/03-数据架构.md`，否则后续 Claude 继续实现任务/运行参数时可能再次回到前端自生成 id 的路径。

## Overall Control Review

项目不是方向错了，而是现在进入了“每修一个点，旁边的契约就暴露一个洞”的阶段。要重新纳入掌控，需要先冻结几个基座契约，减少临时补丁。

### 当前已经稳定的方向

- 设备 source of truth 应在后端；
- 设备通过 `endpointId` 绑定通信端点；
- 前端 start/stop 发送后端 device id；
- 后端可以把 `endpointId=radar_cpu` 映射到当前 `RadarWorkModeController`；
- 默认设备应由后端 seed，而不是前端 fallback；
- Web 不应该直接长期依赖厂家协议。

### 当前最危险的发散点

1. **身份发散**：前端仍能生成最终设备 id，并把它写进 task/control。
2. **边界发散**：WebApiController 正在承载越来越多业务规则。
3. **数据状态发散**：seed 只管空表，不管已有旧数据迁移。
4. **DTO 发散**：POST/PUT 仍多处只返回 ok，不返回后端确认后的完整对象。
5. **文档发散**：review 里有新规则，但正式架构/业务文档还没同步。

### 建议冻结的三条规则

```text
Rule 1: 后端 DeviceRegistry 确认设备业务 id。
Rule 2: endpointId 是设备能否控制真实端点的唯一绑定入口。
Rule 3: WebApiController 只做 API adapter，不继续沉淀业务规则。
```

后续 Claude Code 的实现应先检查是否违反这三条。违反就先收束，不继续加功能。

## Fixed Since Previous Review

- `WebApiController.h` 已 include `<QSet>` / `<QString>`。
- 后端新增默认雷达 seed，空设备表首次启动会创建 `id=radar_cpu, endpointId=radar_cpu`。
- `RadarDevice` 和 `devicesApi` 已开始携带 `endpointId`。
- start/stop handler 已抽出公共 `handleCommand`，减少重复逻辑。
- `radarCpuDeviceIds()` 映射结果在单次请求内已缓存，不再重复遍历。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 85%，但还不能算完成。

Milestone 1 的主链路已经基本成型：

```text
DeviceRegistry
  -> endpointId
  -> start/stop accepted
  -> RadarWorkModeController
  -> status merge
  -> React display
```

剩余收尾不是继续补控制函数，而是补“身份一致性 + 旧数据迁移 + API 返回完整 DTO + 文档同步”。这几件做完，Milestone 1 才算真正收束。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

建议不要马上开。先把 Milestone 1 收口，否则 RuntimeSettings 会不知道引用哪个稳定 device id。

### Milestone 3 - Track Display Data Management

进度：未开始。

当前 WS 航迹仍直接在 `WebApiController` 从 `GeneralPackage` 组 JSON。等 Milestone 1 收口后，应把这条作为下一条架构主线，而不是继续让 WebApiController 变厚。

### Milestone 4 - Task Backend Domain

进度：未开始。

任务仍在前端 localStorage。设备 id 契约冻结前，不建议迁任务，否则会把错误 id 写进后端任务表。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Suggested Next Step

为了重新把项目纳入掌控，下一轮不要开新功能，只做收束：

1. 将 `seedIfEmpty()` 改为幂等的 `ensureDefaultControllableRadar()`，能修复已有旧库中没有可控雷达的问题。
2. 修改 `POST /api/devices` 返回完整后端 `DeviceInfo`，前端新增设备后以后端返回 id 和字段为准。
3. 新增/编辑雷达 UI 补 endpoint 绑定，至少默认 `radar_cpu`。
4. 把“后端 device id 是唯一业务身份”写进正式业务/数据架构文档。
5. 暂停 WebApiController 继续加业务逻辑，下一轮若再动设备控制，应先抽 `DeviceCommandService` 或等价 helper。

## Verification

- 本轮为源码审查和架构复盘。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
