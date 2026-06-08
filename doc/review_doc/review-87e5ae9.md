# Review for 87e5ae9

Full commit: `87e5ae9ec8cf3a1d28078bd34118f22f77c0ee1f`

Commit message: `review-15b5ecd修改完成`

## Findings

### P1 - `resolveControllable` 没有捕获 `radarCpuDeviceIds`，C++ 编译会失败

- File: `core/web/WebApiController.cpp`
- Lines: 67-85

这版把 `radar_cpu` 控制映射拆成了 `radarCpuDeviceIds()` / `resolveControllable()` / `shouldControlRadarCpu()`，方向是对的，已经解决了上一轮“accepted 但不执行”的设计问题。

但 `resolveControllable` 的捕获列表是：

```cpp
auto resolveControllable = [this](const QJsonArray& ids) -> QJsonArray {
    ...
    auto controllable = radarCpuDeviceIds();
```

`radarCpuDeviceIds` 是 `registerRestRoutes()` 内部的局部 lambda，不是成员函数。`resolveControllable` 没有捕获它，编译器会报类似：

```text
'radarCpuDeviceIds' is not captured
```

建议 Claude Code 修改：

```cpp
auto resolveControllable = [radarCpuDeviceIds](const QJsonArray& ids) -> QJsonArray {
```

或者更好一点，把这几个局部 lambda 收敛成私有 helper，避免 REST handler 内部嵌套 lambda 越来越难维护。

### P2 - start/stop 共享同一个 `static lastCmdMs`，可能误伤“快速反向操作”

- File: `core/web/WebApiController.cpp`
- Lines: 100-126

这版用 `makeHandler(bool work)` 合并了 start/stop handler，减少重复是好的。但 handler 内部的：

```cpp
static qint64 lastCmdMs = 0;
```

位于返回的 lambda 里。因为 start 和 stop 都来自同一处 lambda 表达式实例，实际很可能共享同一个 `lastCmdMs`。这意味着用户点击 start 后 500ms 内立即 stop，会被 stop 的节流挡掉。

这不一定是 bug，取决于业务语义：如果系统希望任何启停命令 500ms 内只能有一个，那可以接受；如果只想防重复 start 或重复 stop，那么现在会误伤快速反向操作。

建议 Claude Code 明确契约：

1. 如果全局节流是设计目标，请在注释和 API 契约中说明“start/stop 共享命令节流”。
2. 如果不是，使用 start/stop 各自的节流时间戳。
3. 前端对 `throttled:true` 应显示“操作太频繁”，不要只显示“请求被拒绝”。

### P2 - 控制映射逻辑仍散落在局部 lambda 和状态 helper 中，后续容易漂移

- File: `core/web/WebApiController.cpp`
- Lines: 67-96
- File: `core/web/WebApiController.cpp`
- Lines: 307-323

现在控制侧用 `radarCpuDeviceIds()` 判断哪些业务设备映射到 `radar_cpu`，状态侧在 `buildDeviceStatusArray()` 中再次写了一遍：

```cpp
d.id == "radar_cpu" || d.endpointId == "radar_cpu"
```

短期功能能跑，但映射规则已经出现两处。后续如果映射规则变成 `endpointId/controllerId/deviceKind/status` 组合条件，控制 accepted 和状态 merge 很容易再次漂移。

建议 Claude Code 下一步把映射规则抽成一个私有 helper，例如：

```cpp
bool mapsToRadarCpuController(const DeviceInfo& d) const;
QSet<QString> radarCpuDeviceIds() const;
```

让 start/stop accepted、实际 dispatch、status merge 共用同一个来源。

## Fixed Since Previous Review

- 前端 `safeJsonParse(res)` 已补 `await`。
- 前端新增 `CommandResponse` 类型，启停响应结构更明确。
- `safeJsonParse()` 已能在非 2xx 或非 JSON 时返回结构化错误，而不是直接丢失响应。
- 后端已把 accepted 业务设备 id 和实际 `radar_cpu` controller dispatch 分开：只要 accepted 中存在映射到 `radar_cpu` 的设备，就执行一次 `requestWork()` / `requestStandby()`。
- 部分成功语义继续保留，不再把 accepted 设备整体 rollback。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 82%，但当前有一个 P1 编译阻塞。

从架构方向看，这版已经把核心链路基本串起来了：

```text
业务 deviceId
  -> endpointId == radar_cpu
  -> acceptedDeviceIds 返回业务 id
  -> shouldControlRadarCpu()
  -> RadarWorkModeController requestWork/requestStandby
  -> buildDeviceStatusArray() merge runtime state
```

只要修掉 lambda 捕获编译问题，这条最小链路就可以算基本站住。下一步才适合考虑把映射 helper 抽出来，以及进入 RuntimeSettings。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

建议等本轮 P1 修完、确认 start/stop 能编译并手动跑通后，再开始。

### Milestone 3 - Track Display Data Management

进度：未开始。

仍建议排在 RuntimeSettings 或设备控制闭环之后。

### Milestone 4 - Task Backend Domain

进度：未开始。

暂时可接受。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Suggested Next Step

下一轮优先修：

```cpp
auto resolveControllable = [radarCpuDeviceIds](...)
```

然后让 Claude Code 做一次最小手动验证：

1. 台账设备 `id=radar_001, endpointId=radar_cpu`；
2. 前端勾选 `radar_001` 点击开始；
3. 后端 response `acceptedDeviceIds=["radar_001"]`；
4. `RadarWorkModeController::requestWork()` 确实被调用；
5. `/api/status` 中 `radar_001` 显示 Working/Starting。

这条验证通过后，Milestone 1 就可以进入收尾优化，而不是继续补假成功。

## Verification

- 本轮为源码审查。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake；C++ 编译问题基于 lambda 捕获规则检查。
