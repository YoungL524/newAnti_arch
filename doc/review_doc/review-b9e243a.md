# Review for b9e243a

Full commit: `b9e243ac87c8eccc6d43179c8bc4cc846c04af16`

Commit message: `review-d26e94b修改完成`

## Findings

### P1 - UI 仍看不到后端设备身份和 endpoint 绑定，用户无法确认“前后端是否对应”

- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 104-180
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 330-465
- File: `ui/src/components/map/types.ts`
- Lines: 16-25
- File: `ui/src/lib/devicesApi.ts`
- Lines: 30-45

这版后端修得比较扎实：`createAssigningId()` 已经进入 `DeviceRegistry`，`POST /api/devices` 也不再自己生成 id。

但 owner 这轮问的是“UI 设备信息好像还是没和后端对应”。这个问题还没有解决。原因是 UI 仍然只展示：

```text
品牌: 中电38所
设备名称: NODE_ALPHA_01
位置/阵面
```

却没有展示最关键的后端身份字段：

```text
后端 deviceId
endpointId
数据来源是否来自 /api/devices
```

所以即使当前设备确实来自后端台账，用户也看不出来；如果它来自前端 fallback，用户也看不出来。这会继续造成“UI 和后端完全不是一回事”的感受。

建议 Claude Code 下一轮先做 UI 可观察性，不要继续只改后端：

1. `RadarEditForm` 的编辑弹窗顶部增加只读“设备身份”区：

```text
后端设备 ID: radar_001
通信端点: radar_cpu
数据来源: 后端台账
```

2. `Sidebar` 设备卡片在副标题中显示 endpoint 绑定，例如：

```text
中电38所 · 1面 · 5km · 端点 radar_cpu
```

3. 如果设备没有 `endpointId`，显示：

```text
未绑定端点
```

并禁止勾选采集或在 start 前给出明确提示。

4. 如果 `/api/devices` 加载失败，不要静默保留前端 fallback 并让用户以为这是后端设备。应显示“后端设备台账未连接”。

### P1 - 设备台账文档仍没有说明“后端设备存储在哪里、如何查看”

- File: `doc/business/设备台账与端点映射.md`
- Lines: 1-92
- File: `config/database.json`
- Lines: 1-4

后端业务设备实际存储位置是：

```text
config/database.json -> data/antidrone.db -> devices table
```

`endpoints.json` 只描述通信端点，不存业务设备。

但正式业务文档仍没有“如何查看当前后端设备表”的说明。对于现在这个阶段，这个说明很重要，因为我们正在处理前后端设备身份对齐问题。

建议补一节：

```text
## 设备存储位置与排查

SQLite: config/database.json 中的 database 字段
默认: data/antidrone.db
表名: devices
关键字段: id, name, kind, brand, endpoint_id, faces_json

排查:
SELECT id, name, kind, brand, endpoint_id FROM devices;
```

并说明：UI 显示的设备必须来自 `/api/devices` 返回的 `devices` 表数据，除非明确进入 fallback/demo 模式。

### P2 - `ensureDefaultControllableRadar()` 注释仍写旧模型，和当前 seed 不一致

- File: `core/business/DeviceRegistry.h`
- Lines: 50-53
- File: `core/business/DeviceRegistry.cpp`
- Lines: 87-111

头文件注释仍然写：

```cpp
/// 确保至少有一条可控雷达设备（id=radar_cpu, endpointId=radar_cpu）
```

但当前新 seed 实际是：

```text
id=radar_001
endpointId=radar_cpu
```

这类注释不准会继续误导后续实现者，把业务设备 id 和通信端点 id 混为一谈。

建议改为：

```cpp
/// 确保至少有一条绑定 radar_cpu 端点的可控雷达业务设备。
/// 新库默认创建 id=radar_001, endpointId=radar_cpu。
/// 历史库可能存在 id=radar_cpu 的 legacy 设备。
```

### P2 - `createAssigningId()` 会对任何 `create()` 失败重试，可能掩盖非 id 冲突错误

- File: `core/business/DeviceRegistry.cpp`
- Lines: 138-155

`createAssigningId()` 当前逻辑是：

```cpp
if (create(dev)) return created;
// retry up to 5 times
```

但 `create()` 失败不一定是 id 冲突，也可能是数据库断开、字段约束、SQL 错误、schema 异常。现在会把所有失败都当成 id collision 重试，最终只输出“重试耗尽”。

建议：

1. 最小改法：重试前先 `get(dev.id)`，只有 id 已存在才换 id 重试。
2. 如果 `get(dev.id)` 不存在但 insert 失败，应立即返回错误，不要重试。
3. 更好：让 `DataStore::insert` 暴露最近错误，区分 constraint 和其他 DB 错误。

### P2 - `/api/devices` 返回 DTO 仍没有 `backendManaged/source` 字段

- File: `core/web/WebApiController.cpp`
- Lines: 96-122
- File: `ui/src/lib/devicesApi.ts`
- Lines: 10-16

当前前端可以从 `/api/devices` 得到 `endpointId`，但无法直接标记“这个设备来自后端台账”。从数据流上当然 GET 回来的就是后端设备，但 UI 还有 fallback 默认设备，所以前端需要一个清晰状态：

```text
devicesLoadedFromBackend: true/false
```

这个可以不一定放 DTO，也可以在前端 fetch 状态里维护。但需要落地，否则 fallback 设备和后端设备在 UI 上仍然混在一起。

建议：

1. `App.tsx` 增加 `devicesSource: 'backend' | 'fallback' | 'unavailable'`。
2. `RadarDevice` 可选增加 `backendManaged?: boolean`，或 UI 从全局 source 判断。
3. 设备编辑弹窗显示来源。

## Fixed Since Previous Review

- `DeviceRegistry::createAssigningId()` 已新增，id 为空时由后端生成，且最多重试 5 次。
- `WebApiController` 的 `POST /api/devices` 已改为调用 `createAssigningId()`，不再自己生成 id。
- 历史 `id=radar_cpu` 默认设备会输出 `qWarning`，比之前不可见更好。
- 后端 id 生成已从局部 static counter 移到 `DeviceRegistry::generateId()`。

## Assessment

这轮解决的是“后端创建契约”的问题，但没有解决 owner 当前看到的“UI 信息和后端不对应”的问题。

现在的事实是：

```text
后端设备位置:
  SQLite devices 表

UI 当前显示:
  设备名称 / 品牌 / 阵面

缺失显示:
  后端 device id
  endpointId
  数据来源
```

所以用户仍然无法判断 `NODE_ALPHA_01` 是后端 `radar_001`，还是前端 fallback 设备，还是旧库里的 legacy `radar_cpu`。

## How Claude Code Should Proceed

下一轮请只做“设备身份可见化”：

1. 前端记录设备数据来源：

```text
backend: /api/devices 成功返回
fallback: 后端不可用或返回空时的演示数据
```

2. 设备编辑弹窗显示：

```text
后端设备 ID
通信端点 endpointId
数据来源
```

3. Sidebar 设备项显示 endpoint 状态：

```text
端点 radar_cpu
或 未绑定端点
```

4. 未绑定端点设备不允许采集，提示“设备未绑定通信端点”，不要再泛化成“不支持设备 id”。
5. 文档补“设备存储位置与排查”。

这一步完成后，owner 才能直观看到 UI 和后端是否对应。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 90%。

后端契约已接近稳定，但 UI 可观察性没有完成，所以不能关。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。继续等待。

### Milestone 3 - Track Display Data Management

进度：未开始。

### Milestone 4 - Task Backend Domain

进度：未开始。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Verification

- 本轮为源码审查。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
