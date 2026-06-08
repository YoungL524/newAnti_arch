# Review for 1833fbc

Full commit: `1833fbce4dde9167897c0cf767945e16ca60663f`

Commit message: `review-866d630修改完成`

## Findings

### P1 - 后端 id 生成用局部 static counter，进程重启/并发/同秒创建时仍有冲突风险

- File: `core/web/WebApiController.cpp`
- Lines: 145-157

这版修掉了“前端传空 id、后端仍要求 id 非空”的问题。`POST /api/devices` 已经会在 body.id 为空时生成 id：

```cpp
static uint32_t idCounter = 0;
d.id = prefix + QDateTime::currentDateTime().toString("yyyyMMddHHmmss")
       + "_" + QString::number(++idCounter);
```

方向是对的，但这个 id 生成仍不够稳：

- 进程重启后 `idCounter` 归零；
- 如果同一秒内曾经创建过 `radar_20260608120000_1`，重启后同一秒再创建会撞主键；
- 生成逻辑放在 `WebApiController` 局部 lambda 中，不属于设备台账 domain；
- 未来如果还有导入、批量创建、CLI 创建，会复制另一套 id 逻辑。

建议 Claude Code 修改：

1. 把 id 生成移到 `DeviceRegistry` 或 `DeviceIdGenerator`。
2. 使用更稳的唯一后缀，例如 `currentMSecsSinceEpoch` + 短随机数，或 Qt UUID。
3. 生成后检查 `get(id)`，冲突则重试。
4. `WebApiController` 不应该知道 id 生成规则，只调用 `DeviceRegistry::create()` 或 `createWithGeneratedId()`。

### P1 - `ensureDefaultControllableRadar()` 不会修复历史 `id=radar_cpu` 设备，和目标模型仍有偏差

- File: `core/business/DeviceRegistry.cpp`
- Lines: 77-97

Claude 总结中说当前默认设备是：

```text
id=radar_001, endpointId=radar_cpu
```

对“新库”是对的。但对已经 seed 过旧数据的库不一定成立。

当前逻辑：

```cpp
for (const auto& d : list()) {
    if (d.id == "radar_cpu" || d.endpointId == "radar_cpu")
        return true;
}
```

如果历史库里已有旧 seed：

```text
id=radar_cpu, endpointId=radar_cpu
```

函数会直接 return true，不会迁移为 `radar_001 -> radar_cpu`。这意味着不同开发者/不同数据库会出现不同默认设备 id。对后续 task/runtime/track 归属非常危险。

建议 Claude Code 明确迁移策略：

1. 如果接受历史 `id=radar_cpu`，文档不要声称默认设备一定是 `radar_001`。
2. 如果要统一成 `radar_001`，需要写幂等迁移：创建/更新 `radar_001`，并处理旧 `radar_cpu` 设备的任务引用或删除策略。
3. 短期至少在启动日志中输出当前 default controllable device id，便于排查。

### P2 - `WebApiController` 仍在沉淀设备业务规则，Claude 总结把“目标状态”说成了“当前状态”

- File: `core/web/WebApiController.cpp`
- Lines: 63-98
- File: `core/web/WebApiController.cpp`
- Lines: 145-190
- File: `core/web/WebApiController.cpp`
- Lines: 336-352

Claude 总结里写：

```text
WebApiController → API 表现层（不沉淀业务规则）
```

这是我们想要的目标，但不是当前事实。现在 `WebApiController` 仍负责：

- 生成设备 id；
- 校验设备字段；
- 拼 DeviceInfo DTO；
- 判断 `mapsToRadarCpu`；
- 构造 accepted/rejected；
- 决定是否调用 `RadarWorkModeController`；
- 组装 status runtime merge。

这说明 Milestone 1 代码虽跑通，但架构边界还没收干净。

建议 Claude Code 下一步不要继续把设备逻辑塞进 WebApiController，而是按下面方向抽离：

```text
DeviceRegistry
  - create device
  - generate/confirm device id
  - validate basic fields
  - list/get/update/remove

DeviceControlResolver 或 DeviceCommandService
  - resolve deviceIds -> CommandPlan
  - acceptedDeviceIds/rejectedDeviceIds
  - shouldControlRadarCpu

WebApiController
  - parse JSON
  - call service
  - return DTO
```

可以先不大重构，但新逻辑不要再继续进 WebApiController。

### P2 - DeviceModel 仍未建立，品牌/厂家/型号继续只是前端枚举

- File: `ui/src/components/map/types.ts`
- Lines: 8-13
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 45-45
- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 11-13
- File: `doc/business/设备台账与端点映射.md`
- Lines: 10-31

Claude 总结里把这一点列入“仍未做”，判断准确。

这不是一个锦上添花功能，而是设备管理业务合理性的关键。否则 UI 里的“中电38所/中电14所/雷鹰”仍只是前端标签，不会约束：

- 默认阵面；
- 默认量程；
- 可配置参数；
- 可绑定 endpoint 类型；
- 后端协议适配能力；
- 设备能力说明。

建议下一阶段先做最小 DeviceModel，不要做复杂：

```text
DeviceModel:
  id: cetc38_single_face
  vendor: 中电38所
  modelName: 默认单面阵雷达
  kind: Radar
  defaultFaces
  defaultMaxRange
  controllableEndpointKinds: ["radar_cpu"]
```

实现可以先用后端静态 JSON 或 C++ 常量，API 暴露：

```text
GET /api/device-models
```

前端“品牌”下拉应改成“厂家/型号”，从后端加载。

### P2 - endpoint 绑定 UI 仍未做，用户看不到设备为何可控/不可控

- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 330-465
- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 104-180

Claude 总结里也承认 endpoint 绑定 UI 未做。这个必须排在 RuntimeSettings/Task 之前。

当前 `endpointId` 已经进 DTO，但新增/编辑界面仍没有“通信端点”字段。用户看到的是名称、厂家、位置、阵面，却不知道这台设备绑定到了 `radar_cpu`，也不知道新增设备如果没有 endpoint 为什么不可控。

建议最小 UI：

```text
通信端点: radar_cpu - 雷达CPU通信
```

短期可以只读或单选。重点不是做得多漂亮，而是让用户理解“业务设备”和“通信端点”的绑定关系。

## Claude Summary Evaluation

Claude 的总结整体方向是对的，但有三处需要修正表述，避免把“目标状态”误认为“完成状态”。

### 判断准确的部分

- `endpoints.json` 是通信端点，和业务设备不是一回事。
- `DeviceRegistry` 已经成为设备台账的主要来源。
- 前端新增设备开始尝试以后端返回 DTO 为准。
- 控制链路已经接近：`deviceId -> endpointId=radar_cpu -> RadarWorkModeController`。
- DeviceModel、endpoint 绑定 UI、任务后端化、RuntimeSettings、TrackDisplayManager 都还没做。

### 需要纠偏的部分

1. **“WebApiController 不沉淀业务规则”不准确。**  
   当前它仍沉淀了不少设备业务规则，只能说这是目标架构。

2. **“默认设备 id=radar_001”只对新库成立。**  
   旧库如果已有 `id=radar_cpu`，当前迁移不会改成 `radar_001`。

3. **“后端生成 id”已开始，但生成策略不够稳。**  
   目前生成逻辑在 WebApiController 局部 lambda，且 counter 进程重启会重置。

## How Claude Code Should Proceed

下面给 Claude Code 的执行建议要按顺序做，不要跳到 RuntimeSettings 或 TrackDisplayManager。

### Step 1 - Freeze Device Identity

目标：设备 id 规则完全由后端确认。

要做：

1. 把设备 id 生成移到 `DeviceRegistry`。
2. 使用不会因进程重启撞库的 id。
3. `POST /api/devices` 返回完整设备 DTO。
4. 前端不再保留任何最终设备 id 生成逻辑，只保留临时草稿状态。

验收：

```text
新增雷达 -> 后端返回 radar_xxx -> UI 使用 radar_xxx -> task.deviceIds 使用 radar_xxx
```

### Step 2 - Freeze Device / Endpoint Binding

目标：用户能看懂设备是否绑定通信端点。

要做：

1. 提供 endpoint 列表，短期可硬编码返回 `radar_cpu`。
2. 新增/编辑雷达表单显示 endpoint 绑定。
3. 未绑定 endpoint 的设备显示“未绑定端点”，不可执行采集。
4. 后端校验 radar endpointId 是否有效。

验收：

```text
radar_001 显示绑定 radar_cpu
新增 radar_002 可选择不绑定或绑定 radar_cpu
未绑定设备点击开始时提示“未绑定通信端点”，不是“不支持设备 id”
```

### Step 3 - Introduce Minimal DeviceModel

目标：品牌/厂家/型号不再是前端随便写的枚举。

要做：

1. 后端定义最小 DeviceModel 列表。
2. API: `GET /api/device-models`。
3. 前端从 API 加载厂家/型号。
4. 选择型号时填充默认 faces/maxRange。

验收：

```text
UI 下拉显示“中电38所 / 默认单面阵雷达”
选中后阵面和量程来自 model 默认值
保存 DeviceInfo.modelId 或当前 brand 字段的明确替代字段
```

### Step 4 - Thin WebApiController

目标：WebApiController 回到 adapter。

要做：

1. 抽 `deviceInfoToJson()` 成成员/helper。
2. 抽 `DeviceControlResolver` 或 `DeviceCommandService`。
3. start/stop route 只调用服务并返回结果。

验收：

```text
WebApiController 不再直接生成 device id
WebApiController 不再直接判断 endpointId == radar_cpu
```

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 88%，但还不能关。

现在主链路已经接近可用，但还差三件收口：

- 稳定后端 id 生成；
- endpoint 绑定 UI/校验；
- 最小 DeviceModel。

这三件做完，Milestone 1 才能真正结束。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

不要现在开。设备身份、endpoint 绑定、DeviceModel 还没完全冻结。

### Milestone 3 - Track Display Data Management

进度：未开始。

继续等待。

### Milestone 4 - Task Backend Domain

进度：未开始。

设备 id 稳定后再迁任务，否则 task 表会固化不成熟的 device id 规则。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Suggested Next Step

下一轮只做 Step 1 和 Step 2，不要碰 RuntimeSettings：

```text
DeviceRegistry 负责生成稳定 device id
WebApiController 不再生成 id
前端新增设备完全以后端 DTO 为准
新增/编辑设备 UI 显示 endpoint 绑定
后端校验 endpointId
```

DeviceModel 可以排下一轮，但文档应先补定义。

## Verification

- 本轮为源码审查，并对 Claude Code 总结做事实核对。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
