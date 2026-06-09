# Review: ff81749

Commit: `ff81749` (`review-017d1c6修改完成`)

## 总体判断

这版已经把 RuntimeSettings 的核心边界基本补齐，并开始做最小类型落点：

- 第一阶段明确选择真实可写字段：`frequencyControl`、`waveControl`。
- `sceneNumber` 已从 Settings 中移除，改为 `DphStatus` 只读状态。
- `RadarRuntimeSettings` / `DphStatus` 放到 `core/business/RuntimeSettingsTypes.h`。
- `getPayloadTypeConstant<RadarRuntimeSettings>()` 特化放在业务类型头里，没有让 `GenericPackets.h` include 业务类型。
- bridge 按 `SOURCE_RUNTIME_SETTINGS` 订阅，不依赖 `dispatchTarget = UI_RADARWIDGET`。

这版方向可以继续，已经接近“可进入最小实现”。但实现前还要补两类小边界：参数校验范围，以及 `DphPublisher::applyRuntimeSettings()` 的映射语义。

## P1 - `frequencyControl` / `waveControl` 缺少合法范围和校验规则

文件：`doc/business/运行时参数管理.md`

当前 API 写了：

```json
{
  "frequencyControl": 1,
  "waveControl": 2
}
```

但没有说明这两个字段的合法范围、默认值含义、越界行为。

既然文档要求：

```text
后端校验完整 DTO，非法字段/越界值返回 400
```

就必须先定义什么是非法。

### 建议改法

先给第一阶段一个保守范围，即使只是临时范围也要写清楚：

```text
frequencyControl: uint32, 第一阶段允许 0..N，默认 0
waveControl: uint32, 第一阶段允许 0..N，默认 0
```

如果硬件枚举还没确认，就先写：

```text
第一阶段仅透传 uint32，但限制在 0..255，避免异常大值进入外部协议。
具体枚举含义待硬件协议确认后补充。
```

同时 API 文档补 400 响应示例：

```json
{
  "ok": false,
  "error": "frequencyControl out of range"
}
```

## P1 - `applyRuntimeSettings()` 应明确只改映射字段，不覆盖 DPH 持续状态

文件：`doc/business/运行时参数管理.md`

当前文档说：

```text
DphPublisher 新增 applyRuntimeSettings(const RadarRuntimeSettings&) 方法
```

但没有明确 apply 的粒度。

现有 `DphPublisher` 持有 `DataPackageHeader m_header`，里面既有持续状态：

- `F_UI_Scene_Num` 自增
- `F_UI_Time_Code` 每次发送更新时间
- `F_UI_Work_Mode` 由状态机或默认值决定
- `F_CR_Save_Path` 每次清空

也有可配置参数：

- `F_UI_Freq_Ctrl`
- `F_UI_Wave_Ctrl`

如果实现时把整个 `m_header` 重建，可能破坏 sceneNumber/sendCount/时间码这些持续状态。

### 建议改法

文档补一句明确语义：

```text
DphPublisher::applyRuntimeSettings(settings) 只更新 settings 映射字段：
- m_header.F_UI_Freq_Ctrl = settings.frequencyControl
- m_header.F_UI_Wave_Ctrl = settings.waveControl

不得重置 F_UI_Scene_Num、sendCount、timer、route、F_UI_Work_Mode。
```

这样 Claude 实现时不会把“运行时参数快照”误写成“整包头替换”。

## P2 - `RuntimeSettingsTypes.h` 需要加入构建清单

文件：`core/business/RuntimeSettingsTypes.h`

当前 CMake 的 include path 已包含 `core/business` 和 `core/bus`，所以 `"GenericPackets.h"` 在 CMake 下可以解析。

但 `core/CMakeLists.txt` 的 `CORE_HEADERS` 还没有加入：

```text
business/RuntimeSettingsTypes.h
```

这不一定影响编译，因为头文件只有被 include 时才参与，但建议加入，保持工程清单完整。

另外 `core/core.pro` 仍是旧清单，缺少 business include 和很多 business 文件。如果项目已经完全转 CMake，可以不急；如果仍有人用 qmake，这会成为后续构建分叉。

## P2 - `SOURCE_RUNTIME_SETTINGS = 205` 可以接受，但建议记录编号区间

文件：`core/bus/GenericPackets.h`

当前新增：

```cpp
const uint32_t PAYLOAD_TYPE_DPH_PARAMS_CHANGED = 103;
const uint16_t SOURCE_RUNTIME_SETTINGS = 205;
```

这没问题，但现在 payload/source 编号已经开始扩展。建议在 `GenericPackets.h` 或文档中约定编号区间：

```text
100..199: 系统/业务 payload
200..299: 内部服务 source
300..399: runtime/control target
```

避免后续 TrackDisplayManager、TaskDomain、RuntimeSettings 继续随手占号。

## 已确认修复项

- `workMode` 没有进入 RuntimeSettings。
- `sceneNumber` 不再作为可写 settings。
- Settings 已有真实可写字段，不再是空操作。
- `GenericPackets.h` 只放常量，不反向 include 业务 DTO。
- `RuntimeSettingsTypes.h` 中定义业务 DTO 和 payload type 特化。
- bridge 通过 `subscribeBySource(SOURCE_RUNTIME_SETTINGS)` 订阅，不依赖 UI target。

## 推荐 Claude 下一步

可以进入最小实现前的最后一轮文档补丁：

1. 补 `frequencyControl` / `waveControl` 合法范围和 400 错误响应示例。
2. 补 `applyRuntimeSettings()` 只更新映射字段、不重置 DPH 持续状态。
3. 把 `business/RuntimeSettingsTypes.h` 加入 `core/CMakeLists.txt` 的 `CORE_HEADERS`。
4. 如果 qmake 仍要维护，同步更新 `core/core.pro`；否则在文档中说明以 CMake 为准。
5. 记录 payload/source 编号区间。

## 架构结论

Milestone 2 的边界已经基本定住，可以准备进入最小代码实现。

但实现前请先补参数校验和 `DphPublisher` apply 语义。这两个点很小，却能避免后面出现“API 能写任意值”和“更新参数时重置 DPH 状态”的隐性问题。
