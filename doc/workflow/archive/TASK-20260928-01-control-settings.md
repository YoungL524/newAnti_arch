# 当前任务：雷达控制参数面板与任务参数更新链

本文件一次只记录一个任务。新任务开始时，先将上一任务的方案和交接移入 `doc/workflow/archive/`，再填写本文件；归档只为追溯，不改变 `PROJECT_CONTEXT.md` 中的项目事实。协作边界与优先级见仓库根目录 `AGENTS.md`。

## 状态

待用户验证

## Codex 直接实施结果（2026-09-28）

- 用户决定停止 CodeBuddy 分工；本任务剩余的数字输入问题由 Codex 直接修复并复核，旧方案和往返审查记录仅作追溯。
- `ui/src/components/dialogs/RadarControlSettingsDialog.tsx`：`NumberField` 保存可见的十进制编辑文本；空白、负号等未完成输入以及非法类型或越界值标红并上报给已有整表有效性状态，禁止“应用”；只有合法值写入本地草稿。切换阵面时重建当前阵面控件，避免沿用上一阵面的临时输入。
- `AGENTS.md`：取消插件分工，恢复 Codex 直接制定方案、实施、静态复核和交接；编译与运行验证仍由用户执行。
- Codex 独立运行 `git diff --check`，无空白错误；运行 `scripts/check_doxygen.ps1`，40 个头文件和 330 个 C++ 函数通过中文 Doxygen 机械审计。前端改动经静态代码审查，未执行编译、测试或运行验证。
- 待用户验证：清空、输入负号/小数中间态及越界数值时“应用”禁用；改回合法值后恢复；切换阵面不带入上一阵面的临时文本。后端还需由用户编译并验证 Ready/Running 更新和下一周期报文。未获得结果前不能标记“已验收”。

当前及后续任务状态按“待实施 → 实施中 → 待用户验证 → 已验收”推进；需要补充业务决定时写“待用户决定”，不得把静态检查通过写成编译或运行通过。

## CodeBuddy 原方案（保留作为输入；以下述 Codex 计划为实施依据）

- 任务编号与目标：
  `TASK-20260928-01 雷达控制参数面板与任务参数更新链`。为当前活动任务提供完整
  `RadarControlSettings` 低频编辑面板，并接通“HTTP 更新 → TaskService 持久化任务快照 →
  DeviceController 原子替换运行副本 → 50 ms 周期读取最新值”的正式链路。不新增命令总线、FIFO、
  ACK、重试或第二份长期参数模型；不新增公开业务类型（复用现有 `RadarControlSettings`）。

- 用户原始要求或链接：
  2026-09-28 会话确认，选择方案 A（控制参数面板 + 参数更新链），并要求把方案写入本文件。
  界面字段与值域依据 `doc/contracts/DCMP界面控制参数映射契约.md` §5～§9。

- 现行协议、架构和代码依据：
  - `AGENTS.md`：Command 平面固定调用链；控制参数快照不得进入 Topic；“新增公开数据结构前必须先更新
    对应合同”，本任务不新增业务结构。
  - `doc/architecture/11-最小控制Owner设计.md` §2.2：运行中的“应用参数”先由 TaskService 保存任务快照，
    成功后再原子替换 Owner 内存值；共享模板只在显式“另存为模板/更新模板”时变化。
  - `doc/contracts/DCMP界面控制参数映射契约.md` §5/§6/§7/§8/§9：CommonControl、FrameControl、
    Navigation、FaceControl 的控件、值域、初始值与模式依赖；“本地草稿 + 一次应用”。
  - `doc/contracts/前后端接口契约-v1草案.md` §3.4/§4.3：HTTP 成功/失败 JSON、稳定错误码与状态码映射。
  - 代码现状：`core/task/RadarControlSettings.h` 与 `RadarControlSettingsJson` 已存在并含值域校验；
    `DeviceController` 已持有唯一运行副本 `m_controlSettings` 且 50 ms 定时器读取该副本（`sendControl()`）；
    `TaskService` 目前只从模板复制任务快照，无更新入口；`StorageRuntime`/`DeviceRegistryHttpService` 已有
    “主线程解析边界 + 存储线程阻塞调用 Owner”的既有模式；`ui/` 参数类型已定义但无任何参数控件。

- 允许修改的文件或模块：
  - 后端：`core/persistence/SqliteRuntimeRepository.{h,cpp}`、`core/task/TaskService.{h,cpp}`、
    `core/task/RadarControlSettingsJson.{h,cpp}`（拆出“结构/类型解析”与“枚举值域校验”两级，供 HTTP 区分
    400 与 422）、`core/control/DeviceController.{h,cpp}`、`core/app/StorageRuntime.{h,cpp}`、
    `core/app/ApplicationController.{h,cpp}`、`core/web/DeviceRegistryHttpService.{h,cpp}`。
  - 文档：`doc/contracts/前后端接口契约-v1草案.md`（补充控制参数更新路由）、
    `doc/contracts/DCMP界面控制参数映射契约.md`（如实现态需补充）、本文件。
  - 前端：`ui/src/lib/tasksApi.ts`（新增 API）、`ui/src/lib/taskTypes.ts`（仅在类型缺失时补充）、
    新增参数面板组件及其子控件、`ui/src/App.tsx` 与 `ui/src/components/layout/TitleBar.tsx`（入口与刷新）。
  - 明确不改：`core/datadomain/*`、`core/model/*`（不新增业务类型）、`core/protocol/*` 载荷布局、
    以及 50 ms 发送周期与心跳门控规则。

- 实施步骤：
  1. 【后端-参数解析】`RadarControlSettingsJson` 拆为两级并保留原组合函数：新增
     `radarControlSettingsStructureFromJson`（只做嵌套结构与字段类型/缺失校验，不做范围判断）+ 既有
     `validateRadarControlSettings`（枚举与值域）；`radarControlSettingsFromJson` 保持“结构 + 值域”的既有
     组合语义，现有调用方（`loadControlTemplates`）行为不变。目的：HTTP 边界能把“结构/类型错误”与
     “枚举/值域非法”映射为不同稳定码，不新增公开业务类型。
  2. 【后端-持久化】`SqliteRuntimeRepository` 增加“按 tasks 主键更新 `control_settings_json` 与
     `updated_utc_ms`”的单行事务方法；`TaskService::updateControlSettings(taskId, taskUuid, settings, task)`
     在存储线程内先核对任务属于本服务、属于当前活动任务且阶段为 Ready 或 Running（不满足则不落库、不修改
     任何状态），再提交 SQLite，成功后更新内存 `m_tasks` 快照；不修改模板、不修改 `TaskState`。
  3. 【后端-Owner】`DeviceController::applyControlSettings(taskId, settings)`：仅当
     `taskId == m_preparedTaskId` 时以纯值拷贝替换 `m_controlSettings` 并返回 true，否则返回 false 且不修改。
     不发送报文、不改 DeviceState、不启停定时器。
     @note `prepareTask()` 仅在开始采集时建立运行副本（`DeviceController.cpp:139-166`），且 `endAcquisition()`
     不清除准备态（`DeviceController.h:110`），故“从未开始过的 Ready 任务”没有运行副本，“停止后回到 Ready
     的任务”仍有运行副本；编排者用 `preparedTaskId()` 判定，与 `TaskPhase` 无关，“未替换”不算失败。
  4. 【后端-编排】`StorageRuntime::updateTaskControlSettings(taskUuid, requestBody, appliedSettings, response)`
     在存储线程内依次完成：解析 `controlSettings` 顶层结构 → 结构/类型校验 → 枚举值域校验 → 解析任务存在性
     与阶段 → 持久化；每类失败直接写入带稳定 `code` 的 V1 响应，调用方不得解析中文文本。
     `ApplicationController::updateTaskControlSettings(storageRuntime, taskUuid, requestBody, response)` 只在存储
     返回成功且 `m_deviceController.preparedTaskId() == 该任务 TaskId` 时调用 `applyControlSettings` 同步运行
     副本。持久化与 Owner 替换两步同在主线程的同一同步调用栈内完成（存储调用为 `BlockingQueuedConnection`，
     期间主线程不会重入 start/stop，因此不存在 TOCTOU）。
  5. 【后端-HTTP】`DeviceRegistryHttpService::dispatch` 增加 `PUT /api/v1/tasks/{taskId}/control-settings`
     路由；沿用既有“从响应 JSON 取 `error.code` 映射状态码”的机制并新增 `TASK_STATE_CONFLICT → 409`，
     不按中文文本判断状态码；同步在 `前后端接口契约` §3.4/§4.3 登记路由、请求体与错误码映射与新码。
  6. 【前端-API】`tasksApi.ts` 增加 `updateTaskControlSettingsApi(id, controlSettings)`，复用统一 `apiCall`
     的错误展示与 `ApiResult` 契约。
  7. 【前端-面板】新增参数面板对话框：通用参数、帧参数（按 `frameWorkMode` 渐进展示/灰显）、
     1～4 号阵面页签（含“复制参数到全部阵面”）、只读部署信息（经纬高来自设备台账，Navigation 不进入任务
     快照）。采用“本地草稿 + 一次应用”：输入过程不发包，应用时提交完整参数集合，失败保留上一份有效配置并
     逐字段提示；被模式禁用或暂不响应的字段保留控件并标注“当前 DP 版本可能不响应”，切换模式不清零。
  8. 【前端-入口】在当前活动任务（Ready/Running）提供“雷达参数”入口打开面板；应用成功后用响应中的任务
     快照刷新本地显示与 `activeTask`，不整页重载。
  9. 【收尾】按中文 Doxygen 补齐所有新增/修改的类型、字段（`///<`）与 `.cpp` 逐函数注释；运行仓库注释
     审计脚本。仅静态检查，不编译、不运行测试。

- 可观察的验收条件：
  1. 对当前 Ready 任务应用一组合法参数后，`GET /api/v1/tasks` 返回的 `controlSettings` 变为新值，
     且 `sourceTemplateId` 与共享模板内容保持不变。
  2. 采集进行中应用参数成功后，随后 50 ms `0x0001` 报文包含新值（DP/SP 在线时）；无需重启采集、
     无需新建 `collection_session`，`TaskState` 不变。
  3. 请求体结构或类型错误（如缺少 `controlSettings`、字段类型不符）返回 400 `INVALID_REQUEST`；
     枚举或值域越界返回 422 `BUSINESS_REJECTED` 并给出字段级信息；两种失败下任务快照与
     DeviceController 运行副本均保持旧值。
  4. 任务 UUID 不存在或已软删除返回 404 `RESOURCE_NOT_FOUND`；任务存在但不是当前活动任务、或阶段不是
     Ready/Running 返回 409 `TASK_STATE_CONFLICT`；两种情况均不产生任何持久化或 Owner 变化。
  5. 编辑过程中不产生任何 HTTP 请求与 UDP 报文；只有“应用”触发一次提交。
  6. 未新增公开业务类型，`RadarControlSettings` 仍是模板、任务快照与 Owner 运行副本共用的唯一值类型；
     未引入 FIFO、ACK、重试队列或第二份长期参数模型。

- 已确认的决策（2026-09-28 用户确认）：
  1. 路由形态：新增专用 `PUT /api/v1/tasks/{taskId}/control-settings`，仅接受完整参数、支持 Ready 与
     Running、语义单一；不复用接口契约 §4.3 中保留给“修改尚未运行任务”的 `PUT /api/v1/tasks/{taskId}`。
     实施时同步在 `前后端接口契约` §4.3 登记该路由。
  2. 运行中应用：允许采集进行中直接应用参数；先由 TaskService 持久化任务快照，成功后再原子替换
     DeviceController 运行副本（对应实施步骤 2、3、4）。
  3. 双语范围：本次参数面板只做中文；全局中/English 词典与语言切换单列为独立 i18n 任务，不并入本任务。

- 其余风险与约束：
  4. RF 复位（§6/§10）的保持时间与恢复条件仍未冻结，本次只保留禁用或只读占位，不实现推测性动作状态机。
  5. Navigation 经纬高按契约只读自设备台账；若用户希望修改，应引导至设备编辑流程，本任务不允许在面板内改台账。
  6. 帧参数的模式依赖与灰显规则以 §6 为准；未启用或暂不响应的字段仍保留任务快照中的值，切换模式不得清零。

- 失败不变性与一致性保证（修订自 Codex 第 2 点）：
  - 校验严格先于持久化：结构、值域、任务存在性与阶段都在 SQLite 提交前完成；非法请求不落库、不改 Owner。
  - 唯一可失败步骤是 SQLite 提交：失败时任务快照（SQLite 与 `m_tasks`）与 Owner 运行副本同时保持旧值。
  - 持久化成功后对 Owner 的替换是“按 `preparedTaskId()` 判定的纯值拷贝”，无失败路径：Owner 持有该任务运行
    副本（Running，或停止后仍保留准备态）时同步为同一份新值；未持有时跳过，由下次 `prepareTask()` 从
    TaskService 载入新快照。
  - 明确本操作不是跨线程原子事务：一致性依赖“单一可失败点 + 两步同在主线程同步调用栈 + 任务状态由
    TaskService 在存储线程串行判定”，不得把跨线程两步描述为整体原子事务。
  - 若将来 Owner 替换引入可失败路径（例如再次编码或能力校验），必须先核验再提交，禁止在持久化后追加
    可失败步骤。

- HTTP 错误码映射（修订自 Codex 第 3 点；实施时写入 `前后端接口契约`，状态码只从响应 `error.code` 映射，
  不解析中文文本）：
  - 顶层结构错误（无 `controlSettings` 或不是 object） → `INVALID_REQUEST` / 400。
  - 字段缺失或类型错误（结构级） → `INVALID_REQUEST` / 400。
  - 枚举或值域越界（`validateRadarControlSettings` 失败） → `BUSINESS_REJECTED` / 422。
  - 任务 UUID 不存在或已软删除 → `RESOURCE_NOT_FOUND` / 404。
  - 任务存在但非当前活动任务或阶段不是 Ready/Running → `TASK_STATE_CONFLICT`（新增登记码）/ 409。
  - 存储线程调用失败 → `SERVICE_UNAVAILABLE` / 503。
  - 请求体固定为 `{"controlSettings": {common, frame, faces}}`；该映射细化 §3.4 中“枚举非法”的归类，
    实施时同步修订合同并登记新码与路由。

## Codex 批准的文件级实施计划（2026-09-28）

### 评估结论与执行边界

修订版的 Ready/Running 区分、专用 PUT 路由、400/422/404/409 错误分类和先持久化再更新运行副本方向通过；尚缺跨线程输出、提交前的 Running Owner 一致性门禁、SQLite 失败的 500 映射及前端状态更新落点。以下计划补齐这些细节，取代上方实施步骤中不够具体或相冲突的描述。上方“唯一可失败步骤是 SQLite”仅指预期业务失败；实现仍须检查线程调用失败、持久化结果和 Owner 不变量。未经用户要求，不编译、不运行或新增测试。

### 后端逐文件改动

1. `core/task/RadarControlSettingsJson.{h,cpp}`：保留 `radarControlSettingsFromJson()` 的原有“完整结构 + 值域校验”语义供模板加载使用；抽出返回 `RadarControlSettings` 候选值及字段名错误的结构/类型解析函数，HTTP 更新先调用它，再调用现有 `validateRadarControlSettings()`。四个 `faces` 必须全部提供。只增加函数，不增加新的参数业务结构；字段错误至少指出首个失败字段。
2. `core/persistence/SqliteRuntimeRepository.{h,cpp}`：增加按数据库 `task_id` 更新 `control_settings_json`、`updated_utc_ms` 的方法。使用现有 SQLite 连接与 `QSqlQuery` 风格，`WHERE task_id=? AND is_deleted=0 AND closed_utc_ms IS NULL AND archived_utc_ms IS NULL`；执行失败或受影响行数不为 1 均返回 false 和可诊断错误。单条 UPDATE 已由 SQLite 原子提交，不另建事务框架，不改模板表和会话表。
3. `core/task/TaskService.{h,cpp}`：增加 `updateControlSettings(taskUuid, settings, taskIdOut, taskOut)`。复用 UUID→TaskId/数据库主键映射，核对当前 `TaskState` 的 TaskId 和 Ready/Running；复核参数值域；仓库更新成功后才替换 `m_tasks[taskId].controlSettings`，返回同一任务句柄与完整新任务值。失败时不改内存、TaskState、模板或会话。外层在调用前区分“UUID 不存在”与“任务状态不允许”，不得解析 `lastError()` 中文文本。
4. `core/app/StorageRuntime.{h,cpp}`：增加仅供 `ApplicationController` 在存储线程同步调用的更新入口，输入 UUID、完整请求体及主线程读取的 `preparedTaskId`，输出 `TaskId`、更新后的 `TaskDefinition` 和 V1 JSON 响应。依次校验 JSON 文法与顶层 `controlSettings`、完整字段类型、业务值域、任务存在性、Ready/Running；若阶段为 Running 且 `preparedTaskId` 不等于目标 TaskId，在落库前返回 `TASK_STATE_CONFLICT`。Ready 允许无准备态。调用 TaskService 后用现有 `taskJson(..., true)` 构造响应；存储失败返回独立 `PERSISTENCE_FAILED`，不得伪装成 422。不要在此类中调用 DeviceController、发送 UDP 或保存第二份长期参数。
5. `core/control/DeviceController.{h,cpp}`：增加同线程 `applyControlSettings(taskId, settings)`。仅当非零 taskId 等于 `m_preparedTaskId` 时一次替换 `m_controlSettings`，否则保持旧值并返回 false；不重启定时器、不触发即时 UDP、不改 DeviceState。此方法不再做可能失败的编码或二次业务校验。现有 `sendControl()` 自然在下一周期读取新值。
6. `core/app/ApplicationController.{h,cpp}`：增加主线程参数更新编排入口。先读取 `m_deviceController.preparedTaskId()`，再以 `BlockingQueuedConnection` 调 StorageRuntime 的新入口并取得 `TaskId`、任务值及 JSON；存储失败原样返回错误。成功后仅当准备句柄与该任务句柄相等时调用 `applyControlSettings()`；从未准备的 Ready 任务跳过，下一次 `prepareTask()` 载入已保存值。主线程同步调用期间不启动嵌套事件循环，50 ms 定时器不会在两步之间发送。若匹配句柄却意外替换失败，不能回成功或声称双副本一致；记录不变量故障并返回 500，评估时必须核查此分支，不能静默忽略。
7. `core/web/DeviceRegistryHttpService.cpp`：在通用 `/api/v1/tasks/` 分支内先匹配精确后缀 `/control-settings` 和 PUT；拒绝路径中额外分段或错误方法。无效 UUID/JSON 文法/结构用 `INVALID_REQUEST` 400，字段业务值域用 `BUSINESS_REJECTED` 422，已删除或不存在用 `RESOURCE_NOT_FOUND` 404，非活动或阶段冲突用 `TASK_STATE_CONFLICT` 409，存储线程不可用用 503，SQLite 或不变量失败用 `PERSISTENCE_FAILED`/`INTERNAL_ERROR` 500。沿用现有 `error.code` 映射并为 500 补 `Internal Server Error` reason；不靠中文错误文本判码。头文件只在公开接口契约实际变化时更新。

### 前端逐文件改动

8. `ui/src/lib/tasksApi.ts`：新增 `updateTaskControlSettingsApi(id, settings)`，使用既有 `apiCall` 发送 `PUT /api/v1/tasks/{id}/control-settings`，请求体为 `{ controlSettings: settings }`，返回既有 `ApiTask`；不增加另一套请求或错误容器。`ui/src/lib/taskTypes.ts` 已具备完整 `RadarControlSettings`，无需改类型。
9. 新建一个 `ui/src/components/dialogs/RadarControlSettingsDialog.tsx`，优先沿用现有对话框的布局与控件，不为每组字段建独立组件。打开时从 `activeTask.controlSettings` 深拷贝本地草稿；Common/Frame/Faces 的控件和值域逐项依据 `doc/contracts/DCMP界面控制参数映射契约.md` §5～§8，不把控制字误作连续十进制枚举。单面阵只显示 1#，四面阵显示 1#～4#，复制到全部阵面只修改草稿；切换 `frameWorkMode` 不清零被隐藏或灰显字段。只读部署信息从 `App.tsx` 已有设备台账对象取经纬高和阵面安装姿态；找不到设备时显示“设备台账不可用”，不编造零值。RF 复位保持不可执行说明，不实现按钮动作。表单输入不发 HTTP；只在“应用”时一次提交全部值，提交中禁用重复提交，失败保留草稿并显示字段级错误，成功以服务端返回快照关闭或更新对话框。
10. `ui/src/App.tsx`：持有对话框开关与一次更新请求处理函数；仅当前活动 Ready/Running 且非历史视图可打开。成功后用 `apiTaskToTask(response.task)` 替换 `tasks` 中对应对象，使现有 `activeTask` 派生值自动刷新；失败显示响应错误，不整页重载、不独立保存第二份已应用参数。把当前任务、匹配的设备台账和回调传给对话框。
11. `ui/src/components/layout/TitleBar.tsx`：添加由 `App.tsx` 传入的“雷达参数”打开回调和入口；任务缺失、历史视图、非 Ready/Running、后端断开或更新进行中禁用。TitleBar 只展示入口，不持有草稿，也不直接请求 API。

### 合同、交接与验收

12. `doc/contracts/前后端接口契约-v1草案.md`：登记完整 PUT 路由、`{controlSettings}` 请求、成功返回 `data.task`、400/404/409/422/500/503 的稳定 `error.code`。`doc/contracts/DCMP界面控制参数映射契约.md` §5 目前写“首版全局中/English 切换”，与本任务已记录的用户决定“本次只做中文”冲突；将该句修订为本次面板中文、全局 i18n 独立任务，其他已冻结协议字段含义不动。若“失败返回逐字段错误”意指一次返回所有错误，而现有校验只返回首个字段，先记录差异，不擅自新增错误数组协议。
13. 所有新增/修改的 C++ 头文件接口、字段及 `.cpp` 函数逐项补中文 Doxygen；TS/TSX 新增组件、回调及状态补兼容 Doxygen 的中文 JSDoc。CodeBuddy 只运行注释审计、`git diff --check` 等静态检查，记录命令及结果；不编译、不运行测试、不新增测试程序。交接写明实际修改文件、与计划差异、用户需手工验证的 Ready 更新、Running 下一周期报文、各错误码及模板不变性。

## 实施交接（CodeBuddy 填写；Codex 复核）

- 实际改动文件及原因（严格按 Codex 第 134-160 行的文件级计划；未改动计划外文件）：
  - `core/task/RadarControlSettingsJson.{h,cpp}`：新增 `radarControlSettingsStructureFromJson()`，只做嵌套结构
    与字段类型校验；`radarControlSettingsFromJson()` 改为“结构 + 值域”的组合，模板/数据库加载行为不变。
  - `core/persistence/SqliteRuntimeRepository.{h,cpp}`：新增 `updateTaskControlSettings()`，按 `task_id`
    单条条件更新 `control_settings_json`、`updated_utc_ms`，条件是 `is_deleted=0 AND closed_utc_ms IS NULL
    AND archived_utc_ms IS NULL`，受影响行数不为 1 即失败；不建事务框架、不改模板/会话表。
  - `core/task/TaskService.{h,cpp}`：新增 `updateControlSettings(taskUuid, settings, taskId, task)`；先核对
    当前 `TaskState` 的 TaskId 与 Ready/Running，再复核值域，仓库成功后才替换 `m_tasks` 内存快照。
  - `core/control/DeviceController.{h,cpp}`：新增 `applyControlSettings(taskId, settings)`；仅当
    `taskId == m_preparedTaskId` 时一次纯值替换 `m_controlSettings`，否则保持旧值返回 false；不发送、不改
    DeviceState、不启停定时器。
  - `core/app/StorageRuntime.{h,cpp}`：新增存储线程入口 `updateTaskControlSettings(taskUuid, requestBody,
    preparedTaskId, appliedTaskId, appliedTask, response)`；按“结构 → 类型 → 值域 → 任务存在性 →
    Ready/Running → 持久化”顺序分类失败，逐类写入稳定 `error.code`；Running 且 preparedTaskId 不匹配时
    在落库前拒绝；成功后用现有 `taskJson(..., true)` 组装 `data.task`。
  - `core/app/ApplicationController.{h,cpp}`：新增主线程编排入口；先读 `preparedTaskId()`，再以
    `BlockingQueuedConnection` 调存储入口；成功后仅在准备句柄与任务句柄相等时调用 `applyControlSettings()`；
    句柄匹配却替换失败时写 `INTERNAL_ERROR` 并返回失败（不返回成功、不声称双副本一致）。
  - `core/web/DeviceRegistryHttpService.cpp`：在 `/api/v1/tasks/` 分支内新增
    `PUT /api/v1/tasks/{uuid}/control-settings`；`error.code → 状态码` 新增
    `TASK_STATE_CONFLICT→409`、`PERSISTENCE_FAILED→500`、`INTERNAL_ERROR→500`，并补 500 的
    `Internal Server Error` reason；仍只从 `error.code` 映射，不解析中文文本。
  - `ui/src/lib/tasksApi.ts`：新增 `updateTaskControlSettingsApi(id, settings)`，PUT 专用路由，请求体
    `{ controlSettings: settings }`，复用既有 `apiCall`/`ApiTask`。
  - `ui/src/components/dialogs/RadarControlSettingsDialog.tsx`（新增）：单一对话框组件 + 通用字段小控件
    （NumberField/HexField/SwitchField/SelectField），未按字段分组拆组件；打开时深拷贝任务快照；通用/帧/
    阵面三页；含“复制参数到全部阵面”；只读部署信息；RF 复位为不可执行占位；应用时才一次提交。
  - `ui/src/App.tsx`：新增 `showRadarSettings`/`radarSettingsSaving` 与 `applyRadarSettings`；成功后用
    `apiTaskToTask` 替换 `tasks` 中对应任务（`activeTask` 派生值自动刷新）；渲染对话框并把入口回调传给
    TitleBar。
  - `ui/src/components/layout/TitleBar.tsx`：新增“雷达参数”入口按钮，禁用条件由 App 传入的
    `radarSettingsDisabled` 叠加本组件内的 `!connected`。
  - `doc/contracts/前后端接口契约-v1草案.md`：§3.4 收敛 400/422 归类并补登记新码；§4.3 登记路由、请求体、
    成功响应与 400/404/409/422/500/503 映射。
  - `doc/contracts/DCMP界面控制参数映射契约.md`：§5 双语句子改为“本次面板只做中文，全局 i18n 单独任务”；
    §9 记录“只返回首个失败字段”的现状差异。
- 关键实现决定：
  1. 运行副本判定统一用 `preparedTaskId()`，与 `TaskPhase` 无关：从未开始的 Ready 跳过，Running 与停止后
     仍保留准备态的任务同步替换，避免把“未替换”误判为失败。
  2. 400/422 拆分靠新增的结构解析函数，`radarControlSettingsFromJson()` 组合语义保持，现有调用方零影响。
  3. 持久化与 Owner 替换两步同在主线程同步调用栈内（存储为阻塞 queued 调用，期间无重入），因此不引入
     TOCTOU；仍声明它不是跨线程原子事务。
  4. `applyControlSettings()` 为无失败路径的纯赋值，唯一可能失败的是 SQLite 提交，满足失败不变性。
- 偏差、与计划差异及未完成项：
  1. CFAR 门限1/2 与速度门限在合同中是 `int32` 十六进制补码，界面按十六进制无符号串显示、提交时以
     `parsed | 0` 写回有符号值；未实现“补码可视化”之外的解释。
  2. 单/四面阵的阵面页签数量取自 `task.radarArrayType`（而非设备台账），确保设备台账缺失时仍按任务形态
     显示 1 或 4 个页签。
  3. 因 `taskJson()` 现有约定把任何活动任务序列化为 `Ready`，本接口成功响应对 Running 任务同样返回
     `Ready`；未在本任务改变该既有映射（改它会牵动 `listTasks/activeTask` 与 UI 状态语义）。
  4. 工作区在本次开工前已存在与本任务无关的未提交改动（`core/protocol/DcmpPayloads.h`、
     `core/protocol/DcmpProtocolTypes.h`、`doc/protocol/toDP.h`、`tests/tst_DcmpProtocolTypes.cpp`、
     `AGENTS.md`、`PROJECT_CONTEXT.md` 及若干架构/协议文档），均为上一轮协议基线同步，本任务未触碰、未回退。
  5. 未新增测试程序、未编译、未运行测试（按 AGENTS 与计划要求）。
- 静态检查及结果：
  - `powershell -File scripts/check_doxygen.ps1`：`[OK] 40 个头文件和 328 个 C++ 函数通过中文 Doxygen
    机械审计。`（exit 0）
  - `git --no-pager diff --check`：exit 0；仅有既有的 LF→CRLF 提示，无空白或冲突标记错误。
  - IDE 语言诊断（read_lints）对本任务全部改动文件与新增文件报告 0 条 error/warning。
- 编译与运行验证：由用户执行；未获得结果前填“未验证”。
  待用户手工验证：Ready 任务更新后 `GET /api/v1/tasks` 的 `controlSettings` 变化且模板不变；Running 任务
  应用后下一周期 50 ms `0x0001` 报文取到新值；缺 `controlSettings`/类型错误→400、枚举或值域越界→422、
  不存在或已删除→404、非活动或阶段冲突→409、SQLite/内部故障→500、未就绪→503；编辑过程无 HTTP 与 UDP。
- 交付日期：2026-09-28

## 偏差与待决（双方追加具体证据）

- 2026-09-28 Codex 方案核对：总体方向符合独立 Command 链和任务快照合同，但以下问题解决前不实施。
  1. `core/task/TaskService.cpp:194-218` 的 `activateTask()` 只发布 Ready；`core/app/ApplicationController.cpp:69-79` 在开始采集时才调用 `prepareTask()`；`core/control/DeviceController.cpp:139-161` 此时才建立 `m_preparedTaskId`。方案步骤 2、3 对 Ready 一律调用 `applyControlSettings(taskId, ...)` 会失败。请明确 Ready 只更新任务快照，开始采集时由 `prepareTask()` 载入新值；Running 才同步更新 Owner 运行副本。
  2. 方案步骤 3 写“先持久化，成功后替换 Owner”，又写“任一步失败整体返回失败且不替换 Owner 值”。第二步失败时 SQLite 和 `m_tasks` 已是新值，Owner 仍是旧值，无法满足验收中的失败不变性。请在方案中给出 Running 更新的前置核验、提交后 Owner 替换的可失败边界与异常补偿/恢复规则，且不得把跨线程两步操作称为整体原子事务。
  3. `core/web/DeviceRegistryHttpService.cpp:274-281` 把 `INVALID_REQUEST` 映射为 400，其余多数业务错误映射为 422；方案同时要求字段越界 422、非活动/已完成任务 404/409。请明确请求结构错误与字段值域错误、任务不存在与状态冲突的稳定错误码及映射，并将其写入接口合同；不能靠错误中文文本判断状态码。

- 2026-09-28 CodeBuddy 对上述三点意见的修订（已同步到上方方案，请 Codex 复核）：
  1. Ready 语义：不再对 Ready 一律调用 `applyControlSettings`。运行副本是否存在改用 `preparedTaskId()`
     判定：从未开始过的 Ready 任务只更新任务快照，由开始采集时的 `prepareTask()` 载入新值；Running 及
     “停止后仍保留准备态”的任务才同步替换运行副本。已补充 `DeviceController.cpp:139-166` 与
     `DeviceController.h:110` 的代码依据。
  2. 失败不变性：改为“校验前置于持久化 + SQLite 提交为唯一可失败步骤 + Owner 替换为无失败路径的纯值拷贝”，
     并在方案中新增“失败不变性与一致性保证”，明确声明本操作不是跨线程原子事务。
  3. HTTP 错误码：新增专用路由并确定 “400 结构 / 422 值域 / 404 不存在 / 409 状态冲突 / 503 存储失败”的
     稳定码映射（新增登记码 `TASK_STATE_CONFLICT`），写入接口合同；状态码只从响应 `error.code` 映射，
     不解析中文文本。为区分 400/422，把参数解析拆为“结构/类型”与“枚举值域”两级。

## 用户验收结果（用户反馈后记录）

- 待反馈。

## Codex 第二轮静态复核（2026-09-28）

上轮第 1 项已修复：`ApplicationController` 检查 `invokeMethod` 返回值并生成 `SERVICE_UNAVAILABLE`，Web 层也兜底拒绝空错误码。第 2 项已修复：任务 JSON 按真实 `TaskPhase` 输出，GET 和更新响应一致；前端可识别 `Active/Faulted`。第 4 项已修复：新增的两类 500 故障通过存储线程中的 `SystemEventPublisher` 发布，事件码及门禁已登记。

**仍未通过上轮第 3 项中的数字输入验收。** `ui/src/components/dialogs/RadarControlSettingsDialog.tsx:100-104` 的 `NumberField` 在 `raw.trim() === ''` 时直接返回，没有更新草稿，也没有像 `HexField` 一样上报无效状态。用户清空数字框后，“应用”按钮仍可能启用并提交旧数字；负数或小数的中间输入也可能被受控输入恢复为旧值。应让数字框保存可见的临时文本、对空白/不完整/非法文本上报无效并阻止提交，合法时才更新草稿；切换页签和阵面时要保证失效状态与实际可见文本一致。不要以静默改零或保留旧值作为处理。

本轮独立静态检查：`scripts/check_doxygen.ps1` 通过（40 个头文件、330 个 C++ 函数），`git diff --check` 无空白错误。未编译、未运行测试。

## Codex 静态复核（2026-09-28）

结论：暂不进入用户验证。Command 链、存储后替换运行副本、Ready/Running 门禁和错误码主线已接通；以下问题需要修复并重新交接，不能用 CodeBuddy 的自述代替代码核查。

1. **阻断：存储线程调用失败未形成 503 响应。** `core/app/ApplicationController.cpp:260-267` 没有检查 `QMetaObject::invokeMethod` 的返回值。调用失败时 `updated` 保持 false、`response` 仍为空；`core/web/DeviceRegistryHttpService.cpp:274-296` 已把这条分支标作 `invoked=true`，空响应被当成无错误码的失败并落入 422。应检查调用是否成功并生成 `SERVICE_UNAVAILABLE`；Web 层不能把空响应伪装成字段错误。
2. **阻断：Running 参数更新后前端任务显示被降为 Ready。** `core/app/StorageRuntime.cpp:621` 使用 `taskJson(appliedTask, true)`，该函数在第 69 行把任意活动任务写为 `Ready`；`ui/src/App.tsx:326-330` 又用响应覆盖原任务。采集不停止，但标题栏等使用 `Task.status` 的界面会显示“已就绪”。成功响应应表达真实 Running/Active 阶段；同时核对 `GET /api/v1/tasks` 与 `/active`，避免刷新后又降级。不得靠新增一份前端长期状态掩盖后端序列化错误。
3. **阻断：参数输入可显示一个值、提交另一个值。** `ui/src/components/dialogs/RadarControlSettingsDialog.tsx:121-133` 的十六进制框将非法文本留在 `text`，却不更新草稿或阻止第 246-258 行的“应用”；例如将有效值改为 `GG` 后仍会提交上一次有效值。第 90-95 行的数字框把空字符串经 `Number('')` 变成零，清空输入也会悄悄改写草稿。应保留临时编辑文本、明确标错并阻止提交，或者立即恢复有效显示值；不能静默提交与当前界面不一致的配置。
4. **合同缺口：HTTP 500 的事件要求未落实。** `doc/contracts/前后端接口契约-v1草案.md:131` 要求 500 内部错误同时形成 `SystemEvent`。新增的 `PERSISTENCE_FAILED` 和 `INTERNAL_ERROR` 分支只返回 JSON（`StorageRuntime.cpp:613-619`、`ApplicationController.cpp:269-277`），没有发布事件。请沿现有事件 Owner 的正确线程发布已发生故障事实，或先修订合同并给出用户认可的例外；不能声称满足当前合同。

静态检查：Codex 独立运行 `scripts/check_doxygen.ps1`，40 个头文件和 328 个 C++ 函数通过；`git diff --check` 无空白错误。未编译、未运行测试，未新增测试程序。

## CodeBuddy 修复交接（第 2 轮，2026-09-28）

针对上方 Codex 静态复核的 4 项问题逐条修复；未改动其它文件。

1. 存储线程调用失败形成 503：
   - `core/app/ApplicationController.cpp`：显式检查 `QMetaObject::invokeMethod` 的返回值，失败时写
     `SERVICE_UNAVAILABLE` 并返回；存储返回失败但 `response` 为空时同样写 `SERVICE_UNAVAILABLE`，不再把
     空响应交给 Web 层。新增私有 `errorResponse(code, message)`，本层只产生 `SERVICE_UNAVAILABLE` 与
     `INTERNAL_ERROR`。
   - `core/web/DeviceRegistryHttpService.cpp`：失败响应中 `error.code` 为空时直接返回 503
     `SERVICE_UNAVAILABLE`，不再落入 422。
2. Running 任务状态不再被降为 Ready：
   - `core/app/StorageRuntime.cpp`：新增 `taskStatusText(task, phase)`；`taskJson()` 第二参数由 `bool`
     改为 `TaskPhase`（默认 None），`Running→Active`、`Ready→Ready`、`Faulted→Faulted`，非活动任务才按
     `archived/completed` 标记回落到 `Archived/Completed/Draft`。
   - `listTasks` 与 `activeTask` 改用唯一 `TaskState` 阶段（新增私有 `activePhaseOf(taskId)`，仅当该
     `TaskState` 属于目标任务时才返回其阶段）；`activateTask` 显式传 `Ready`；控制参数更新成功响应传当前
     阶段。`GET /api/v1/tasks`、`/api/v1/tasks/active` 与更新响应现在给出一致阶段，刷新后不再降级。
   - 前端同步：`TaskStatus` 增加 `'faulted'`，`mapStatus` 增加 `Faulted`，`TitleBar.STATUS_LABEL` 与
     `Sidebar.STATUS_META` 增加“故障”；未新增前端长期状态。
3. 参数输入不再“显示一个值、提交另一个值”：
   - `RadarControlSettingsDialog.tsx`：十六进制框保留用户原始文本，解析失败时标红边框并提示、不同步草稿，
     并通过新增的 `ValidityContext` 上报；对话框在存在非法字段时禁用“应用”并显示“存在未完成的参数输入”。
   - 数字框清空或纯空白输入不再经 `Number('')` 写入 0；受控输入会恢复到最近有效显示值，显示与提交一致。
4. 500 分支补齐 SystemEvent：
   - `core/model/SystemEvent.h`：登记 `ControlTaskControlSettingsPersistFailed = 0x04010004`（Control/Error）
     与 `ControlTaskControlSettingsInvariantFailed = 0x04010005`（Control/Fault）。
   - `core/producer/SystemEventPublisher.cpp`：两个码加入登记表，并要求携带有效 `TaskId`。
   - `core/app/StorageRuntime.cpp`：`PERSISTENCE_FAILED` 分支在存储线程发布 Error 事件；新增
     `publishTaskSettingsInvariantFault(taskId)` 供主线程以 `BlockingQueuedConnection` 调用发布 Fault 事件。
   - `core/app/ApplicationController.cpp`：不变量故障分支先发布事件（跨线程），再返回 `INTERNAL_ERROR`。
   - 事件发布失败不回滚已确认的本地结果，也不重试。
   - 文档同步：`doc/contracts/SystemEvent错误模型契约.md` §6 登记两个新事件码；
     `doc/contracts/前后端接口契约-v1草案.md` §4.3 说明 500 事件与空响应 503 规则。

本轮静态检查：
- `scripts/check_doxygen.ps1`：`[OK] 40 个头文件和 330 个 C++ 函数通过中文 Doxygen 机械审计。`（exit 0）
- IDE 语言诊断（read_lints）：本轮全部改动文件 0 条 error/warning。
- 仍未编译、未运行测试、未新增测试程序；编译与运行验证由用户执行，状态“未验证”。
