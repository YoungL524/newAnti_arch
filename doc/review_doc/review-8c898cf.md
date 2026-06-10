# Review 8c898cf

提交：`8c898cf review-a76d500修改完成`

## 总体结论

这版已经把 `review-a76d500.md` 里的关键问题基本修完：

- `RuntimeSettingsService` 新增 `validate()`，`set()` 内部复用同一套校验。
- Web 层移除了 `255` 业务范围硬编码，只负责 JSON 数值形态校验。
- Web 层在 publish 前先调用 `RuntimeSettingsService::validate()`，避免 API 和 Service 校验规则分叉。
- `subscribeBySource()` 返回 `0` 时已经输出 `qWarning()`。

从代码结构看，Milestone 2 的后端基座可以暂时收口。当前没有看到需要阻断继续推进的实现问题。

## P2：业务文档里的 RuntimeSettings 数据流需要同步最新提交顺序

位置：`doc/business/运行时参数管理.md:16-31`

文档当前仍写成：

```text
UI 参数编辑
  → WebApiController
  → RuntimeSettingsService (更新内存快照)
  → SystemBus publish DphParamsChanged
  → DphPublisher.applyRuntimeSettings(settings)
```

但当前代码实际顺序已经调整为：

```text
UI 参数编辑
  → WebApiController 解析 JSON
  → RuntimeSettingsService.validate(settings)
  → SystemBus publish DphParamsChanged
  → RuntimeSettingsService.set(settings)
  → AppController bridge
  → DphPublisher.applyRuntimeSettings(settings)
```

这里的顺序差异不是纯文字问题。之前我们刻意把逻辑改成 `validate -> publish -> set`，是为了避免 publish 失败时 RuntimeSettingsService 已经半提交。  
如果文档继续写“先更新内存快照，再 publish”，后续 Claude Code 或其他实现者很容易按文档把顺序改回旧逻辑。

建议 Claude Code 更新 `doc/business/运行时参数管理.md`：

1. 明确 `PUT /api/runtime-settings` 的事务语义：
   
   - JSON 解析失败：直接返回失败，不 publish，不 set。
   - `RuntimeSettingsService::validate()` 失败：直接返回失败，不 publish，不 set。
   - `SystemBus::publish()` 失败：返回失败，不 set。
   - publish 成功后再 `RuntimeSettingsService::set()` 提交当前内存参数。

2. 在数据流里区分：
   
   - `validate()` 是业务校验；
   - `set()` 是成功发布后的本地快照提交；
   - `DphPublisher` 不直接读取 `RuntimeSettingsService`，只响应 bridge 传来的事件。

3. 避免继续写成 `RuntimeSettingsService -> 事件`，更准确的是：

```text
WebApiController
  → RuntimeSettingsService.validate()
  → SystemBus publish
  → RuntimeSettingsService.set()
```

## P2：建议补一个 RuntimeSettings 顺序测试，防止以后回退

这轮实现本身没看到阻断问题，但当前链路的关键价值在顺序。建议补一个最小测试，专门防止后续重构时回到“先 set 再 publish”：

1. `validate()` 失败时：
   
   - API 返回失败；
   - 不 publish；
   - service 当前 settings 不变。

2. `publish()` 失败时：
   
   - API 返回失败；
   - service 当前 settings 不变。

3. 成功时：
   
   - 先通过 service 校验；
   - publish 成功；
   - service settings 更新；
   - DPH bridge 能收到并调用 `applyRuntimeSettings()`。

如果现在测试基础设施还不方便 mock `SystemBus`，至少可以先把这条写进 `doc/test` 或后续测试计划里。这个顺序是 Milestone 2 的核心契约之一，值得固定住。

## Milestone 2 收口判断

Milestone 2 后端现在已经具备这几个稳定边界：

- UI 不直接接触 `DataPackageHeader`。
- 可写运行时参数收敛为 `RadarRuntimeSettings` DTO。
- DPH 只在 `DphPublisher` 内部映射协议字段。
- `sceneNumber` / `sendCount` 是只读状态，不参与 settings 写入。
- 工作/待机状态仍归 `RadarWorkModeController`。
- 参数变更通过 `SystemBus` 事件桥接，而不是让 `DphPublisher` 直接依赖 Web/API/Service。

因此我建议：  
**文档同步 + 最小测试计划补上后，Milestone 2 后端可以暂停继续打磨，下一步可以进入 Milestone 3：`TrackDisplayManager / 固定航迹显示协议`。**
