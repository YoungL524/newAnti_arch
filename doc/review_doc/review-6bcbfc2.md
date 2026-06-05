# Review for 6bcbfc2

Full commit: `6bcbfc24dc70107e6c43307a6b75eb2ba3f49f50`

## Findings

### P2 - `WebService::stop()` does not clear `m_idleTimers`

- File: `core/web/WebService.cpp`
- Lines: 81-106
- File: `core/web/WebService.h`
- Line: 75

`WebService::stop()` disconnects and schedules HTTP sockets for deletion through `m_buffers`, then clears `m_buffers`, but it never clears `m_idleTimers`.

The timers are parented to the sockets, so Qt will eventually delete them when the sockets are deleted. The problem is the hash itself keeps stale socket keys and timer pointers after `stop()`. If the same `WebService` object is stopped and then started again, `m_idleTimers` can retain stale entries unrelated to current connections. It also makes the class state inconsistent: `m_buffers` says there are no HTTP connections, but `m_idleTimers` still says there are.

Suggested fix for Claude Code:

1. In `WebService::stop()`, stop and clear all idle timers before or while deleting sockets:

```cpp
for (auto it = m_idleTimers.begin(); it != m_idleTimers.end(); ++it) {
    if (it.value()) {
        it.value()->stop();
    }
}
m_idleTimers.clear();
```

2. Keep `onHttpDisconnected()` as-is for normal per-socket cleanup.
3. If a stop/start lifecycle is expected, add a small test or manual check that both `m_buffers` and `m_idleTimers` are empty after `stop()`.

## Fixed Since Previous Review

- `AppHealth` and `WebServiceResult` were moved into `core/AppTypes.h`, avoiding the incomplete-type compile failure in `WebApiController`.
- `WebApiController.cpp` now includes `<QDebug>`, fixing the `qWarning()` / `qDebug()` incomplete type error.
- `WebApiController` now has a destructor that calls `shutdown()`.
- `AppController::stop()` now shuts down `m_webApi` before the `m_running` early return, so Web callbacks are cleaned even if the core communication layer was not started.
- HTTP status text now includes `403 Forbidden` and `413 Payload Too Large`.

## Architecture Notes

The fix is architecturally clean. `AppTypes.h` is a good boundary for DTO-style types shared between the composition root and the Web adapter. It avoids making `WebApiController` include or depend on the whole `AppController` class.

The Web API extraction remains a good direction: `AppController` is back to composition/lifecycle, while `WebApiController` owns REST route registration and WS JSON presentation.

## Verification

I did not run local CMake because `cmake` is not available in the current PATH. The compile errors from the previous attached log appear addressed by source inspection.

## Architecture Reset

当前不应该继续被 HTTP parser、timer hash 这类局部问题牵着走。它们要修，但属于 housekeeping。更重要的是：这个项目已经从“雷达通信 demo”进入“上位机 / 管理系统骨架”阶段，接下来要立住业务主干。

我重新读了一遍项目，当前状态大致是：

- `core` 已经有可运行底座：通信端点、协议结构体、SystemBus、数据处理、SQLite、WebService/WebApiController。
- `ui` 已经有管理系统雏形：任务、设备、地图、PPI、状态栏、日志、数据表。
- `test_comm` 能支撑端到端链路：CSV 回放、UDP、workStandby、DPH。
- 文档里已经明确了方法论：业务线应从业务对象和状态变化出发，而不是从 UI 控件或协议字段出发。

主要缺口是：内部业务模型还没有真正成型。现在很多东西仍然是“协议数据直接推 UI”或“前端 localStorage 自己管”，后端还没有形成设备台账、任务台账、运行时快照、航迹生命周期这些管理系统核心对象。

### The Next Frame

建议把后续工作从“小修小补”切成三个层次：

1. **平台底座收口**：通信、HTTP、WS、生命周期、构建测试，保证能稳定跑。
2. **业务域模型立骨架**：设备、任务、运行时参数、航迹、告警，形成后端真实对象和 API 契约。
3. **管理系统体验闭环**：前端不只是展示实时数据，而是围绕任务执行、设备管理、历史回放、告警处置来组织工作流。

## Recommended Roadmap

### Milestone 1 - Device Registry As Source Of Truth

现在设备信息主要在前端内存 / localStorage 里，后端只知道 `endpoints.json` 里的通信端点和 `processor.json` 里的路由。对于管理系统来说，这不够。设备应该成为后端明确管理的业务对象。

目标：

- 后端建立 `DeviceRegistry` 或 `DeviceManager`。
- 统一维护设备 id、名称、厂家/型号、类型、地理位置、阵面参数、关联 endpoint、运行状态。
- 前端设备列表从后端加载，而不是只存在浏览器 localStorage。
- `endpoints.json` 仍然描述通信端点，设备台账描述业务设备，两者通过 `endpointId` 关联。

建议 Claude Code 的第一步：

1. 新增业务文档：`doc/business/设备台账与端点映射.md`。
2. 定义后端 DTO：`DeviceInfo` / `DeviceRuntimeState` / `DeviceEndpointBinding`。
3. 先不急着做复杂数据库迁移，可以从 JSON 或 SQLite 简表开始。
4. 新增 API：
   - `GET /api/devices`
   - `POST /api/devices`
   - `PUT /api/devices/{id}`
   - `DELETE /api/devices/{id}`
5. 前端保留现有 UI，但设备初始数据改为从 `/api/devices` 来。

架构边界：

```text
endpoints.json        -> 通信配置
DeviceRegistry        -> 业务设备台账
RadarWorkModeController -> 设备运行状态机
WebApiController      -> API/WS 表现层
```

这一步完成后，系统才从“一个雷达通路”变成“多设备管理平台”。

### Milestone 2 - Runtime Settings Snapshot

文档里已经提到 `RuntimeSettings`。这条线很重要，因为 DPH 50ms 定时发送不应该长期直接拼固定默认值，也不应该未来被 UI 零散字段牵着跑。

目标：

- 建立 `RuntimeSettings` 或 `RadarRuntimeConfig`，表示当前运行参数快照。
- DPH Publisher 每 50ms 读取快照生成 `DataPackageHeader`。
- UI 修改参数时写入快照，快照变化可落库或记录操作日志。
- 参数有版本号 / 更新时间 / 来源，便于排查“当前雷达到底按什么参数跑”。

建议 Claude Code 的第一步：

1. 新增业务文档：`doc/business/运行时参数快照.md`。
2. 定义最小快照字段，不要一次搬完整协议字段：
   - selected device ids
   - work mode
   - scene number
   - DPH interval
   - radar face enabled / boresight / scan angle
3. `DphPublisher` 改为依赖一个只读 provider，例如 `IRuntimeSettingsProvider`。
4. API 先做：
   - `GET /api/runtime`
   - `PUT /api/runtime`
5. 后续再把 UI 的阵面参数、任务参数逐步接入。

架构边界：

```text
UI 参数编辑 -> RuntimeSettingsService -> DphPublisher
外部协议结构体 <- DphPublisher adapter <- 内部运行时快照
```

这一步会把“实时控制系统”的地基铺出来。

### Milestone 3 - Track Lifecycle Manager

当前 `RadarDataProcessor` 收到 `GeneralPackage` 后直接写库并推 WS。UI 端维护最近 500 条点。这对演示足够，但还不是管理系统里的“航迹”。

目标：

- 建立 `TrackManager`，把单帧目标点变成稳定航迹对象。
- 管理航迹状态：New / Active / Lost / Dropped / Archived。
- 处理超时、合并、去抖、最后更新时间、所属设备、任务归属。
- UI 订阅的是 track snapshot，而不是原始协议点。
- 数据库保存 raw point 和 track lifecycle 可以分层。

建议 Claude Code 的第一步：

1. 新增业务文档：`doc/business/航迹生命周期管理.md`。
2. 定义内部模型：
   - `TrackPoint`
   - `TrackState`
   - `TrackSnapshot`
3. `RadarDataProcessor` 保持协议适配职责，只把 `GeneralPackage` 转为 `TrackPoint`。
4. `TrackManager` 订阅/接收 `TrackPoint`，产出 `TrackSnapshot`。
5. `WebApiController` 推送 `tracks` 时改推 snapshot。
6. 加一个 1s 或 2s 超时规则：长时间未更新的目标进入 Lost。

架构边界：

```text
GeneralPackage -> RadarDataProcessor(protocol adapter)
TrackPoint     -> TrackManager(domain model)
TrackSnapshot  -> WebApiController / DataStore / UI
```

这一步是 PPI 和数据表从“点流展示”升级成“态势管理”的关键。

### Milestone 4 - Task As Backend Domain

前端现在已经有 task 概念，但它只在 localStorage 中。对于管理系统，任务应该成为后端业务对象，并且关联设备、航迹、日志、操作记录。

目标：

- 后端建立任务表和任务状态机：Draft / Active / Completed / Archived。
- 任务关联设备集合。
- 航迹、系统事件、控制命令记录 task_id。
- UI 切换任务时加载对应设备、历史航迹、日志。

建议 Claude Code 的第一步：

1. 新增业务文档：`doc/business/任务生命周期管理.md`。
2. 建立最小表：
   - `tasks`
   - `task_devices`
3. API：
   - `GET /api/tasks`
   - `POST /api/tasks`
   - `POST /api/tasks/{id}/activate`
   - `POST /api/tasks/{id}/complete`
4. 前端任务 localStorage 先保留为 fallback，逐步切后端。

这一步会让系统真正像“管理系统”，而不是“实时画面工具”。

### Milestone 5 - Contract And Test Harness

现在 C++ 和 TS 的 API 契约靠手写同步，容易漂。后续功能一多，这会变成持续摩擦。

目标：

- 给 REST/WS 契约建立单一来源。
- 至少维护一份 `doc/api.md` 或 `schema/*.json`。
- test_comm 不只是发 UDP，也能跑端到端场景脚本。
- CI 或本地脚本能一键跑 core tests + HTTP parser tests + business tests。

建议 Claude Code 的第一步：

1. 新增 `doc/api-contract.md`，记录 REST/WS payload。
2. 把 `ui/src/lib/protocol.ts` 中旧注释 `HttpServer` 改掉，并按契约整理类型。
3. 给 `WebApiController` 的关键接口补集成测试或轻量 smoke test。
4. 固化一条 E2E 手动脚本：
   - 启动 core
   - 启动 test_comm
   - start acquisition
   - 验证 WS 收到 status/tracks
   - stop acquisition

## Suggested Priority

如果我是这个项目的架构 owner，我会这样排：

1. **Device Registry**：先把设备作为后端业务对象立住。
2. **Runtime Settings Snapshot**：再把控制参数和 DPH 从 UI/默认值里解耦出来。
3. **Track Lifecycle Manager**：然后把航迹从原始点流升级成态势对象。
4. **Task Backend Domain**：最后把前端任务迁到后端，串起设备、航迹、日志和操作记录。
5. **Contract/Test Harness**：穿插做，但不要等到最后才做。

当前 `WebService::stop()` 的 `m_idleTimers.clear()` 可以让 Claude 顺手补掉，但不要把它当成主线。接下来最值得开的任务是 **设备台账与端点映射**，因为它会决定后续多设备控制、任务绑定、运行时参数、UI 状态聚合的所有边界。

## Architecture Decisions After Owner Review

下面是根据架构 owner 评估后修正过的路线。后续 Claude Code 应以这一版为准，不要继续按“航迹生命周期由上位机管理”或“DPH publisher 轮询快照”的方向实现。

### Decision 1 - Device And Business Are Strongly Coupled

设备和业务不能长期分开考虑。现实系统里设备台账、任务、运行时参数、状态控制、显示规则是强关联的。

确认方向：

- 设备信息由后端管理；
- 前端负责加载、展示和发起操作；
- `endpoints.json` 描述通信端点；
- 设备台账描述业务设备；
- 两者通过 `endpointId` / binding 关联。

这意味着 Milestone 1 仍然是优先级最高的基础工作。

### Decision 2 - Runtime Settings Is Event Updated, Not Polled By Publisher

DPH 参数确实和 UI 调参强相关，但不能让 UI 直接绑死外部协议结构体，也不能让 `DphPublisher` 每 50ms 去“读总线”。

修正后的模型：

```text
UI 调参
  -> Web API
  -> RuntimeSettingsService
  -> publish RuntimeSettingsChanged / DphParamsChanged event
  -> DphPublisher 接收事件并更新本地缓存
  -> QTimer 每 50ms 使用最近一次缓存参数组包发送
```

关键点：

- 前端没改参数，就不发事件；
- 后端没收到新事件，就沿用旧参数继续 50ms 发 DPH；
- 50ms 周期只负责发送，不负责轮询总线；
- “快照”不是为了省传输资源做 diff，而是后端持有的一份当前有效运行参数；
- UI 可以发送局部参数更新，但后端需要合成完整有效参数；
- 外部 `DataPackageHeader` 仍然应该由 adapter/publisher 组装，UI 不直接理解完整协议结构体。

建议命名：

- `RuntimeSettingsService`：持有当前有效运行参数；
- `DphParamsChanged`：参数变更事件；
- `DphPublisher`：订阅变更、缓存参数、定时组包发送。

### Decision 3 - TrackManager Is For Track Data Presentation, Not Track Lifecycle

上位机不负责航迹起批、消失、融合等“航迹管理”算法，这属于数据处理系统职责。上位机的核心工作是接收航迹并显示。

因此 Milestone 3 应从 `TrackLifecycleManager` 修正为 `TrackDisplayManager` 或 `TrackPresentationManager`。

它的职责不是判断航迹是否起批/消失，而是管理“航迹数据在当前业务状态下应该怎么给 Web 显示，以及用户操作后数据如何分发”。

建议职责：

- 接收 `RadarDataProcessor` 解析后的航迹点；
- 根据 track id、点序号、用户交互状态生成显示样式；
- 例如同一 `targetId=1`：
  - 前 3 个点用一种尺寸/颜色；
  - 第 4 个点开始用另一种尺寸/颜色；
  - 普通点击、高亮、追踪使能后样式不同；
  - 追踪使能后可能把该航迹数据转发给其他模块；
- 输出给 UI 的不是原始协议点，而是 `TrackDisplaySnapshot`。

建议模型：

```text
RadarDataProcessor
  -> TrackPoint
  -> TrackDisplayManager
  -> TrackDisplaySnapshot
  -> WebApiController / WebSocket
  -> UI
```

上位机可以管理显示状态和用户操作状态，但不冒充雷达/数据处理系统做航迹生命周期算法。

### Decision 4 - Task Domain Can Solidify Stable Workflows

任务状态机方向确认。尤其是以下业务可以固化：

- UI 切换任务时加载任务关联设备；
- UI 切换任务时加载该任务历史航迹；
- UI 切换任务时加载该任务日志 / 操作记录；
- 任务状态驱动设备选择、数据过滤和历史归档。

这意味着任务不是前端临时分组，而应该逐步成为后端 domain。

### Decision 5 - Core-Web Track Protocol Should Be Fixed

Milestone 5 不需要做得太复杂。核心原则是：无论后端接入哪个厂家、底层协议如何变化，`core` 和 `web` 之间传输的航迹协议应固定。

确认方向：

- 厂家协议差异在 `protocol + adapter + RadarDataProcessor` 内部消化；
- Web 只接收固定的 `TrackDisplaySnapshot` / `TrackPointView` 协议；
- 后续第三方如果要接入我们的航迹数据，给他 `core + 固定协议` 即可；
- 不需要一开始就做复杂 schema/codegen，先把固定协议文档和 TS/C++ DTO 对齐。

建议新增：

```text
doc/api-core-web-track-protocol.md
```

其中明确：

- track id；
- device id；
- timestamp；
- position/range/azimuth/elevation；
- display style；
- interaction state；
- optional routing/control flags。
