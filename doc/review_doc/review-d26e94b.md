# Review for d26e94b

Full commit: `d26e94b9bb746d6768a30629ecdc79f169f280c6`

Commit message: `review-1833fbc修改完成`

## Findings

### P1 - 用户仍无法确认 UI 设备是否来自后端台账，需要提供设备台账可观察入口

- File: `config/database.json`
- Lines: 1-4
- File: `core/business/DeviceRegistry.cpp`
- Lines: 53-111
- File: `core/AppController.cpp`
- Lines: 60-72
- File: `ui/src/App.tsx`
- Lines: 35-46

先回答 owner 的问题：**后端业务设备不在 `endpoints.json` 里**。

当前后端设备放在：

```text
SQLite database:
  config/database.json -> data/antidrone.db

table:
  devices

owner:
  core/business/DeviceRegistry
```

`endpoints.json` 只描述通信端点，例如：

```text
endpoint id = radar_cpu
UDP localPort / targets / description
```

业务设备应该在 SQLite `devices` 表，例如：

```text
device id = radar_001
name = NODE_ALPHA_01
endpointId = radar_cpu
brand/model/faces/location...
```

但当前用户从 UI 上无法判断这个设备到底是：

- 后端 `/api/devices` 返回的真实台账设备；
- 前端 fallback 默认设备；
- 旧数据库里的历史设备；
- 另一个运行目录下 SQLite 的设备。

我在当前工作区没有找到 `data/antidrone.db`，说明截图里的运行实例很可能用了另一个启动目录/配置目录下的数据库，或者本工作区尚未生成 DB。这会让“UI 和后端是否对应”只能靠猜。

建议 Claude Code 优先补一个可观察入口：

1. 后端启动日志已经打印设备列表，这是好的，但还不够。
2. 在 UI 设备编辑/详情中显示只读字段：

```text
后端设备 ID: radar_001
通信端点: radar_cpu
数据来源: 后端台账 / 前端 fallback
```

3. 如果 `/api/devices` 加载失败或返回空，不要静默显示可控 fallback；应显示“未连接后端设备台账”。
4. 在 `/api/status` 或 `/api/devices` 中补 `source` / `backendManaged` 这类前端可判断字段，或者前端用 fetch 成功状态判断。
5. 文档里补一节“如何查看当前后端设备表”，例如 SQLite 路径和 `SELECT * FROM devices`。

### P1 - `ensureDefaultControllableRadar()` 仍接受历史 `id=radar_cpu`，不同数据库会出现不同默认业务设备 id

- File: `core/business/DeviceRegistry.cpp`
- Lines: 87-97

这版把 id 生成移到了 `DeviceRegistry::generateId()`，是对的。但默认设备迁移问题仍在。

当前：

```cpp
if (d.id == "radar_cpu" || d.endpointId == "radar_cpu") {
    return true;
}
```

这意味着：

- 新库：会 seed `id=radar_001, endpointId=radar_cpu`；
- 旧库：如果已有 `id=radar_cpu, endpointId=radar_cpu`，就不会迁移；
- 用户看到的 UI 设备 id 和日志里的 device id 会因数据库历史不同而不同。

如果我们已经决定“业务设备 id 不应等于通信端点 id”，那就不能继续把旧 `id=radar_cpu` 当成完全正常的目标状态。

建议 Claude Code 二选一，必须明确：

1. **兼容策略**：允许 `id=radar_cpu` 作为历史业务设备 id，但 UI/文档说清楚它是 legacy device id。
2. **迁移策略**：启动时创建 `radar_001`，把旧 `radar_cpu` 的必要字段迁过去，并后续让 UI 使用 `radar_001`。

短期推荐兼容但可见：保留旧数据不强迁移，但 UI 和日志显示 `legacyId=true` 或启动日志明确“当前默认设备 id=radar_cpu（历史数据）”。

### P2 - `generateId()` 已移到 DeviceRegistry，但仍缺少冲突重试

- File: `core/business/DeviceRegistry.cpp`
- Lines: 53-63
- File: `core/web/WebApiController.cpp`
- Lines: 145-153

这版修正了上一轮最关键的问题：id 生成不再放在 `WebApiController` 局部 static counter，而是：

```cpp
DeviceRegistry::generateId(d.kind)
```

并使用：

```cpp
currentMSecsSinceEpoch + random(0..9999)
```

这已经比上一版稳定很多。但 `POST /api/devices` 生成后直接 create，如果极小概率撞 id，会返回“创建设备失败”，没有重试。

建议把生成和 create 合在 DeviceRegistry 内部，例如：

```cpp
std::optional<DeviceInfo> DeviceRegistry::createAssigningId(DeviceInfo dev)
```

内部最多重试 5 次，确保 API 层拿到的是“创建成功后的设备”或明确错误。

### P2 - `WebApiController` 仍保留设备创建校验和 DTO 逻辑，边界还没完全瘦下来

- File: `core/web/WebApiController.cpp`
- Lines: 96-190

Claude 的总结里说 WebApiController 是 API 表现层，这仍然只是目标。现在它虽然不再直接写 id 生成规则，但仍然负责：

- `DeviceInfo -> QJsonObject`；
- `body -> DeviceInfo`；
- validation；
- create/update/delete route；
- start/stop command plan；
- status merge。

建议下一步不要急着抽所有东西，但至少先做两个小 helper：

```cpp
DeviceDtoMapper
DeviceValidator
```

或者放在 `DeviceRegistry` 旁边，别继续让 WebApiController 膨胀。

### P2 - 设备业务信息仍未和 Endpoint/Model 在 UI 上解释清楚

- File: `ui/src/components/dialogs/RadarEditForm.tsx`
- Lines: 104-180
- File: `ui/src/components/layout/Sidebar.tsx`
- Lines: 330-465
- File: `doc/business/设备台账与端点映射.md`
- Lines: 1-92

用户截图里仍然看到“品牌=中电38所、设备名=NODE_ALPHA_01”，但看不到：

- 后端 device id；
- 绑定 endpoint id；
- endpoint 描述；
- 该品牌/型号来自哪里。

这会继续让人感觉“UI 信息和后端不是一回事”。

建议下一步 UI 不需要大改，只要增加一个只读信息块：

```text
设备身份
  后端设备 ID: radar_001
  通信端点: radar_cpu（雷达CPU通信）
  厂家/型号: 中电38所 / 默认单面阵
```

这能立刻把业务设备、通信端点、厂家型号三个概念分开。

## Assessment Of Current State

Claude Code 这轮修复方向总体正确：

- `POST /api/devices` 已支持空 id，由后端生成；
- id 生成移到 `DeviceRegistry`；
- 默认 seed 新库使用 `radar_001 -> radar_cpu`；
- 启动日志会打印当前设备台账；
- 代码开始补齐“后端 source of truth”链路。

但 owner 看到的 UI 仍“不像和后端对应”，原因不是单点 bug，而是**缺少可见性**：

```text
后端确实有 DeviceRegistry
但 UI 没显示后端 device id / endpoint binding / source
开发者也没有一个清晰入口查看 SQLite devices 表
旧数据库还可能保留 legacy id
```

所以现在最重要的不是继续抽象，而是让设备身份变得可观察、可解释、可验证。

## How Claude Code Should Proceed

下一轮请按这个顺序做：

### Step 1 - Make Backend Device Table Visible

1. 在设备编辑弹窗显示只读：

```text
后端设备 ID
通信端点
端点说明
```

2. 如果设备来自前端 fallback，明确显示“未连接后端台账”，并禁止采集。
3. 在 `doc/business/设备台账与端点映射.md` 增加“设备存储位置”：

```text
config/database.json -> data/antidrone.db -> devices table
```

4. 增加一个简单调试 API 可选：

```text
GET /api/devices/debug
```

也可以暂时不加 API，只靠 `/api/devices` 和 UI 显示。

### Step 2 - Decide Legacy Default Device Policy

明确旧 `id=radar_cpu` 怎么办：

- 如果保留，标记为 legacy；
- 如果迁移，写幂等迁移逻辑；
- 不要让不同数据库有不同“默认设备身份”却没有任何提示。

### Step 3 - Endpoint Binding UI

编辑/新增雷达表单加 endpoint 绑定：

```text
通信端点: radar_cpu - 雷达CPU通信
```

短期可以只读或单选，不必做复杂。

### Step 4 - Then DeviceModel

DeviceModel 下一步再做，但要先把 UI 上“品牌”改成“厂家/型号”的概念。

## Refactor Progress Recap From `review-6bcbfc2`

### Milestone 1 - Device Registry As Source Of Truth

进度：约 90%，但还不能关闭。

底层链路基本可用，剩下的是“可观察性 + legacy 策略 + endpoint UI”。这些做完才算真正可控。

### Milestone 2 - Runtime Settings / DPH Event Update

进度：未开始。继续等待。

### Milestone 3 - Track Display Data Management

进度：未开始。

### Milestone 4 - Task Backend Domain

进度：未开始。

### Milestone 5 - Fixed Core-Web Track Protocol

进度：文档方向已定，代码未开始。

## Verification

- 本轮为源码审查，并结合用户截图分析。
- 确认后端设备表设计位置：`config/database.json` -> `data/antidrone.db` -> `devices` table。
- 当前工作区未发现 `data/antidrone.db`，说明截图中的运行实例可能使用另一个启动目录/数据库，或当前工作区尚未生成 DB。
- 当前环境 PATH 中没有 `npm`，未运行前端 build。
- 未运行 CMake。
