# Review for 15b5ecd

Full commit: `15b5ecd7ee3568b2293231c110c8b093adb77280`

Commit message: `review-8d02505修改完成`

## Findings

### P1 - `safeJsonParse()` 调用缺少 `await`，启停响应判断会失真并导致 typecheck 风险

- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 1-7
- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 123-131

这版新增了 `safeJsonParse()`，方向是对的，但调用处写成了：

```ts
const data = safeJsonParse(res)
if (!data) { rollback('后端返回异常'); return }
if (!data.ok) { rollback(data.error || '请求被拒绝'); return }
```

`safeJsonParse()` 是 async function，返回的是 `Promise<any | null>`。这里没有 `await`，所以：

- `data` 实际是 Promise，不是后端 JSON；
- `!data` 永远是 false；
- `data.ok` 是 `undefined`，`!data.ok` 为 true；
- 最终会把所有启停请求都当成“请求被拒绝”回滚；
- 在 TypeScript 严格检查下，也很可能报 `Property 'ok' does not exist on type 'Promise<...>'`。

建议 Claude Code 修改：

```ts
const data = await safeJsonParse(res)
```

同时建议给启停响应定义明确类型，例如：

```ts
interface CommandResponse {
  ok: boolean
  error?: string
  throttled?: boolean
  acceptedDeviceIds?: string[]
  rejectedDeviceIds?: string[]
}
```

这样 TypeScript 会更早抓到少 `await` 这类问题。

### P1 - `endpointId == "radar_cpu"` 的业务设备会被 accepted，但不会真正执行命令

- File: `core/web/WebApiController.cpp`
- Lines: 64-83
- File: `core/web/WebApiController.cpp`
- Lines: 91-102
- File: `core/web/WebApiController.cpp`
- Lines: 112-123

这版把 `resolveControllable()` 扩展为：

```cpp
if (d.id == "radar_cpu" || d.endpointId == "radar_cpu")
    controllableIds.insert(d.id);
```

这一步符合上一轮建议：业务设备可以通过 `endpointId` 映射到当前 `RadarWorkModeController`。

但执行命令时仍然只判断 accepted id 本身是否等于 `radar_cpu`：

```cpp
for (const auto& v : accepted)
    if (v.toString() == QStringLiteral("radar_cpu")) m_wmc->requestWork();
```

所以如果台账里有设备：

```text
id = radar_001
endpointId = radar_cpu
```

用户勾选 `radar_001` 后，后端会返回：

```json
{
  "ok": true,
  "acceptedDeviceIds": ["radar_001"],
  "rejectedDeviceIds": []
}
```

但 `requestWork()` 不会被调用，因为 accepted id 不是 `radar_cpu`。这是比上一版更危险的假成功：前后端都认为设备 accepted，但真实控制没有发生。

建议 Claude Code 修改：

1. `resolveControllable()` 不要只返回 accepted id，应该返回“业务设备 id + controller id”的映射结果。
2. 最小实现可以拆成两个集合：
   - `acceptedDeviceIds`：返回给前端的业务 id；
   - `shouldControlRadarCpu`：只要任一 accepted 设备映射到 `radar_cpu` 就执行一次 `m_wmc->requestWork()` / `requestStandby()`。
3. start/stop 和 `buildDeviceStatusArray()` 必须共用同一套映射规则，避免 accepted、执行、状态三者不一致。
4. 如果多个业务设备都映射到同一个 controller，命令只执行一次，但 response 仍返回所有 accepted 业务设备 id。

### P2 - `safeJsonParse()` 把 HTTP 错误体丢掉，用户只能看到“后端返回异常”

- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 3-6
- File: `ui/src/hooks/useCoreApi.tsx`
- Lines: 123-126

当前 `safeJsonParse()` 在 `!res.ok` 时直接返回 null：

```ts
if (!res.ok) return null
```

如果后端未来用 HTTP 400/409/429 返回结构化错误，例如：

```json
{ "ok": false, "error": "不支持的设备 id" }
```

前端会丢掉具体错误，只显示“后端返回异常”。这不是当前最大问题，但会削弱 command feedback 的可用性。

建议改成：

1. 尝试解析 JSON，即使 `res.ok === false`。
2. 如果 JSON 有 `error`，优先显示后端错误。
3. 只有完全无法解析时，再显示 `HTTP ${res.status}` 或“后端返回异常”。

### P2 - 状态 merge 可能让多个业务设备同时显示同一个 `radar_cpu` 运行状态

- File: `core/web/WebApiController.cpp`
- Lines: 307-323

`buildDeviceStatusArray()` 现在对所有满足以下条件的台账设备都 merge `RadarWorkModeController` 状态：

```cpp
d.id == "radar_cpu" || d.endpointId == "radar_cpu"
```

如果台账中误配置或未来确实存在多个设备绑定同一个 `endpointId=radar_cpu`，它们会全部显示同一份 `Working/Standby` 状态。短期可以接受，但架构上需要明确约束：

- 一个 endpoint/controller 是否允许绑定多个业务设备；
- 如果允许，状态是 endpoint 状态还是设备状态；
- UI 勾选多个映射到同一 controller 的设备时，acceptedDeviceIds 如何解释。

建议短期在 `DeviceRegistry` 创建/更新时校验 `endpointId` 唯一，至少对可控 radar endpoint 做唯一约束。否则后续多设备控制会出现“多个设备共享同一状态”的歧义。

## Fixed Since Previous Review

- 前端部分成功语义已调整：`accepted > 0 && rejected > 0` 时不再整体 rollback，而是保留命令状态并提示部分设备未执行。
- 未勾选设备提示会清空 `commandError`，避免两个提示层重叠。
- 后端 `resolveControllable()` 已开始使用 `DeviceRegistry.endpointId == "radar_cpu"` 作为最小映射规则。
- `buildDeviceStatusArray()` 也开始用 `endpointId == "radar_cpu"` merge runtime state，方向上更接近“台账设备 + 运行状态”视图。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 78%，但本轮存在 P1 阻塞。

这版在架构方向上是对的：`endpointId` 开始进入控制映射和状态 merge，不再只停留在注释层。

但因为执行命令仍只认 accepted id 是否等于 `radar_cpu`，当前链路还没有真正闭环：

```text
业务 deviceId
  -> endpointId == radar_cpu
  -> acceptedDeviceIds 返回业务 id
  -> controller 执行命令   当前这里断了
  -> status merge runtime
```

下一轮必须优先修这个断点，否则前端会看到“accepted 成功”，实际设备不动。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。

仍不建议现在开。设备控制映射还没完全稳定，RuntimeSettings 现在接入会绑定到不可靠的设备语义上。

### Milestone 3 - Track Display Data Management

进度：未开始。

保持原判断：等 Milestone 1 控制链路稳定后，再定义 `TrackDisplaySnapshot`。

### Milestone 4 - Task Backend Domain

进度：未开始。

暂时可接受。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Suggested Next Step

下一轮建议只做两个修复，不要发散：

1. 前端补 `await safeJsonParse(res)`，并给启停响应加类型。
2. 后端把“accepted 业务设备 id”和“实际 controller 执行目标”分开，保证 `endpointId == radar_cpu` 的设备 accepted 后一定会触发 `RadarWorkModeController`。

这两点修完，Milestone 1 才能继续往 85% 以上推进。

## Verification

- 本轮为源码审查。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
