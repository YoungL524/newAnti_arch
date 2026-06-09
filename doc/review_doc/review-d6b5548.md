# Review: d6b5548

Commit: `d6b5548` (`review-a2a9db6修改完成`)

## 总体判断

这版开始实现 RuntimeSettings 最小链路，方向是对的：

- 新增 `RuntimeSettingsService` 内存快照与 0..255 校验。
- `WebApiController` 增加 `GET/PUT /api/runtime-settings`。
- `PUT` 后通过 SystemBus 发布 `DphParamsChanged`。
- `AppController` 增加 bridge，收到事件后 queued invoke `DphPublisher::applyRuntimeSettings()`。
- `DphPublisher::applyRuntimeSettings()` 只更新 `F_UI_Freq_Ctrl` / `F_UI_Wave_Ctrl`，没有重置 sceneNumber/sendCount/timer/route。

但当前实现还有几个必须修的点，其中一个是编译级问题，另外几个是 API 契约和生命周期问题。

## P0 - `RuntimeSettingsService.h` 使用 `QString` 但没有声明或 include

文件：`core/business/RuntimeSettingsService.h:15`

当前：

```cpp
bool set(const RadarRuntimeSettings& settings, QString* errorOut = nullptr);
```

但头文件只 include 了：

```cpp
#include "RuntimeSettingsTypes.h"
```

没有 `#include <QString>`，也没有 `class QString;` 前置声明。`QString*` 可以用前置声明，但现在没有声明，编译会失败。

### 建议改法

二选一：

```cpp
#include <QString>
```

或：

```cpp
class QString;
```

这里用 `QString*`，前置声明即可；如果后续返回 QString 值，再 include。

## P1 - PUT 缺字段/非数字会被悄悄当成 0，不符合全量替换契约

文件：`core/web/WebApiController.cpp:220`

当前：

```cpp
s.frequencyControl = static_cast<uint32_t>(body["frequencyControl"].toInt());
s.waveControl = static_cast<uint32_t>(body["waveControl"].toInt());
```

问题：

- 缺少字段时 `toInt()` 返回 0。
- 字段类型是字符串/对象/数组时也可能变成 0。
- 负数 `-1` 经 `uint32_t` 转换会变成巨大值，虽然 service 会挡住，但错误信息会变成“超出范围”而不是类型错误。

文档已经明确 PUT 是全量替换，不允许局部字段更新。因此缺字段必须返回错误，不能静默补 0。

### 建议改法

在 WebApiController 做 JSON 层校验：

```cpp
auto parseUInt = [](const QJsonObject& body, const QString& key, uint32_t* out, QString* err) {
    if (!body.contains(key)) { *err = key + " 不能为空"; return false; }
    const auto v = body.value(key);
    if (!v.isDouble()) { *err = key + " 必须是数字"; return false; }
    const int n = v.toInt(-1);
    if (n < 0) { *err = key + " 必须是非负整数"; return false; }
    *out = static_cast<uint32_t>(n);
    return true;
};
```

然后再交给 `RuntimeSettingsService::set()` 做业务范围校验。

## P1 - GET/PUT 响应没有返回文档承诺的 `DphStatus`

文件：`core/web/WebApiController.cpp:204`

文档约定：

```json
{
  "ok": true,
  "settings": { ... },
  "status": {
    "currentSceneNumber": 12,
    "sendCount": 1200
  }
}
```

当前 GET 返回：

```cpp
r["status"] = QJsonObject();
```

PUT 成功响应则没有 `status` 字段。

这会导致前后端契约又开始漂。当前 `DphPublisher` 已经有：

```cpp
uint64_t sendCount() const
uint32_t sceneNumber() const
```

### 建议改法

不要让 `WebApiController` 直接依赖 `DphPublisher`。可以由 `AppController` 传一个 status provider：

```cpp
using RuntimeStatusProvider = std::function<DphStatus()>;
```

或更简单：

```cpp
std::function<QJsonObject()> runtimeStatusProvider
```

由 AppController 从 `m_dphPublisher` 读取 `sceneNumber()` / `sendCount()` 后组装。GET 和 PUT 成功响应都返回同一套 `status`。

## P1 - SystemBus publish 失败仍返回 ok=true，可能造成 API 与 DphPublisher 状态不一致

文件：`core/web/WebApiController.cpp:234`

当前：

```cpp
m_runtimeSettings->set(s, &error);
packDataToPayload(packet, s);
SystemBus::getInstance().publish(std::move(packet));
return { ok: true, settings: ... };
```

如果 publish 失败，`RuntimeSettingsService` 已经更新，但 `DphPublisher` 不会收到事件，API 却返回成功。这样 service 快照和实际 DPH 缓存会分裂。

### 建议改法

最小修法：

```cpp
const bool published = SystemBus::getInstance().publish(std::move(packet));
if (!published) {
    return QJsonObject{{"ok", false}, {"error", "运行时参数事件发布失败"}};
}
```

更严谨的做法是先校验，再发布成功后 commit settings；但当前系统 bus 一般 running，最小修法可以先接受。至少不能静默成功。

## P1 - RuntimeSettings bridge 订阅没有保存 subscriptionId，无法 unsubscribe

文件：`core/AppController.cpp:623`

当前：

```cpp
SystemBus::getInstance().subscribeBySource(SOURCE_RUNTIME_SETTINGS, [this](...) { ... });
```

返回的 subscription id 没保存。`AppController::stop()` 也没有 unsubscribe。

这个回调捕获 `this`，如果 WebService stop 但 SystemBus 仍运行，或后续生命周期调整，就有悬空回调风险。现有 `WebApiController` 已经保存 `m_trackSubId` 并在 shutdown 中 unsubscribe，RuntimeSettings bridge 应保持同样习惯。

### 建议改法

在 `AppController.h` 增加：

```cpp
SubscriptionId m_runtimeSettingsSubId = 0;
```

订阅时保存：

```cpp
m_runtimeSettingsSubId = SystemBus::getInstance().subscribeBySource(...);
```

`stop()` 中在 shutdown bus 前取消：

```cpp
if (m_runtimeSettingsSubId != 0) {
    SystemBus::getInstance().unsubscribe(m_runtimeSettingsSubId);
    m_runtimeSettingsSubId = 0;
}
```

并检查订阅失败时的日志。

## P2 - bridge 应使用 `unpackDataFromPayload()`，不要手写 payloadSize + memcpy

文件：`core/AppController.cpp:625`

当前：

```cpp
if (pkt->payloadSize < sizeof(RadarRuntimeSettings)) return;
RadarRuntimeSettings s;
memcpy(&s, pkt->payload.rawData, sizeof(RadarRuntimeSettings));
```

这绕过了 `GenericPackets.h` 已有的类型检查工具。当前 payloadType 已检查，风险不大，但既然已经有通用方法，建议统一：

```cpp
RadarRuntimeSettings s;
if (!unpackDataFromPayload(*pkt, s)) return;
```

这样 payload size 必须精确匹配结构体大小，未来更稳。

## P2 - `RuntimeSettingsService` 当前不是线程安全的，文档或实现要明确调用线程

文件：`core/business/RuntimeSettingsService.cpp`

当前 service 是普通成员：

```cpp
RadarRuntimeSettings m_settings;
```

GET/PUT HTTP handler 和 AppController 都在 Qt 主线程时没问题。但 SystemBus callback 在 bus worker 线程，虽然当前只在 WebApiController 的 PUT 中 set、GET 中 get，bridge 不读 service，所以暂时可接受。

建议在 service 注释里明确：

```text
当前由 WebApiController 所在线程访问；SystemBus 回调不得直接访问本 service。
```

如果后续多线程访问，再加 mutex。

## 已确认修复项

- `core/main.cpp` 尾随空格已清理。
- `RuntimeSettingsService` 已新增。
- `DphPublisher::applyRuntimeSettings()` 只更新频率/波形字段。
- Web API 路由已接入。
- AppController bridge 已使用 queued invoke，不在 bus worker 线程直接改 QObject 状态。

## 推荐 Claude 下一步

1. 修复 `RuntimeSettingsService.h` 的 `QString` 声明/include。
2. PUT 严格校验字段存在、类型、非负整数，再交给 service 校验范围。
3. GET 和 PUT 成功响应补 `status.currentSceneNumber/sendCount`。
4. SystemBus publish 失败时不要返回 ok=true。
5. RuntimeSettings bridge 保存 subscription id，并在 `AppController::stop()` 中 unsubscribe。
6. bridge 使用 `unpackDataFromPayload()` 替代手写 memcpy。
7. 至少补一个最小验证：PUT 后 `DphPublisher` 的映射字段更新，sceneNumber/sendCount 不被重置。

## 架构结论

Milestone 2 已经从文档进入实现，方向没偏。但这一版还不能算完成：当前有编译级问题，也有 API 契约不一致。

先把 P0/P1 修掉，再看是否需要扩展 UI。当前阶段不要扩字段，也不要把 RuntimeSettings 落库，先把最小链路跑稳。
