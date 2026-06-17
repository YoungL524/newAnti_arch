# Review b920486

## 结论

这一版把 `review-4cc3fc2.md` 的两个主要护栏补上了：`TrackDisplayManager` 增加了 `m_acceptCommands` stop gate，REST 也明确返回 `accepted: true` 和 `mode: queued`。同时 `TrackDisplayManager.h` 降低了对 bus 头文件的直接 include。整体修改方向正确，没有发现新的阻塞问题。

结合当前项目整体来看，代码质量已经从“原型堆功能”进入“架构骨架基本成型”的阶段。核心链路比前几轮稳定很多，但仍不是成熟工程，主要欠账在任务后端化、WebApiController/AppController 体量、测试覆盖和部分业务规则沉淀。

## 本轮改动 Review

### 已完成得比较好的点

- `TrackDisplayManager::start()` 设置 `m_acceptCommands=true`，`stop()` 设置 false。
- `onInteractionCommand()` 入口和 queued lambda 内都检查 `m_acceptCommands`，避免 stop/clear 后已排队命令继续落地。
- `WebApiController` 交互命令成功响应新增：

```json
{
  "ok": true,
  "accepted": true,
  "mode": "queued"
}
```

- `05-核心流程.md/drawio` 已说明 REST 返回代表命令入队，最终状态以 WebSocket snapshot 为准。
- `TrackDisplayManager.h` 不再直接 include `BusEnvelope.h / ISystemBus.h`，只 forward declare `BusEnvelope`，`m_cmdSubId` 改为 `uint32_t`，头文件依赖更轻。

### 建议修改

本轮没有必须立刻修的代码问题。只有一个轻量建议：

#### 1. queued 语义可以同步写进 Web API 注释或前端调用处

位置：

- `core/web/WebApiController.cpp`
- `ui/src` 中调用航迹 interaction API 的位置

后端 response 已经有 `mode: queued`，架构文档也写清楚了。建议后续在前端调用侧也按这个语义处理：

- REST 成功只表示命令已接收；
- UI 最终显示状态不要直接信 REST response；
- 地图/表格状态以 WebSocket snapshot 回推为准。

如果前端当前只是乐观更新，需要确认是否会和后续 snapshot 产生短暂不一致。现在可以先不改，但后面做 UI 状态管理时要注意。

## 当前项目代码质量评估

### 总体评价

当前代码质量我会评为：**B- / 正在从原型走向可维护架构**。

不是“烂代码”，也还不是“工程化成熟”。它现在最好的地方是：核心边界正在被拉直，尤其是航迹显示链路已经形成明确的总线闭环；最弱的地方是：上层业务框架还没完全后端化，组合根和 Web API 胶水层还偏大，自动化测试还不足。

### 主要优点

#### 1. 架构方向已经比较清楚

目前几个核心原则已经落到代码里：

- React Web 是唯一前端方向；
- 后端持有设备 source of truth；
- `DataPacket` 和 `BusEnvelope` 的边界已经定下；
- 航迹显示输出链路走 `SystemBus + TrackDisplayWebBridge`；
- UI 航迹交互命令也已经走 `SystemBus + TrackInteractionCommand`；
- Web 层不再直接调用 `TrackDisplayManager::setInteraction()`。

这些都说明代码已经开始服务架构，而不是单纯堆接口。

#### 2. 总线模型比之前健康很多

`BusEnvelope + payloadKind + objectPayload` 解决了两个关键问题：

- 大对象不再硬塞进 `DataPacket::rawData`；
- Web/UI 输出 DTO 可以通过统一 envelope 分发。

这对后续任务状态、告警批量、历史回放、第三方固定协议输出都有复用价值。

#### 3. 文档和代码开始同步

`doc/architecture/02/04/05` 已经跟着代码重构更新，不再只是“画一张漂亮图”。这点很重要，因为你现在是在定上位机/管理系统的基座，文档如果能持续跟代码互相约束，后面不容易跑偏。

#### 4. 线程边界意识在增强

最近几轮修了：

- WebBridge 的 atomic active gate；
- TrackDisplayManager 的 command accept gate；
- 总线 worker 回调转 Qt owner 线程处理；
- 避免业务对象被 worker 线程直接修改。

这是 Qt/C++ 项目里很关键的质量分水岭。

### 主要风险

#### 1. WebApiController 仍偏大，承担了太多 REST 胶水

`core/web/WebApiController.cpp` 当前约 400 行，已经包含：

- status；
- logs；
- start/stop；
- device CRUD；
- track interaction；
- runtime settings；
- Web push 连接；
- DTO 拼装。

目前还能接受，但再继续加任务、历史、告警、回放，它会很快变成新的大泥球。

建议后续拆分方向：

- `DeviceApiRoutes`
- `RuntimeSettingsApiRoutes`
- `TrackApiRoutes`
- `SystemStatusApiRoutes`

`WebApiController` 保留为 route composition，而不是把所有业务 API lambda 都塞在一个函数里。

#### 2. AppController 仍是过重组合根

`core/AppController.cpp` 约 590 行，组合根偏大是可以理解的，但现在它还承担了不少流程判断、初始化细节和桥接订阅。

建议后续不要急着拆，但要控制新增内容：

- 新业务服务由独立类负责；
- AppController 只负责创建、wire、start/stop；
- 不要继续把业务规则放进 AppController。

#### 3. 任务仍在前端 localStorage，是当前最大业务架构缺口

`ui/src/lib/taskTypes.ts` 仍使用 `localStorage` 管任务：

```ts
localStorage.getItem(TASK_STORAGE_KEY)
localStorage.setItem(TASK_STORAGE_KEY, ...)
```

这和我们架构文档里的 `TaskService / 任务状态机 / 任务关联设备与历史` 还没对齐。现在航迹链路已经闭环，下一块最应该做的就是任务后端化。

任务后端化后才能稳定承载：

- 任务关联设备；
- 任务状态机；
- 历史航迹；
- 操作日志；
- 任务上下文过滤。

#### 4. 测试覆盖明显不足

目前重构已经进入核心链路，但从代码结构看，关键链路还缺少足够的自动化验收：

- `SystemBus` packet/envelope 订阅匹配；
- `TrackDisplayManager` command gate；
- Web API interaction 返回 `queued`；
- DeviceRegistry 与设备 id 契约；
- RuntimeSettings -> DPH publisher。

后续如果继续靠人工编译/运行验证，短期可以推进，但长期会压低重构速度。

#### 5. 业务规则还在逐步沉淀，部分边界仍容易漂

目前已经沉淀好的边界：

- 航迹显示；
- 总线载荷；
- 设备台账的一部分；
- RuntimeSettings 的一部分。

仍容易漂的边界：

- 任务；
- 历史数据；
- 告警/日志；
- 第三方输出协议；
- Web API route 组织方式。

这些需要下一阶段继续“定基座”，不能只靠局部功能实现。

## 建议下一阶段优先级

### P1. 任务后端化 / TaskService

这是当前最重要的架构缺口。建议先做最小闭环：

- `TaskInfo` 数据模型；
- SQLite task 表；
- `TaskService`；
- `GET/POST/PUT /api/tasks`；
- active task；
- task-device 关联；
- 前端从 localStorage 迁移到后端 API。

先别做复杂回放，先把任务 source of truth 收回后端。

### P2. WebApiController 拆 route 模块

等 Task API 要进来时，不要继续塞进 `registerRestRoutes()`。可以先拆一两个最明显的：

- `TrackApiRoutes`：航迹 interaction；
- `DeviceApiRoutes`：设备 CRUD。

这会让后续功能扩展更稳。

### P3. 为 SystemBus 和 TrackDisplayManager 补单元测试

优先补最小测试：

- envelope source/target/all 订阅匹配；
- unsubscribe 后不再接收；
- `TrackInteractionCommand` 被 manager 接收后产生新的 snapshot；
- stop gate 生效。

### P4. 历史航迹和日志再进入

等任务上下文有了，历史航迹和日志才有稳定归属。不要在任务还未后端化时先大做历史数据中心，否则后面容易返工。

## 最终判断

现在项目不是失控状态，反而比前面几轮更可控了。真正的变化是：之前很多东西是“能跑的功能”，现在开始变成“有边界的系统”。  

但它还处于架构基座成型期，不能马上大量堆业务页面。下一步最关键的是把 `TaskService` 做出来，并同步控制 `WebApiController/AppController` 的膨胀。只要这两点守住，后续管理系统形态是能撑起来的。
