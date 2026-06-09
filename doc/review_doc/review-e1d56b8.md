# Review: e1d56b8

Commit: `e1d56b8` (`review-1032246修改完成，运行时参数管理.md创建完成`)

## 总体判断

这版没有急着写代码，只新增了 `doc/business/运行时参数管理.md`。这个节奏是对的。

文档已经抓住了主线：

- `DPH` 是外部协议结构体，不是 UI model。
- UI 只读写内部运行时参数 DTO。
- `DphPublisher` 每 50ms 使用本地缓存发送，不主动轮询 UI/API。
- UI 不改参数就不发事件，后端继续使用上一次有效参数。
- 当前阶段不落库，只做内存运行时参数。

但在进入代码实现前，还需要把几个边界补清楚，否则 Claude 下一步很容易把工作模式、SystemBus 订阅、DataPacket payload 这些点写散。

## P1 - `workMode` 不应放进 RuntimeSettings，避免和 RadarWorkModeController 形成双 source of truth

文件：`doc/business/运行时参数管理.md`

当前 DTO 写了：

```cpp
struct RadarRuntimeSettings {
    uint32_t workMode = 0;          // 0=standby, 1=work
    uint32_t sceneNumber = 1;       // 景幅序号
    // 后续扩展：天线模式、频率、波形等
};
```

问题是系统已经有 `RadarWorkModeController` 管理工作/待机状态，并通过 `workStandby` 命令和回显维护状态机。如果 RuntimeSettings 里也放 `workMode`，系统就会出现两个工作状态来源：

- `RadarWorkModeController::state()`
- `RadarRuntimeSettings::workMode`

这会导致两个问题：

1. UI 改 RuntimeSettings 里的 `workMode`，是否等价于 start/stop？如果等价，就绕过了状态机；如果不等价，字段名会误导。
2. `DphPublisher` 组 DPH 时到底用状态机状态，还是 RuntimeSettings 里的 `workMode`？

### 建议改法

RuntimeSettings 第一阶段先不要包含 `workMode`。工作/待机仍由 `RadarWorkModeController` 独立负责。

如果 `DataPackageHeader.F_UI_Work_Mode` 必须填，建议明确来源：

```text
F_UI_Work_Mode 由 RadarWorkModeController 当前状态映射，或先保持固定协议默认值。
RuntimeSettings 不拥有工作模式状态。
```

DTO 可以先改成：

```cpp
struct RadarRuntimeSettings {
    uint32_t sceneNumber = 1;
    // 后续扩展：天线模式、频率、波形等
};
```

或者如果 `sceneNumber` 仍由 `DphPublisher` 自增维护，那第一阶段 DTO 可以先只放真正可由 UI 调整的字段，不要为了凑 DTO 把内部连续状态暴露出去。

## P1 - `DphPublisher` 不建议直接订阅 SystemBus，保持现有职责边界

文件：`doc/business/运行时参数管理.md`

当前文档写：

```text
RuntimeSettingsService
  → DphParamsChanged event (通过 SystemBus 发布)
  → DphPublisher 更新本地缓存
```

以及：

```text
DphPublisher 不直接访问 RuntimeSettingsService，只响应 DphParamsChanged 事件
```

这里容易被实现成 `DphPublisher` 直接订阅 `SystemBus`。但现有 `DphPublisher.h` 的职责边界明确写了：

```text
非职责：
- 不直接访问 SystemBus
```

这个边界是好的，不建议为了 RuntimeSettings 打破。

### 建议改法

推荐改成“桥接器/组装层订阅事件，DphPublisher 只暴露 apply 方法”：

```text
RuntimeSettingsService
  -> publish DphParamsChanged
  -> AppController 或 RuntimeSettingsBusBridge 订阅
  -> queued invoke DphPublisher::applyRuntimeSettings(settings)
  -> DphPublisher 更新本地缓存
```

这样：

- `RuntimeSettingsService` 不依赖 `DphPublisher`。
- `DphPublisher` 不依赖 `SystemBus`。
- `AppController` 作为组装层负责连线。
- SystemBus 回调线程不直接改 QObject 状态。

文档里应明确这一点，避免 Claude 下一步把订阅逻辑写进 `DphPublisher`。

## P1 - 需要定义 `DphParamsChanged` 的 payload type 和打包方式

文件：`doc/business/运行时参数管理.md`

当前只说：

```text
DphParamsChanged 事件通过 SystemBus 的 DataPacket 机制发布
```

但没有定义：

- 新增哪个 `PAYLOAD_TYPE_*`
- `sourceType` / `dispatchTarget` 用什么
- payload 是 trivially copyable struct，还是 JSON bytes
- `getPayloadTypeConstant<RadarRuntimeSettings>()` 是否要特化

现有 `GenericPackets.h` 的 `packDataToPayload<T>()` 要求：

```cpp
static_assert(std::is_trivially_copyable<T>::value)
```

所以如果下一步 RuntimeSettings DTO 含 `QString`、`QVector`、JSON 对象，不能直接走这个模板。

### 建议改法

第一阶段建议使用 trivially copyable struct，并在文档里明确：

```cpp
const uint32_t PAYLOAD_TYPE_DPH_PARAMS_CHANGED = 103; // 或新的运行时参数区间
const uint16_t SOURCE_RUNTIME_SETTINGS = 205;
```

并说明：

```cpp
template<>
inline uint32_t getPayloadTypeConstant<RadarRuntimeSettings>() {
    return PAYLOAD_TYPE_DPH_PARAMS_CHANGED;
}
```

如果后续参数变复杂，再考虑单独的 typed event 或 JSON payload，不要在这一步混用。

## P2 - `sceneNumber` 的归属需要再确认

文件：`doc/business/运行时参数管理.md`

当前 `sceneNumber` 被放入 RuntimeSettings：

```cpp
uint32_t sceneNumber = 1;
```

但现有 `DphPublisher` 里景幅号是连续状态：

```cpp
m_header.F_UI_Scene_Num++;
```

如果 UI 可以写 `sceneNumber`，它到底表示：

- 下一次发送的起始景幅号？
- 强制重置景幅号？
- 只是当前值查询？
- 每次 PUT 都覆盖自增状态？

这需要先定，不然实现时会非常容易打架。

### 建议改法

短期建议：

- `sceneNumber` 仍由 `DphPublisher` 自增维护。
- RuntimeSettings 只允许提供“重置景幅号”的显式命令，或者暂不开放。
- `GET /api/runtime-settings` 如果要展示当前景幅号，可以把它作为 runtime status，不作为 settings。

文档里可以区分：

```text
Settings：用户可配置参数
Status：系统运行时只读状态，例如当前 sceneNumber/sendCount
```

## P2 - API 需要说明 PUT 是全量替换还是 patch

文件：`doc/business/运行时参数管理.md`

当前只有：

```text
PUT /api/runtime-settings 更新运行时参数（触发事件）
```

需要明确：

- PUT 是否要求提交完整 DTO；
- 是否允许局部字段更新；
- 非法字段/越界值如何处理；
- 写入成功后是否返回后端确认后的完整 DTO。

建议第一阶段采用：

```text
PUT 为全量替换，后端校验并返回完整 RuntimeSettings DTO。
前端以后端返回值更新本地显示。
```

这和设备台账“以后端确认 DTO 为准”的原则一致。

## 推荐 Claude 下一步

先不要写 RuntimeSettings 代码。请先把 `doc/business/运行时参数管理.md` 补成可实现规格：

1. 移除或重新定义 `workMode`，明确工作模式仍归 `RadarWorkModeController`。
2. 明确 `sceneNumber` 是 settings 还是 status；建议暂不作为可写 settings。
3. 明确 DphPublisher 不直接订阅 SystemBus，由 AppController/bridge 订阅后调用 `applyRuntimeSettings()`。
4. 定义 `DphParamsChanged` 的 payload type、sourceType、dispatchTarget、打包方式。
5. 说明 `PUT /api/runtime-settings` 是全量替换，并返回后端确认后的完整 DTO。

## 架构结论

Milestone 2 的方向可以继续，但现在还处在“定边界”阶段，不建议马上写实现。

这一步的关键不是把 UI 字段接到 DPH，而是防止 DPH 协议结构体、运行时参数、工作模式状态机、SystemBus 事件这四件事互相污染。把上面几个边界补清楚后，再进入最小代码实现会稳很多。
