# Review 043ecf0

提交：`043ecf0 review-d6b5548修改完成-bug修复`

## 总体结论

这版 RuntimeSettings / DPH 链路已经进入可继续推进的状态：  

- `DPH/DataPackageHeader` 仍然保持为外部协议结构体，没有被直接提升成 UI 模型。
- `RuntimeSettingsService` 作为后端内存态运行时参数服务，暂不入库，符合当前阶段。
- `frequencyControl` / `waveControl` 已经通过 `PUT /api/runtime-settings` 写入，并通过 `SystemBus` 事件桥接到 `DphPublisher::applyRuntimeSettings()`。
- `sceneNumber` / `sendCount` 作为只读运行状态返回给前端，没有混进可写 settings，方向正确。
- `workMode` 仍然没有被 RuntimeSettings 接管，继续归 `RadarWorkModeController`，边界正确。

这轮没有看到必须阻断继续推进的大问题，但有一个一致性问题建议优先修掉，否则后续前端调参时会出现“接口返回失败但服务状态已经变了”的隐性分叉。

## P1：运行时参数先落服务、后发事件，失败时会造成内部状态分叉

位置：`core/web/WebApiController.cpp:246-257`

当前逻辑是：

```cpp
if (!m_runtimeSettings->set(s, &err))
    return ...

packDataToPayload(packet, s);
if (!SystemBus::getInstance().publish(std::move(packet)))
    return { ok:false, error:"运行时参数事件发布失败" };
```

问题在于：`RuntimeSettingsService::set()` 已经把新参数写进服务内存了，但如果 `SystemBus::publish()` 返回失败，API 又返回 `ok:false`。这会导致：

- `GET /api/runtime-settings` 读到的是新值；
- `DphPublisher` 没收到事件，继续按旧值发 DPH；
- 前端看到本次写入失败，但刷新后又可能看到后端 settings 已经变了；
- 后续排查时很难判断 RuntimeSettingsService 和 DphPublisher 谁才是当前真实状态。

建议 Claude Code 明确这里的事务语义，推荐采用其中一种：

1. **发布成功后再提交服务状态**  
   先做纯校验，再构造并发布事件，`publish()` 成功后再 `m_runtimeSettings->set(s)`。  
   适合把“DPH 实际应用成功”作为写入成功标准。

2. **提交失败可回滚**  
   `set()` 前保存 old settings，`publish()` 失败则恢复 old settings。  
   这种实现简单，但要注意回滚本身也应走同一套校验或内部私有接口。

3. **明确 RuntimeSettingsService 是唯一 source of truth**  
   如果选择服务先变更，那么 `publish()` 失败不应简单返回失败后结束，必须有 DphPublisher 主动同步、重试、或从 service 拉取的补偿路径。  
   但这会把当前“前端改了才上总线，后端收到就应用”的设计复杂化，不建议现在走这条。

我建议选第 1 种：先 validate，再 publish，再 commit。当前字段少，成本最低，也最符合“接口成功代表 DPH 链路已接收变更”的直觉。

## P2：JSON 数字解析会接受小数，和 uint32 参数契约不一致

位置：`core/web/WebApiController.cpp:232-239`

`parseIntField()` 当前只判断 `v.isDouble()`，然后 `v.toInt(-1)`。Qt JSON 数字统一表现为 double，所以 `1.5` 这种输入会被截断成 `1`，从 API 契约看不应该被接受。

建议：

- 缺字段：保持现在的 `不能为空`。
- 非数字：保持现在的 `必须是数字`。
- 小数：返回 `必须是整数`。
- 负数：返回 `必须是非负整数`。
- 范围：继续交给 `RuntimeSettingsService` 做 `0..255` 校验。

可用思路：

```cpp
const double d = v.toDouble();
if (!std::isfinite(d) || std::floor(d) != d) {
    *err = key + QStringLiteral(" 必须是整数");
    return false;
}
if (d < 0 || d > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    *err = key + QStringLiteral(" 必须是非负整数");
    return false;
}
*out = static_cast<uint32_t>(d);
```

注意补 `<cmath>` / `<limits>`。

## P2：订阅 id 建议使用项目内的 SubscriptionId 类型

位置：`core/AppController.h:260`

当前是：

```cpp
uint32_t m_runtimeSettingsSubId = 0;
```

项目里 `SystemBus` 已经有订阅 id 的语义类型，建议改成：

```cpp
SubscriptionId m_runtimeSettingsSubId = 0;
```

这不是功能 bug，但能减少后续读代码时的概念漂移：这里保存的是 bus subscription，而不是普通业务 id。

## P2：运行时参数桥接建议防止重复订阅

位置：`core/AppController.cpp:634-642`

当前 `startWebService()` 中只要 `m_runtimeSettings && m_dphPublisher` 就订阅 RuntimeSettings 事件。正常启动路径可能只调用一次，所以这不是当前阻断问题。

但为了把基础框架做稳，建议在订阅前加保护：

```cpp
if (m_runtimeSettingsSubId == 0 && m_runtimeSettings && m_dphPublisher) {
    ...
}
```

否则未来如果支持 Web 服务重启、端口切换、运行时重新初始化，就可能出现重复订阅，同一个参数事件被应用多次。虽然当前 `applyRuntimeSettings()` 是幂等赋值，问题不明显，但这类生命周期问题越早收口越好。

## 可补的最小测试

建议 Claude Code 这轮不用铺大测试，只补 RuntimeSettings 的几个核心用例即可：

1. `PUT /api/runtime-settings` 成功后：
   - 返回 `ok:true`；
   - 返回 settings 为后端确认值；
   - DphPublisher 收到并应用 `frequencyControl` / `waveControl`；
   - `sceneNumber` / `sendCount` 不被重置。

2. 非法输入：
   - 缺字段；
   - 字符串；
   - 小数；
   - 负数；
   - 超出 `0..255`。

3. 事件发布失败：
   - API 返回失败；
   - RuntimeSettingsService 不应留下已经提交的新值，或者必须有明确同步补偿机制。

## Milestone 2 进度复盘

Milestone 2 的核心不是“把 DPH 暴露给 UI”，而是建立一条稳定的运行时参数链路：

```text
Web UI 参数表单
  -> WebApiController API DTO
  -> RuntimeSettingsService 内存态参数
  -> SystemBus 参数变更事件
  -> AppController bridge
  -> DphPublisher.applyRuntimeSettings()
  -> 50ms DPH 周期发送
```

当前代码已经基本打通了这条链路，剩余重点是把“写入成功”的语义定死：

- API 成功，必须代表后端确认参数合法，并且 DPH 发布侧已经接收本次变更。
- API 失败，不能留下半提交状态。
- UI 只接触 RuntimeSettings DTO，不直接接触 `DataPackageHeader`。
- `DphPublisher` 只负责把当前缓存参数周期性写进外发协议，不主动从 UI 或 Web 层读取状态。

这块修完后，可以认为 Milestone 2 的第一阶段闭环完成。下一步可以开始做前端运行时参数面板，或者继续推进 Milestone 3 的 `TrackDisplayManager / 固定航迹显示协议`。

