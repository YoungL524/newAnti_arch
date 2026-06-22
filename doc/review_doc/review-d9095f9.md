# Review d9095f9

## 结论

这一版主要修了设备台账和前端显示不一致的问题：后端默认雷达品牌改成 `YHF`，前端品牌枚举也从测试期的 `cetc38/cetc14/leiying` 收敛到 `YHF/YF/BW`，并通过 `LEGACY_BRANDS` 兼容历史数据。上一轮 `WebApiController.cpp` 里的多余反引号也已经修掉。

整体架构目前已经比早期稳定很多：设备台账后端化、航迹显示输出总线化、航迹交互命令总线化、`DataPacket / BusEnvelope` 边界都已经落地。下一阶段不要继续在航迹主链路上打磨，应该转向 **TaskService 后端化**，这是当前最大的业务架构缺口。

## 本轮改动 Review

### 已完成得比较好的点

- `DeviceRegistry::ensureDefaultControllableRadar()` 默认品牌从 `cetc38` 改为 `YHF`，和当前前端品牌体系一致。
- `ui/src/components/map/types.ts` 统一导出 `BRAND_LABELS / BRAND_OPTIONS`，表单复用同一份枚举，减少重复。
- `ui/src/lib/devicesApi.ts` 增加 `LEGACY_BRANDS`，能把历史旧品牌映射到新品牌：

```ts
cetc38 -> YHF
cetc14 -> YF
leiying -> BW
```

- 创建设备 API 的返回类型变成 `ApiResult<CreateDeviceResponse>`，前端能明确使用后端生成的完整 device DTO。
- `WebService::broadcast()` 增加慢客户端保护，WebSocket 写缓冲积压超过阈值会断开，避免慢客户端拖垮推送链路。
- `ui/vite.config.ts` 明确 WS 不经 Vite 代理，减少开发环境中代理层 `ECONNRESET` 干扰。
- 新增 `doc/验收报告-简化版.md`，对交付/验收流程有帮助。

### 需要修改

#### 1. 品牌/型号仍然是前端硬编码，建议下一阶段收回后端

位置：

- `ui/src/components/map/types.ts`
- `ui/src/lib/devicesApi.ts`
- `core/business/DeviceRegistry.cpp`

当前前端仍定义：

```ts
export type RadarBrand = 'YHF' | 'YF' | 'BW'
export const BRAND_LABELS = ...
export const BRAND_OPTIONS = ...
```

这比之前测试品牌好很多，但从架构上看，品牌/型号属于设备台账的业务字典，不应长期由前端硬编码。否则后续新增厂家/型号时仍要改前端代码。

建议后续新增：

```text
GET /api/device-models
```

返回：

```json
[
  {
    "brand": "YHF",
    "brandName": "云禾峰",
    "models": [
      {
        "model": "NODE_ALPHA_01",
        "kind": 0,
        "defaultFaces": [],
        "defaultMaxRange": 5
      }
    ]
  }
]
```

前端表单从后端加载品牌/型号选项，`BRAND_OPTIONS` 只作为开发期 fallback。

#### 2. `devicesApi.ts` 有尾部空白，顺手清理

位置：

- `ui/src/lib/devicesApi.ts`

当前：

```ts
coordinateSystem: item.coordinateSystem || 'WGS-84',  
```

行尾有两个空格。不是业务问题，但建议顺手清掉，避免格式噪音。

#### 3. `doc/验收报告-简化版.md` 放在 doc 根目录可以接受，但建议后续归类

位置：

- `doc/验收报告-简化版.md`

这份文档是交付/验收材料，不属于架构文档。当前放在 `doc/` 根目录可以接受，但后续文档多起来后建议归类：

```text
doc/delivery/验收报告-简化版.md
```

或者：

```text
doc/acceptance/验收报告-简化版.md
```

这个不急，等交付类文档变多时再整理。

## 当前整体架构评估

### 当前架构状态

我把当前项目状态判断为：

```text
核心通信/航迹链路：基本成型
设备台账：初步后端化，但设备型号字典仍需后端化
任务体系：仍未后端化，是最大缺口
运行时参数：已有后端服务，但还需要和任务上下文关联
历史/日志/告警：有基础表和日志能力，但还不是完整业务闭环
Web API：功能可用，但 WebApiController 已偏大
AppController：作为组合根可接受，但继续膨胀会有风险
```

### 已经比较稳的基座

#### 1. 航迹显示链路

当前链路已经清晰：

```text
RadarDataProcessor
  -> TrackDisplayManager
  -> SystemBus(BusEnvelope: TrackDisplaySnapshot)
  -> TrackDisplayWebBridge
  -> React Web
```

UI 交互命令也已经闭环：

```text
React Web
  -> WebApiController
  -> SystemBus(BusEnvelope: TrackInteractionCommand)
  -> TrackDisplayManager
  -> snapshot 回推
```

这块不要再大改主链路，只做性能/测试/小护栏即可。

#### 2. 总线载荷模型

`DataPacket` 和 `BusEnvelope` 的边界已经比较清楚：

- `DataPacket`：小包、固定协议、通信下行、运行参数变更；
- `BusEnvelope`：大对象、Web/UI DTO、航迹 snapshot、交互命令。

这个设计可以继续作为后续任务状态、告警批量、历史回放的基础。

#### 3. 设备台账

后端 `DeviceRegistry + SQLite` 作为 source of truth 的方向是对的，当前前端已经开始使用后端返回的完整 device DTO，不再自己生成最终 id。设备和端点通过 `endpointId` 绑定，这也符合之前定的边界。

### 当前主要风险

#### 1. TaskService 缺失

任务仍在前端 `localStorage`：

```ts
localStorage.getItem(TASK_STORAGE_KEY)
localStorage.setItem(TASK_STORAGE_KEY, ...)
```

这和当前架构文档里的任务上下文不一致。只要任务还在前端，下面这些能力都无法真正稳定：

- 任务关联设备；
- 任务状态机；
- 任务历史航迹；
- 操作日志；
- 任务上下文过滤；
- 任务切换时加载设备/历史/日志。

这是下一阶段 P1。

#### 2. WebApiController 继续膨胀

`WebApiController` 已经负责设备、状态、日志、启停、运行参数、航迹交互等 route。短期可用，但如果 Task API 继续塞进去，会很快变成维护压力。

TaskService 开始实现时，建议同步拆 route：

```text
DeviceApiRoutes
TaskApiRoutes
TrackApiRoutes
RuntimeSettingsApiRoutes
SystemStatusApiRoutes
```

不一定一次全拆，但新增 Task API 不要继续直接堆到 `registerRestRoutes()` 里。

#### 3. 设备型号字典仍在前端

品牌现在统一了，但还不是最终状态。业务上设备型号、厂家、默认阵面、默认参数都应该由后端提供。前端只负责展示和选择。

#### 4. 测试仍不足

现在链路越来越清晰，也意味着可以开始补最小测试了。最值得测的是：

- `SystemBus` envelope 订阅匹配；
- `TrackDisplayManager` interaction command；
- `DeviceRegistry` 默认设备 seed / legacy brand；
- `TaskService` 后续状态机。

## 给 Cursor 的下一阶段工作安排

### P0：清理本轮小问题

1. 清理 `ui/src/lib/devicesApi.ts` 的尾部空白。
2. 确认 `core/web/WebApiController.cpp` 不再有多余反引号。
3. 如果 `doc/review_doc/review-43bab5e.md` 已经被远端纳入，后续拉取前注意不要留下未跟踪副本。

### P1：TaskService 后端化最小闭环

这是下一阶段主线。

#### 目标

把任务从前端 `localStorage` 收回后端，形成最小可用业务闭环。

#### 后端模型

新增：

```cpp
struct TaskInfo {
    QString id;
    QString name;
    QString description;
    QString operatorName;
    QString status; // Draft / Active / Completed / Archived
    QStringList deviceIds;
    qint64 createdAt;
    qint64 activatedAt;
    qint64 completedAt;
    qint64 archivedAt;
};
```

建议先用字符串状态，后续再收成 enum。

#### SQLite 表

建议最小两张表：

```sql
tasks(
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  description TEXT,
  operator_name TEXT,
  status TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  activated_at INTEGER,
  completed_at INTEGER,
  archived_at INTEGER
)

task_devices(
  task_id TEXT NOT NULL,
  device_id TEXT NOT NULL,
  PRIMARY KEY(task_id, device_id)
)
```

#### 后端服务

新增：

```text
core/business/TaskService.h
core/business/TaskService.cpp
```

职责：

- create task；
- list tasks；
- get task；
- activate task；
- complete task；
- archive task；
- delete task；
- update task devices。

#### API

新增：

```text
GET    /api/tasks
POST   /api/tasks
GET    /api/tasks/{id}
PUT    /api/tasks/{id}
POST   /api/tasks/{id}/activate
POST   /api/tasks/{id}/complete
POST   /api/tasks/{id}/archive
DELETE /api/tasks/{id}
```

先不做复杂权限，不做历史回放，只把 source of truth 搬到后端。

#### 前端迁移

替换：

```text
ui/src/lib/taskTypes.ts localStorage
```

新增：

```text
ui/src/lib/tasksApi.ts
```

前端启动时从 `/api/tasks` 加载任务；创建/完成/归档/删除都调用后端 API。可以保留 localStorage fallback，但不能作为主路径。

### P2：拆 Web API route 模块

在实现 Task API 时，不建议继续把所有 lambda 塞进 `WebApiController::registerRestRoutes()`。

建议先拆：

```text
core/web/DeviceApiRoutes.h/.cpp
core/web/TaskApiRoutes.h/.cpp
core/web/TrackApiRoutes.h/.cpp
```

如果一次拆太多，可以先只拆 `TaskApiRoutes`，让新增功能不要继续扩大 `WebApiController`。

### P3：设备型号字典后端化

在 TaskService 之后做。

新增：

```text
GET /api/device-models
```

数据可以先来自配置文件：

```text
config/device_models.json
```

内容包括：

- brand；
- brandName；
- model；
- kind；
- defaultFaces；
- defaultMaxRange；
- defaultRuntimeSettings 可后续加。

前端品牌/型号选项从 API 加载，`BRAND_OPTIONS` 只作为 fallback。

### P4：最小测试

优先补：

1. `TaskService` 状态流转测试；
2. `DeviceRegistry` legacy brand / default radar 测试；
3. `SystemBus` envelope 订阅匹配测试；
4. `TrackDisplayManager` interaction command 测试。

## 最终建议

当前不要再继续围着航迹链路细修了。航迹链路已经是目前项目里最清楚的一块；继续投入的边际收益不高。

接下来应该把“任务”这个业务基座做出来。因为任务一旦后端化，设备、航迹、日志、历史、运行参数都会有共同上下文，整个系统才会真正从“上位机功能集合”变成“管理系统”。
