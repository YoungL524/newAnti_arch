# Review for 98495af

Full commit: `98495afb33a51205b8e55b3c64d0b3ead2a35b9a`

Commit message: `review-87e5ae9修改完成`

## Findings

### P1 - `WebApiController.h` 声明 `QSet<QString>` 但没有 include `QSet`，C++ 编译会失败

- File: `core/web/WebApiController.h`
- Lines: 1-5
- File: `core/web/WebApiController.h`
- Line: 47

这版把 `radarCpuDeviceIds()` 抽成了私有 helper：

```cpp
QSet<QString> radarCpuDeviceIds() const;
```

但 `WebApiController.h` 只 include 了：

```cpp
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
```

没有 include `<QSet>`。`WebApiController.cpp` 里 include `<QSet>` 不能解决其他编译单元包含头文件时报错的问题。

建议 Claude Code 修改：

```cpp
#include <QSet>
#include <QString>
```

`QString` 当前可能被 Qt 其他头间接带入，但既然头文件签名里直接使用了 `QString`，也建议显式 include，减少隐式依赖。

### P1 - 默认 UI 设备 `NODE_ALPHA_01` 没有后端台账绑定，用户启动时必然“不支持的设备 id”

- File: `ui/src/App.tsx`
- Lines: 23-46
- File: `ui/src/components/map/types.ts`
- Lines: 16-45
- File: `ui/src/lib/devicesApi.ts`
- Lines: 75-82
- File: `core/business/DeviceRegistry.cpp`
- Lines: 53-76

你截图里的现象是：

```text
侧边栏显示 NODE_ALPHA_01
点击开始采集
提示“不支持的设备 id”
```

这不是单纯的 UI 提示问题，而是当前数据初始化链路还没闭合。

前端初始状态会本地生成一个默认设备：

```ts
const [devices, setDevices] = useState<RadarDevice[]>(() => [
  { id: nextDeviceId(), ...DEFAULT_DEVICE },
])
```

这个设备的 id 形如 `dev_...`，不是 `radar_cpu`；`RadarDevice` 类型也没有 `endpointId` 字段。启动时 `fetchDevices()` 只有在后端返回设备时才覆盖默认设备：

```ts
if (radars.length > 0 || subs.length > 0) {
  setDevices(radars)
  setSubDevices(subs)
}
```

如果后端台账为空，UI 就继续显示这个本地默认设备。用户勾选它后，前端发送 `deviceIds=["dev_..."]`，后端 `radarCpuDeviceIds()` 里没有这个 id，于是返回“不支持的设备 id”。这正是截图里的情况。

架构上，Milestone 1 已经明确“设备台账是 source of truth”。因此默认可控雷达不应该只存在于前端 fallback。

更进一步，这里需要定死一条身份规则：**前端展示、勾选、任务绑定、启停控制所使用的设备 id，必须和后端 `DeviceRegistry` 中的业务设备 id 对应**。如果前端存在临时 id，也必须只存在于“未保存草稿”阶段，并且保存成功后立即替换成后端返回的 device id。不能让 `dev_...` 这类前端本地 id 长期参与控制链路。

建议 Claude Code 修改：

1. 后端 `DeviceRegistry::ensureSchema()` 后做最小 seed：如果设备表为空，创建一条默认雷达设备：

```text
id = radar_cpu 或 radar_001
name = NODE_ALPHA_01
kind = Radar
endpointId = radar_cpu
brand/maxRange/facesJson 使用当前 UI 默认值
```

2. 前端 `DEFAULT_DEVICE` 只能作为“后端不可用时的演示 fallback”，不能作为可控设备参与 start/stop。
3. 如果 `fetchDevices()` 失败或后端台账为空，UI 应显示“设备台账未初始化/后端未连接”，不要默认给用户一个可勾选、但必然不可控的设备。
4. `radarToApiPayload()` 和 `RadarDevice` 后续应携带 `endpointId`，否则用户新增的雷达也无法绑定到真实 endpoint。
5. 所有 start/stop、task.deviceIds、runtime settings、track display ownership 后续都应只引用后端 device id，不引用前端本地临时 id。

### P2 - 新增/编辑雷达无法设置 `endpointId`，用户创建的雷达仍不可控

- File: `ui/src/components/map/types.ts`
- Lines: 16-24
- File: `ui/src/lib/devicesApi.ts`
- Lines: 75-82
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 324-464

后端现在已经支持通过 `DeviceInfo.endpointId == "radar_cpu"` 映射控制。但前端 `RadarDevice` 类型没有 `endpointId`，新增设备表单也没有 endpoint 绑定字段，`radarToApiPayload()` 没有发送 endpointId。

这意味着即使用户通过 UI 新增一个雷达，后端也会创建出 `endpointId=""` 的设备。这个设备可以显示、可以勾选，但仍然不能开始采集。

这里还有一个比 endpoint 更底层的问题：前端新增设备时先生成本地 id，再把这个 id 作为后端 id 发送。短期能跑，但身份来源变成了前端。为了保证 source of truth 在后端，建议后端负责最终 device id 分配或确认，前端只在请求期间使用临时 id。创建成功后，UI 必须以 `/api/devices` 返回的后端 id 为准。

建议 Claude Code 下一步补齐：

1. `RadarDevice` 增加 `endpointId?: string`。
2. `apiDeviceToRadar()` 从后端带回 `endpointId`。
3. `radarToApiPayload()` 写回 `endpointId`。
4. 新增/编辑雷达表单至少提供一个 endpoint 选择；当前可先固定选项 `radar_cpu`。
5. 如果设备没有 endpointId，UI 应在设备项上显示“未绑定端点”，并禁用采集勾选或启动时给出明确提示。
6. `POST /api/devices` 最好返回创建后的完整 `DeviceInfo`；前端用返回值更新列表，而不是只靠本地 `{ id, ...d }` 追加。
7. 如果仍允许前端传 id，后端也必须返回最终确认的 id，并且前端后续 task/runtime/control 全部使用这个确认 id。

### P2 - `radarCpuDeviceIds()` 每个请求内被重复查询，后续设备多了会增加不必要开销

- File: `core/web/WebApiController.cpp`
- Lines: 71-80
- File: `core/web/WebApiController.cpp`
- Lines: 321-331

start/stop handler 中先调用一次：

```cpp
auto c = radarCpuDeviceIds()
```

构造 accepted，随后执行命令时又在循环里调用：

```cpp
for (const auto& v : accepted)
    if (radarCpuDeviceIds().contains(v.toString())) { ... }
```

当前设备很少，影响不大。但 `radarCpuDeviceIds()` 会遍历 `DeviceRegistry::list()`，后续设备增加后没必要在同一次请求中重复查。

建议把同一个请求内的映射集合缓存到局部变量，并用于 accepted/rejected/dispatch 三个阶段。

### P3 - start/stop handler 重新出现较多重复逻辑

- File: `core/web/WebApiController.cpp`
- Lines: 65-98

上一版用 `makeHandler(bool work)` 去重，这版为了修捕获问题又展开成 start/stop 两段，逻辑几乎相同。短期可以接受，因为现在更重要的是跑通 Milestone 1。

但等 P1/P2 修完后，建议把公共逻辑抽成私有 helper，例如：

```cpp
QJsonObject handleRadarWorkCommand(const QJsonObject& body, bool work);
```

这样 `accepted/rejected/dispatch/throttle` 契约不会在 start/stop 两处漂移。

## Fixed Since Previous Review

- `resolveControllable` 的局部 lambda 捕获问题已通过私有 helper 方向解决。
- start/stop 已改为独立的 `m_lastStartMs` / `m_lastStopMs`，快速反向操作不会被同一个静态变量误伤。
- 控制映射规则已抽到 `mapsToRadarCpu()` / `radarCpuDeviceIds()`，状态 merge 和控制 accepted 开始共用同一规则。
- `endpointId == "radar_cpu"` 的设备 accepted 后，现在会触发一次 `requestWork()` / `requestStandby()`。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 82%，但仍有一个体验/数据初始化阻塞。

代码层面的控制映射已经接近闭环：

```text
业务 deviceId
  -> DeviceRegistry.endpointId == radar_cpu
  -> acceptedDeviceIds
  -> RadarWorkModeController requestWork/requestStandby
  -> buildDeviceStatusArray merge runtime state
```

但实际运行时，默认 UI 设备没有进入后端台账，也没有 endpointId。因此用户看到的第一台设备不是 source-of-truth 设备，而是前端 fallback 设备。这就是截图中“不支持的设备 id”的根因。

Milestone 1 下一步不应继续只修控制函数，而要补“默认台账初始化 + endpoint 绑定 UI/DTO + 前后端设备 id 身份一致性”。这一步完成后，设备台账才真正能作为 source of truth。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

仍建议等默认设备台账和 endpoint 绑定稳定后再开始。

### Milestone 3 - Track Display Data Management

进度：未开始。

仍未到启动时机。

### Milestone 4 - Task Backend Domain

进度：未开始。

暂时可接受。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Suggested Next Step

下一轮建议只做两件事：

1. 修 `WebApiController.h` 的 `<QSet>` / `<QString>` include。
2. 给后端设备台账 seed 一条默认可控雷达，或者让前端首次启动时创建一条 `endpointId=radar_cpu` 的真实台账设备。更推荐后端 seed，因为设备 source of truth 应该在后端。
3. 统一设备 id 契约：前端展示和操作使用后端 device id；前端临时 id 只能用于未保存草稿，保存成功后必须被后端 id 替换。

完成后再验证截图里的路径：

```text
打开 UI
看到 NODE_ALPHA_01 来自 /api/devices
勾选 NODE_ALPHA_01
点击开始采集
后端 acceptedDeviceIds 包含该设备 id
不再提示“不支持的设备 id”
```

## Verification

- 本轮为源码审查，并结合用户截图现象分析。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake；C++ 头文件 include 问题基于源码检查。
