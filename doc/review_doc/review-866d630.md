# Review for 866d630

Full commit: `866d630dab938c01d651dee9b9cba671219c6992`

Commit message: `review-4a0d801修改完成`

## Findings

### P1 - 前端新增设备不再传 id，但后端仍要求 id 非空，新增设备会失败

- File: `ui/src/App.tsx`
- Lines: 103-116
- File: `ui/src/App.tsx`
- Lines: 128-141
- File: `core/web/WebApiController.cpp`
- Lines: 140-158

这版想把“创建设备后以前端本地 id 为准”改成“以后端返回完整设备为准”，方向是对的。但前后端契约没有真正对齐。

前端现在创建雷达时传空 id：

```ts
const r = await createDevice(radarToApiPayload('', d as RadarDevice))
```

后端仍然校验：

```cpp
if (d.id.isEmpty() || d.name.isEmpty())
    return "id 和 name 不能为空";
```

结果是新增设备必然失败，前端拿不到 `r.data.device`。这说明“后端确认 id”的规则还没有落成 API 契约。

建议 Claude Code 修改：

1. `POST /api/devices` 允许 body.id 为空。
2. 如果 id 为空，由后端生成稳定业务 id，例如：

```text
radar_yyyyMMddHHmmss_xxx
opto_...
level_...
gps_...
```

3. 后端生成 id 后再校验和创建，并返回完整 `device`。
4. `PUT /api/devices/{id}` 仍要求 path id 和 body id 一致；创建和更新的 id 规则要分开。
5. 后续可以保留“导入设备时指定 id”的能力，但普通 UI 新增应以后端生成为准。

### P1 - 设备管理业务概念仍混在一起：通信端点、业务设备、厂家/型号字典没有分层

- File: `config/endpoints.json`
- Lines: 1-14
- File: `core/business/DeviceRegistry.cpp`
- Lines: 77-97
- File: `ui/src/components/map/types.ts`
- Lines: 8-25
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 39-45
- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 11-13

你截图里的疑惑是对的：前端 UI 里的设备名称、品牌、阵面参数，和后端 `endpoints.json` 里的 `radar_cpu` 完全不是一回事。

当前代码里实际上混着三类东西：

```text
1. 通信端点 Endpoint
   例: endpoints.json 里的 radar_cpu
   含义: UDP 怎么收发、target port 是什么

2. 业务设备 Device
   例: NODE_ALPHA_01 / radar_cpu / radar_001
   含义: 用户在系统里管理的一台雷达、光电、水平仪、GPS

3. 厂家/型号/模板 Device Model
   例: 中电38所 / 中电14所 / 雷鹰
   含义: 厂家或型号带来的默认阵面、参数、能力
```

现在 UI 的“品牌”只是一个前端写死的枚举：

```ts
type RadarBrand = 'cetc38' | 'cetc14' | 'leiying'
```

它既不是后端台账中的厂家字典，也不是通信端点，更不是设备实例。`endpoints.json` 里的 `radar_cpu` 也不是设备名称，它只是通信端点 id。

所以当前虽然有 `DeviceInfo` 数据模型，也有 `endpointId` 边界，但业务语义还没有真正落地。结果就是：

- 用户看到的名称是随便写的 `NODE_ALPHA_01`；
- 品牌是前端测试时手写的选项；
- endpoint 是后端配置里的通信 id；
- 三者没有统一来源和解释；
- 新增设备时用户不知道自己是在创建“设备实例”、选择“厂家模板”，还是绑定“通信端点”。

建议先冻结业务分层：

```text
EndpointConfig:
  id = radar_cpu
  protocol/localPort/targets
  只表示通信端点，不出现在 UI 作为设备名称

DeviceInfo:
  id = radar_001
  displayName = NODE_ALPHA_01
  kind = Radar
  modelId = cetc38_default
  endpointId = radar_cpu
  faces/location/status

DeviceModel:
  id = cetc38_default
  vendor = 中电38所
  modelName = 默认单面阵雷达
  defaultFaces/defaultRange/capabilities
```

短期不一定要马上做完整 `DeviceModel` 表，但至少要在文档和代码中区分：

- 设备名称：用户给业务设备取的名字；
- 端点绑定：后端通信端点；
- 厂家/型号：参数模板或设备属性，不等于端点。

### P1 - 默认 seed 仍把业务设备 id 设置为 `radar_cpu`，会继续把“设备”和“端点”混为一谈

- File: `core/business/DeviceRegistry.cpp`
- Lines: 84-95

这版的 seed：

```cpp
dev.id = "radar_cpu";
dev.name = "NODE_ALPHA_01";
dev.endpointId = "radar_cpu";
```

它能让 demo 跑起来，但从业务建模上又把 device id 和 endpoint id 绑成同一个字符串。短期可以容忍，但如果把它作为正式规则，会继续制造概念混乱。

更合理的默认 seed 应该是：

```text
DeviceInfo.id        = radar_001
DeviceInfo.name      = NODE_ALPHA_01
DeviceInfo.endpointId= radar_cpu
DeviceInfo.modelId   = cetc38_default 或 brand=cetc38
```

这样 UI 显示的是业务设备 `NODE_ALPHA_01`，控制绑定到 endpoint `radar_cpu`，两者关系清晰。

建议 Claude Code：

1. 默认 seed 不要用 endpoint id 作为业务 device id。
2. 如果为了兼容历史，可以让 `mapsToRadarCpu()` 同时支持 `id == radar_cpu` 和 `endpointId == radar_cpu`，但新数据应使用 `radar_001 -> radar_cpu`。
3. 文档中明确 `radar_cpu` 是默认通信端点，不是默认业务设备名称。

### P2 - 前端品牌/厂家选项仍是硬编码，不受后端设备能力或配置约束

- File: `ui/src/components/map/types.ts`
- Lines: 8-13
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 45-45
- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 11-13

UI 中 `中电38所 / 中电14所 / 雷鹰` 是前端硬编码枚举。用户选择品牌后，目前只改变显示文本，并没有驱动：

- 默认阵面；
- 默认量程；
- 可用参数；
- 可绑定端点类型；
- 厂家协议适配能力；
- 后端校验。

这会造成“看起来像业务配置，实际只是 UI 标签”的错觉。

建议短期方案：

1. 将品牌字段重命名为 `vendor` 或 `modelId`，明确它是设备属性/模板。
2. 后端提供一个最小 `/api/device-models` 或静态配置文件，前端从后端加载选项。
3. 选择型号时填充默认 faces/maxRange，但用户仍可编辑。
4. 后端保存 vendor/modelId，不要只相信前端枚举。

### P2 - `endpointId` 已进入 DTO，但 UI 没有让用户理解“端点绑定”

- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 324-464
- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 111-126

当前新增雷达默认 `endpointId='radar_cpu'`，但 UI 表单里看不到端点绑定。编辑弹窗也没有 endpointId 字段。

这会让用户觉得保存的是“品牌/名称/阵面”，但真正决定是否可控的是一个隐藏字段。业务上这不透明。

建议短期 UI：

```text
设备名称: NODE_ALPHA_01
厂家/型号: 中电38所 / 默认单面阵
通信端点: radar_cpu - 雷达CPU通信
```

端点可以先只读或单选，至少让用户知道这台业务设备绑定到了哪个后端通信端点。

### P2 - WebApiController 继续复制 DeviceInfo -> JSON 组装，DTO 契约没有集中

- File: `core/web/WebApiController.cpp`
- Lines: 100-125
- File: `core/web/WebApiController.cpp`
- Lines: 165-185

`GET /api/devices` 和 `POST /api/devices` 各自手写了一份 DeviceInfo -> JSON。字段一多就会漂移，后续 `PUT` 如果也返回完整对象，还会出现第三份。

建议抽一个小 helper：

```cpp
QJsonObject deviceInfoToJson(const DeviceInfo& d);
```

并让 GET/POST/PUT/status 需要台账字段时都复用它。

## Business Reframe - 设备管理应该这样收束

当前不是“没有模型”，而是模型还没有转成业务规则。建议把设备管理重新收束成四个问题。

### 1. 我管理的是什么？

管理的是业务设备，不是 UDP 端点。

```text
DeviceInfo = 业务设备实例
EndpointConfig = 通信端点
DeviceModel = 厂家/型号/能力模板
```

### 2. 用户在 UI 里编辑什么？

用户编辑的是业务设备实例：

- 设备名称；
- 厂家/型号；
- 位置姿态；
- 阵面参数；
- 绑定哪个通信端点；
- 是否参与任务/控制。

用户不应该直接把 `radar_cpu` 理解成设备名称。

### 3. 后端如何决定它能不能控制？

只看绑定：

```text
DeviceInfo.endpointId -> EndpointConfig.id -> controller/route
```

没有 endpointId，就是不可控设备，可以显示但不能开始采集。

### 4. 前后端用什么 id 对齐？

只用后端确认的 `DeviceInfo.id`。前端临时 id 不能进入任务、控制、运行时参数、航迹归属。

## Fixed Since Previous Review

- `ensureDefaultControllableRadar()` 已从“空表 seed”升级为“没有可控雷达就补一条默认可控雷达”。
- `POST /api/devices` 开始返回完整 `device` 对象。
- 前端新增设备后开始尝试使用后端返回对象更新本地状态。
- `RadarDevice` / `radarToApiPayload` / `apiDeviceToRadar` 已携带 `endpointId`。
- 新增雷达默认值已带 `endpointId='radar_cpu'`。
- 设备身份规则已补进 `doc/business/设备台账与端点映射.md`。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 85%，但需要业务语义重整后再收尾。

底层链路已经接近完成，但设备管理的业务解释还没完成：

```text
EndpointConfig != DeviceInfo != DeviceModel
```

这条不定死，后续 RuntimeSettings、任务、航迹归属都会继续混乱。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

不建议开。RuntimeSettings 必须引用稳定的业务设备 id，而现在设备业务语义还没完全收束。

### Milestone 3 - Track Display Data Management

进度：未开始。

仍不建议开。

### Milestone 4 - Task Backend Domain

进度：未开始。

也不建议现在开，因为 task.deviceIds 的语义还没完全冻结。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Suggested Next Step

下一轮不要继续在当前 UI 字段上打补丁。建议先做一个“设备业务语义收束”任务：

1. 后端 `POST /api/devices` 支持空 id 并生成后端 device id。
2. 默认 seed 改成 `DeviceInfo.id=radar_001, endpointId=radar_cpu`，避免设备 id 和端点 id 混同。
3. 文档补一节：`EndpointConfig / DeviceInfo / DeviceModel` 三者定义和字段归属。
4. 前端新增/编辑表单把“品牌”改成“厂家/型号”，并显示“通信端点”绑定。
5. 短期先用静态 device model 配置，后续再考虑 `/api/device-models`。

这一步做完，设备管理才从“有表、有字段”变成“业务上说得通”。

## Verification

- 本轮为源码审查，并结合用户截图中的设备管理业务问题复盘。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
