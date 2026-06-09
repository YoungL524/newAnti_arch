# Review: 017d1c6

Commit: `017d1c6` (`review-e1d56b8修改完成`)

## 总体判断

这版 `doc/business/运行时参数管理.md` 已经把上一轮几个关键边界补上了，方向明显更稳：

- `workMode` 已从 RuntimeSettings 中移除，工作/待机仍归 `RadarWorkModeController`。
- `DphPublisher` 不直接订阅 SystemBus，改由 AppController/bridge 订阅后 queued invoke。
- `DphParamsChanged` 已开始定义 payload type、sourceType、dispatchTarget、打包方式。
- PUT 语义已明确为全量替换，并返回后端确认 DTO。

但进入实现前还有两个需要补清楚的点：第一阶段 Settings 目前没有任何真正可写字段；以及 `GenericPackets.h` 如何感知业务 DTO 的方式需要谨慎，否则容易把总线通用层和业务层缠在一起。

## P1 - Settings 当前没有真正可写字段，PUT 会变成空操作

文件：`doc/business/运行时参数管理.md`

当前 Settings DTO：

```cpp
struct RadarRuntimeSettings {
    uint32_t sceneNumber = 1;       // 景幅序号（暂不作为可写字段，仅供查询）
    // 后续扩展：天线模式、频率、波形等
};
```

但文档同时说：

- Settings 是“用户可配置参数”
- `PUT /api/runtime-settings` 是“全量替换运行时参数”
- PUT 成功会触发 `DphParamsChanged`

这里存在语义矛盾：唯一字段 `sceneNumber` 又被标成“暂不作为可写字段，仅供查询”。这样第一阶段没有任何可写 settings，PUT 要么没有意义，要么只能触发一个没有参数变化的事件。

### 建议改法

二选一，先定清楚。

方案 A：第一阶段先做只读 Runtime Status，不开放 PUT。

```text
GET /api/runtime-settings
  -> settings: {}
  -> status: { currentSceneNumber, sendCount }

PUT 暂不开放，等出现真实可写参数后再实现。
```

方案 B：第一阶段必须选 1-2 个真实可写 DPH 字段。

可以从 `DataPackageHeader` 中挑不会和状态机打架的字段，例如：

```cpp
struct RadarRuntimeSettings {
    uint32_t frequencyControl = 0;  // maps to F_UI_Freq_Ctrl
    uint32_t waveControl = 0;       // maps to F_UI_Wave_Ctrl
};
```

字段名必须是内部业务名，不要直接暴露 `F_UI_*` 协议名。文档中再写映射：

```text
frequencyControl -> DataPackageHeader.F_UI_Freq_Ctrl
waveControl      -> DataPackageHeader.F_UI_Wave_Ctrl
```

如果业务上暂时不确定哪个字段能让用户调，就选方案 A，不要为了实现 RuntimeSettings 硬塞一个不可写字段。

## P1 - `sceneNumber` 不应同时出现在 Settings 和 Status

文件：`doc/business/运行时参数管理.md`

当前文档同时有：

```cpp
struct RadarRuntimeSettings {
    uint32_t sceneNumber = 1;       // 景幅序号（暂不作为可写字段，仅供查询）
};
```

以及：

```cpp
struct DphStatus {
    uint32_t currentSceneNumber = 0;
    uint64_t sendCount = 0;
};
```

既然 `sceneNumber` 由 `DphPublisher` 自增维护，并且第一阶段不开放重置，那么它应该只属于 Status，不应该放在 Settings。

### 建议改法

从 `RadarRuntimeSettings` 移除 `sceneNumber`，只保留：

```cpp
struct DphStatus {
    uint32_t currentSceneNumber = 0;
    uint64_t sendCount = 0;
};
```

未来如果需要“重置景幅号”，建议作为命令接口，而不是 settings 字段：

```text
POST /api/runtime-settings/scene-number/reset
```

或后续统一放入 control command，不和普通参数替换混在一起。

## P1 - `GenericPackets.h` 直接特化业务 DTO 可能造成层级反向依赖

文件：`doc/business/运行时参数管理.md`

当前建议：

```cpp
// GenericPackets.h 新增
const uint32_t PAYLOAD_TYPE_DPH_PARAMS_CHANGED = 103;
const uint16_t SOURCE_RUNTIME_SETTINGS = 205;

template<>
inline uint32_t getPayloadTypeConstant<RadarRuntimeSettings>() {
    return PAYLOAD_TYPE_DPH_PARAMS_CHANGED;
}
```

这里要小心：`GenericPackets.h` 是总线通用数据包定义。如果为了特化 `RadarRuntimeSettings` 而让它 include RuntimeSettings 的业务头文件，会让通用总线层依赖业务层。

### 建议改法

可以把常量放在 `GenericPackets.h`，但类型特化放在业务类型声明所在的头文件，或一个专门的 runtime packet 头文件：

```text
core/business/RuntimeSettingsTypes.h
```

其中：

```cpp
struct RadarRuntimeSettings { ... };

template<>
inline uint32_t getPayloadTypeConstant<RadarRuntimeSettings>() {
    return PAYLOAD_TYPE_DPH_PARAMS_CHANGED;
}
```

前提是该头文件 include `GenericPackets.h`，而不是让 `GenericPackets.h` include 业务头。

文档里需要明确“不要让 GenericPackets 反向包含业务类型”。

## P2 - `dispatchTarget = UI_RADARWIDGET` 语义不够准确

文件：`doc/business/运行时参数管理.md`

当前：

```text
dispatchTarget = UI_RADARWIDGET（指向 DPH 相关组件）
```

`UI_RADARWIDGET` 从命名上是 UI 组件目标，而 `DphParamsChanged` 的实际消费者是 AppController/bridge/DphPublisher 这一条运行时链路。用 UI target 容易误导后续订阅者。

### 建议改法

如果当前 SystemBus 只能用已有 target，可以先订阅 `sourceType = SOURCE_RUNTIME_SETTINGS`，不要依赖 `dispatchTarget`。

更清晰的做法是新增 runtime target：

```cpp
const uint16_t TARGET_DPH_PUBLISHER = 300; // 或 Runtime target 区间
```

然后：

```text
sourceType = SOURCE_RUNTIME_SETTINGS
dispatchTarget = TARGET_DPH_PUBLISHER
```

如果不想新增 target，文档至少应写：

```text
bridge 通过 subscribeBySource(SOURCE_RUNTIME_SETTINGS) 订阅；dispatchTarget 第一阶段不作为路由条件。
```

## P2 - 文档需要明确 GET 返回结构

文件：`doc/business/运行时参数管理.md`

当前只说：

```text
GET /api/runtime-settings 读取当前运行时参数 + 只读状态
```

建议补一个响应形状，避免前后端又手写漂移：

```json
{
  "ok": true,
  "settings": {
    "frequencyControl": 0,
    "waveControl": 0
  },
  "status": {
    "currentSceneNumber": 12,
    "sendCount": 1200
  }
}
```

如果第一阶段没有可写 settings，则返回：

```json
{
  "ok": true,
  "settings": {},
  "status": {
    "currentSceneNumber": 12,
    "sendCount": 1200
  }
}
```

## 推荐 Claude 下一步

继续先补文档，不要立刻写代码：

1. 决定第一阶段是“只读 status”还是“包含真实可写字段的 settings”。
2. 如果选择可写 settings，明确内部字段名和 DPH 字段映射。
3. 从 Settings 中移除 `sceneNumber`，只放到 `DphStatus`。
4. 明确 `getPayloadTypeConstant<RadarRuntimeSettings>()` 的放置位置，避免 `GenericPackets.h` 依赖业务头。
5. 重新定义或弱化 `dispatchTarget = UI_RADARWIDGET`，建议新增 runtime target 或只按 sourceType 订阅。
6. 补充 GET/PUT 的 JSON 响应示例。

## 架构结论

Milestone 2 方向仍然正确，但还没到可以开写实现的程度。

现在最重要的是先回答一个问题：第一阶段 RuntimeSettings 到底有没有“用户可写参数”。如果没有，就先做 DPH status/read-only 观测；如果有，就挑真实业务字段并写清内部 DTO 到 `DataPackageHeader` 的映射。这个问题定住后，再让 Claude 写最小代码会顺很多。
