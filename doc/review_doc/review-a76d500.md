# Review a76d500

提交：`a76d500 review-043ecf0修改完成`

## 总体结论

这版把上一轮指出的几个点基本都补上了：

- `parseIntField()` 已经拒绝小数输入。
- `m_runtimeSettingsSubId` 已改为 `SubscriptionId`。
- RuntimeSettings bridge 增加了 `m_runtimeSettingsSubId == 0` 防重复订阅保护。
- `PUT /api/runtime-settings` 从“先 set 再 publish”改成了“先 publish 再 set”，避免了事件发布失败时 service 已经半提交的问题。

方向是对的，Milestone 2 又往前走了一步。  
不过现在仍有一个需要收口的架构细节：**WebApiController 和 RuntimeSettingsService 各自维护了一份合法性判断，导致 publish 和 commit 之间还存在理论分叉。**

## P1：RuntimeSettings 校验逻辑分散，publish 后 set 仍可能失败

位置：

- `core/web/WebApiController.cpp:233-260`
- `core/business/RuntimeSettingsService.cpp:11-23`

当前逻辑是：

```cpp
// WebApiController 里先做 0..255 校验
if (d > 255) return false;

// 然后 publish
if (!SystemBus::getInstance().publish(std::move(packet)))
    return { ok:false, ... };

// 最后 RuntimeSettingsService::set() 再做一次 0..255 校验并提交
if (!m_runtimeSettings->set(s, &err))
    return { ok:false, ... };
```

这已经比上一版好很多，但还有一个架构隐患：  
`publish()` 会同步触发 SystemBus 订阅回调，`AppController` 的 bridge 会收到参数并 queued invoke 到 `DphPublisher::applyRuntimeSettings()`。也就是说，一旦 `publish()` 成功，DPH 发布侧已经进入“准备应用新参数”的路径。

如果未来 `RuntimeSettingsService::set()` 增加新规则，而 WebApiController 忘记同步更新，例如：

- 某个字段范围从 `0..255` 收紧；
- 两个字段之间新增组合约束；
- 某个工作状态下禁止修改运行时参数；

那么就会出现：

```text
WebApiController 认为合法
  -> publish 成功
  -> DphPublisher 收到新参数
  -> RuntimeSettingsService::set() 拒绝
  -> API 返回失败
  -> DPH 实际侧可能已经应用新值
```

这就是新的半提交路径。

建议 Claude Code 不要继续在 Web 层复制业务校验，而是把校验权收回到 `RuntimeSettingsService`：

```cpp
class RuntimeSettingsService {
public:
    bool validate(const RadarRuntimeSettings& settings, QString* errorOut = nullptr) const;
    bool set(const RadarRuntimeSettings& settings, QString* errorOut = nullptr);
};
```

`set()` 内部复用 `validate()`：

```cpp
bool RuntimeSettingsService::set(const RadarRuntimeSettings& settings, QString* errorOut)
{
    if (!validate(settings, errorOut)) {
        return false;
    }
    m_settings = settings;
    return true;
}
```

WebApiController 的顺序改成：

```text
1. 只负责 JSON 类型解析：字段存在、数字、整数、非负、可转 uint32
2. 调 RuntimeSettingsService::validate(s, &err)
3. validate 通过后 publish
4. publish 成功后 set
```

这样可以保证 publish 之前已经通过 service 的最终业务规则，后续 service 规则变化也不会让 API 和 service 分叉。

更进一步，如果想把语义定得更死，可以增加一个专门方法：

```cpp
bool RuntimeSettingsService::commitValidated(const RadarRuntimeSettings& settings);
```

但当前阶段 `validate()` + `set()` 复用同一套规则已经够用。

## P2：Web 层不建议硬编码 255 这个业务范围

位置：`core/web/WebApiController.cpp:240`

当前：

```cpp
if (d > 255) { *err = key + QStringLiteral(" 超出范围 0..255"); return false; }
```

`255` 是运行时参数业务规则，不是 HTTP JSON 解析规则。  
Web 层可以判断 JSON 是否是“非负整数且能放进 uint32”，但字段范围最好交给 `RuntimeSettingsService`，否则后续范围变更时会出现两处规则要同步。

建议 Web 层只做：

- 字段存在；
- JSON number；
- 整数；
- 非负；
- 不超过 `uint32_t` 上限。

`0..255` 由 `RuntimeSettingsService::validate()` 返回错误。

## P2：需要处理订阅失败时的日志或降级

位置：`core/AppController.cpp:633-643`

当前：

```cpp
m_runtimeSettingsSubId = SystemBus::getInstance().subscribeBySource(...);
qDebug() << "[AppController] RuntimeSettings → DphPublisher bridge 已就绪";
```

`subscribeBySource()` 在 bus shutdown 或 callback 非法时会返回 `0`。虽然当前 callback 有效，正常启动路径也不会在 shutdown 后订阅，但这里仍建议加一层判断：

```cpp
m_runtimeSettingsSubId = SystemBus::getInstance().subscribeBySource(...);
if (m_runtimeSettingsSubId == 0) {
    qWarning() << "[AppController] RuntimeSettings → DphPublisher bridge 订阅失败";
} else {
    qDebug() << "[AppController] RuntimeSettings → DphPublisher bridge 已就绪";
}
```

这不是当前功能 bug，但能让 RuntimeSettings 链路故障更容易定位。否则如果订阅失败，API 仍然可能返回成功，但 DPH 永远收不到运行时参数事件。

## Milestone 2 当前状态

可以认为 Milestone 2 的主链路已经基本成型：

```text
Web API DTO
  -> RuntimeSettingsService 校验/保存当前参数
  -> SystemBus 运行时参数事件
  -> AppController bridge
  -> DphPublisher.applyRuntimeSettings()
  -> DPH 周期发送
```

这轮之后建议 Claude Code 再做一个小收口：

1. `RuntimeSettingsService` 增加统一 `validate()`。
2. Web 层移除 `255` 业务范围硬编码。
3. bridge 订阅失败时输出 warning。
4. 补一个最小测试：`validate` 失败时不会 publish，`publish` 失败时不会 commit，成功时 settings 与 DPH 应用值一致。

完成这些后，Milestone 2 后端基座就可以暂时收住，下一步可以选择：

- 做前端运行时参数面板；
- 或者进入 Milestone 3：`TrackDisplayManager / 固定航迹显示协议`。

