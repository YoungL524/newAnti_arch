# Review 682b6ac

## 结论

本轮先检查了配置，再检查代码。

静态配置文件之间没有发现直接冲突：

- `config/endpoints.json`：接收端点为 `radar_cpu`，UDP `127.0.0.1:7788`
- `config/processor.json`：`endpointId=radar_cpu` 映射 `sourceType=1`
- `config/database.json`：SQLite 为 `data/antidrone.db`
- `ui/vite.config.ts`：HTTP 代理到 `8088`，WS 默认直连当前主机 `8089`

截图也证明协议接收正常：core 已持续处理 `GeneralPackage`，所以“没有航迹”不是 UDP 端口或协议适配配置错误。

当前最直接的根因是：**截图里没有激活任务，而新版后端明确丢弃所有无 active task 的实时航迹。**

截图左侧仍显示“新建任务”；core 日志同时出现：

```text
[RadarDataProcessor] 不属于当前任务，跳过归档 endpoint=radar_cpu device=radar_cpu
```

最新 `TrackDisplayManager::isPointVisible()` 又规定：

```cpp
if (m_activeTaskId.isEmpty())
    return false;
```

因此航迹在进入 SystemBus snapshot / WebSocket 之前已经被后端过滤，前端当然显示点数 0。

## P1. 前后端对“无激活任务”的策略互相矛盾

涉及文件：

- `core/business/TrackDisplayManager.cpp`
- `ui/src/hooks/useCoreApi.tsx`
- `core/web/WebApiController.cpp`

后端策略：

```cpp
if (m_activeTaskId.isEmpty())
    return false;
```

即没有 active task 时不生成实时显示航迹。

前端策略：

```ts
} else if (ready && !currentTaskId) {
  visibleTracks = msg.tracks
  notice = '当前无激活任务，实时航迹未按任务过滤'
}
```

即没有 active task 时仍显示全部实时航迹。

但后端已经发送空 snapshot，所以前端这段逻辑永远救不回来。

### 必须先确定业务规则

基于我们之前确定的“系统由任务驱动”，我建议采用严格规则：

```text
没有 active task：允许查看设备和通信状态，但不允许开始采集，也不显示业务航迹
有 active task：只处理和显示该任务绑定设备的航迹
```

如果采用该规则，Cursor 应修改：

1. `/api/start` 在没有 active task 时直接拒绝，返回明确错误：

```text
请先创建并激活任务
```

2. UI 的“开始采集”按钮在无 active task 时禁用或弹出明确提示。
3. 地图显示明确空状态：

```text
当前无激活任务，航迹显示未启用
```

4. 删除前端“无任务时仍显示全部实时航迹”的假逻辑，避免前后端语义冲突。

如果产品上确实要求“无任务也能临时看实时航迹”，则应反过来修改后端：`TrackDisplayManager` 允许无任务点进入显示层，但不归档。这需要明确命名为临时监视模式，不能靠前端偷偷绕过任务上下文。

## P1. 重新加入单航迹 80 点上限，违背已确认需求

位置：

- `core/business/TrackDisplayManager.cpp`

本轮新增：

```cpp
static constexpr int kMaxPointsPerTrack = 80;
```

以及：

```cpp
if (cache.points.size() > kMaxPointsPerTrack)
    cache.points.remove(0, cache.points.size() - kMaxPointsPerTrack);
```

你之前已经明确要求去掉单航迹点数上限，因此这次修改属于需求回退。

建议 Cursor 删除该限制。

如果担心 WS 帧体积，应从以下方向解决：

- 降低 snapshot 频率
- 只推增量或按时间窗口推送
- 传输层压缩/二进制协议
- 全局背压与可观测指标

不能重新用“每条航迹只留 80 点”改变业务显示语义。

## P1. 运行数据库中的业务设备/任务绑定需要核对

截图日志显示：

```text
endpoint=radar_cpu device=radar_cpu
```

说明当前运行 SQLite 的 endpoint 映射结果是 legacy 业务设备：

```text
radar_cpu -> deviceId radar_cpu
```

仓库默认新设备本应是：

```text
deviceId=radar_001
name=NODE_ALPHA_01
endpointId=radar_cpu
```

但 `DeviceRegistry::ensureDefaultControllableRadar()` 会保留历史 `id=radar_cpu`，不会自动迁移成 `radar_001`。

这本身可以兼容，但必须保证 active task 的 `task_devices.device_id` 与实际 endpoint cache 返回值完全相同。

### 建议先查询运行库

程序实际数据库路径是：

```text
<实际 config 目录的父目录>/data/antidrone.db
```

需要执行或通过调试 API检查：

```sql
SELECT id, name, kind, endpoint_id FROM devices;

SELECT id, name, status FROM tasks;

SELECT task_id, device_id FROM task_devices;
```

重点确认：

```text
devices.endpoint_id = radar_cpu 的那条记录 id 是什么
active task 的 task_devices 是否包含这个完全相同的 id
```

从截图看，当前没有 active task，因此第一步不是改 endpoint 配置，而是创建/激活任务并绑定实际 deviceId。

## P1. TaskService 自动绑定只处理“设备列表为空”，不能修复错误绑定

位置：

- `core/business/TaskService.cpp`
- `TaskService::refreshActiveTaskCache()`

本轮逻辑只有在：

```cpp
m_activeTaskCache.deviceIds.isEmpty()
```

时才自动绑定所有 endpoint-bound radar。

如果任务已经绑定了一个错误、过期或无 endpoint 的设备，则列表不为空，真正的数据源设备仍不会被补上。

建议不要靠这种隐式自动绑定修补数据。

更稳的规则是：

1. 激活任务前校验至少包含一个可控设备。
2. 开始采集前校验所选设备属于 active task，并且 endpoint 映射可解析。
3. 返回明确的 accepted/rejected 设备原因。
4. 不在 `refreshActiveTaskCache()` 这种读路径里偷偷修改数据库。

`refreshActiveTaskCache()` 当前既读缓存又写 task_devices，职责混杂，后续应移除自动写入副作用。

## P1. TrackDisplayManager 没有严格校验点自身 taskId

位置：

- `core/business/TrackDisplayManager.cpp`
- `TrackDisplayManager::isPointVisible()`

当前可见性判断最终是：

```cpp
return m_taskService->isDeviceInActiveTask(pt.deviceId);
```

它只检查设备属于当前任务，没有检查：

```cpp
pt.taskId == m_activeTaskId
```

如果同一设备在任务 A/B 间切换，队列中延迟到达的任务 A 航迹仍可能因为设备也属于任务 B，而被显示到任务 B。

建议严格判断：

```cpp
return !pt.taskId.isEmpty()
    && pt.taskId == m_activeTaskId
    && m_activeDeviceIds.contains(pt.deviceId);
```

空 `taskId` 的兼容补全只能发生一次，并且补全后再按 taskId 判断。

## P2. 配置搜索顺序可能导致读取了另一份 config/database

位置：

- `core/data/ConfigManager.cpp`

配置搜索顺序是：

```text
1. current working directory/config
2. executable directory/config
3. executable directory/../config
```

如果机器上存在多份 build 或旧 config，启动方式不同可能读取不同配置和不同 SQLite。

截图中的运行库不在当前仓库目录下，说明实际 core 很可能从另一个构建/部署目录加载 config。

建议 Cursor/运行日志在启动时明确打印：

- 最终 configDir
- 最终 database absolute path
- endpoint 列表
- endpointId -> business deviceId 映射
- activeTaskId 和 deviceIds

目前已有部分日志，但建议整理成一段启动诊断摘要，避免每次靠猜。

长期建议部署时显式传入 config 目录，减少 current working directory 带来的不确定性。

## P2. WebSocket 配置不是当前主因

`ui/vite.config.ts` 和 `useCoreApi.tsx` 当前分别配置：

```text
HTTP API -> localhost:8088
WS -> ws://<browser-hostname>:8089
```

截图 core 已打印 WS 客户端连接，证明 WS 连接建立成功。

因此本轮不应继续在 Vite proxy 上消耗时间。应优先修任务状态与设备归属。

## P2. 帧裁剪策略仍需后续设计

本轮将：

```cpp
kMaxSnapshotPoints = 600
kSnapshotIntervalMs = 1000
```

并打印大帧 warning。

这不会解释当前完全无航迹，因为当前点在 task visibility 阶段就被过滤了。

但全局 600 点裁剪会影响多航迹公平性，后续应单独设计：

- 按航迹公平采样
- 每条航迹保留最新点但不限制其业务缓存
- 或采用增量推送

不要把性能问题和任务归属问题混在同一轮修。

## 建议 Cursor 修复顺序

1. 明确并固化规则：没有 active task 时 `/api/start` 拒绝，UI 给明确提示。
2. 删除 `kMaxPointsPerTrack = 80`，恢复用户确认的无单航迹点数上限。
3. 用运行库查询确认 `radar_cpu endpoint -> business deviceId -> active task deviceIds` 完全一致。
4. `TrackDisplayManager::isPointVisible()` 严格校验 `pt.taskId == activeTaskId`。
5. 移除 `refreshActiveTaskCache()` 中隐式修改 task_devices 的副作用，改为激活/开始采集时显式校验。
6. 增加启动诊断摘要，打印最终配置路径、数据库路径、endpoint/device/task 映射。

## 当前现场如何快速验证

不改代码也可以先验证根因：

1. 在 UI 创建并激活一个任务。
2. 确保任务绑定截图中已勾选、endpoint 为 `radar_cpu` 的雷达设备。
3. 重新点击开始采集。
4. core 日志不应再出现：

```text
不属于当前任务，跳过归档
```

5. 应看到 TrackDisplay snapshot 中 points 大于 0，UI 底部“显示点数”不再为 0。

如果激活任务后仍提示 `device=radar_cpu` 不属于任务，说明 SQLite 中 task_devices 绑定的 ID 与 endpoint cache 返回的 `radar_cpu` 不一致，需要按上面的三条 SQL 核对运行库。
