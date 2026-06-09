# Review: b6303c5

Commit: `b6303c5` (`review-b9e243a修改完成`)

## 总体判断

这版是在修 `review-b9e243a`，后端两个局部问题修得比较到位：

- `DeviceRegistry.h` 的默认可控雷达注释已经改成新模型：`id=radar_001, endpointId=radar_cpu`，同时承认历史库可能存在 `id=radar_cpu`。
- `DeviceRegistry::createAssigningId()` 已经不再对任意 `create()` 失败都盲目换 id 重试。

但是设备管理的核心问题还没有完全收束：前端仍然会在启动初始态静默创建一个本地默认雷达，只有 `/api/devices` 成功且返回非空时才替换成后端台账。也就是说，UI 现在能显示一部分“后端身份”，但还没有形成“后端台账驱动 UI”的稳定状态机。

这会继续造成用户看到的设备、后端 SQLite `devices` 表、端点绑定、控制链路之间不够透明。

## P1 - 前端启动仍静默显示本地默认设备，破坏后端台账 source of truth

文件：`ui/src/App.tsx:23`

当前代码：

```ts
const [devices, setDevices] = useState<RadarDevice[]>(() => [
  { id: nextDeviceId(), ...DEFAULT_DEVICE },
])
```

随后加载后端：

```ts
fetchDevices()
  .then(({ radars, subs }) => {
    if (radars.length > 0 || subs.length > 0) {
      setDevices(radars)
      setSubDevices(subs)
    }
  })
  .catch(() => {})
```

问题在于：

1. 后端 API 失败时，UI 静默保留 `dev_xxx + DEFAULT_DEVICE`。
2. 后端返回空台账时，UI 也静默保留本地默认设备。
3. 这个本地默认设备带 `endpointId: radar_cpu`，用户可以勾选它并尝试控制。
4. 但它不是 `DeviceRegistry` 确认过的业务设备 id，后端 `/api/start` 会拒绝它。

这和我们定下的架构原则冲突：前端临时 id 只能存在于“未保存草稿”，不能进入正式设备列表、任务关联和控制链路。

### 建议改法

把设备加载改成明确的状态机，而不是默认塞一条设备：

```ts
type DeviceLoadState = 'loading' | 'ready' | 'empty' | 'unavailable'
```

推荐规则：

1. `devices` 初始值为 `[]`，不要放 `DEFAULT_DEVICE`。
2. `/api/devices` 成功后：
   - 有设备：`state=ready`，显示后端设备。
   - 空数组：`state=empty`，显示“后端台账为空”，引导用户添加设备。
3. `/api/devices` 失败：
   - `state=unavailable`，显示“后端设备台账不可用”。
   - 禁止采集控制。
   - 禁止把 fallback 设备当成正式设备加入任务。
4. `DEFAULT_DEVICE` 只允许作为“新增设备表单默认值”，不能作为已注册设备显示。

这一步比继续补字段更重要，它决定 UI 里的设备到底是不是后端确认过的业务对象。

## P1 - `backendManaged` 只覆盖 API 成功路径，无法表达 fallback / unavailable

文件：`ui/src/lib/devicesApi.ts:30`

这版在 `apiDeviceToRadar()` 中加了：

```ts
backendManaged: true
```

这只能说明“由 `/api/devices` 映射出来的设备来自后端”。它不能表达整页设备列表的来源状态，也不能覆盖 API 失败或空台账时的 UI 状态。

现在 `RadarEditForm` 只有在 `device.backendManaged` 为 true 时显示：

```tsx
数据来源: 后端台账
```

但如果是本地默认设备，它不会显示“本地临时/fallback/后端不可用”，用户仍然无法判断当前设备是否可信。

### 建议改法

不要只在单个 `RadarDevice` 上放一个可选布尔值。需要页面级的数据源状态：

```ts
type DeviceSourceState =
  | { kind: 'loading' }
  | { kind: 'backend' }
  | { kind: 'empty' }
  | { kind: 'unavailable'; error: string }
```

UI 行为：

- `backend`：设备可编辑、可勾选、可控制。
- `empty`：列表为空，允许添加设备，不能显示假设备。
- `unavailable`：显示后端不可用，禁用采集按钮或禁止勾选设备。
- 本地新增表单中的草稿不进入 `allDevices`。

`backendManaged?: boolean` 可以保留作显示辅助，但不能替代上面的 source state。

## P1 - 设备业务文档仍写旧状态，会误导后续实现

文件：`doc/business/设备台账与端点映射.md:5`

当前文档仍写：

> 当前设备信息主要在前端内存 / localStorage 中，后端只知道 `endpoints.json` 里的通信端点。

这已经不符合当前架构目标，也不符合现有代码方向。现在应该明确：

- `endpoints.json` 只描述通信端点。
- 业务设备台账在后端 `DeviceRegistry`。
- 持久化位置由 `config/database.json` 指向 SQLite。
- 设备表是 `devices`。
- 前端设备列表必须来自 `/api/devices`。

### 建议改法

把文档开头改成当前架构事实，并补一个“排查入口”章节：

```md
## 设备存储位置

- 数据库配置：`config/database.json`
- 默认 SQLite：`data/antidrone.db`
- 设备表：`devices`
- 后端归口：`core/business/DeviceRegistry`

常用排查 SQL：

```sql
SELECT id, name, kind, brand, endpoint_id, linked_radar_id, status
FROM devices
ORDER BY created_at;
```
```

这份文档要成为 Claude 后续改设备管理时的准绳，不能继续保留“前端内存/localStorage 是主要设备信息”的旧表述。

## P2 - 新增雷达表单默认绑定 `radar_cpu`，容易制造多个可控雷达

文件：`ui/src/components/layout/Sidebar.tsx:38`

当前新增雷达默认值：

```ts
const RADAR_DEFAULTS: Omit<RadarDevice, 'id'> = {
  ...
  endpointId: 'radar_cpu',
}
```

这会导致用户每新增一个雷达，默认都绑定同一个通信端点 `radar_cpu`。后端 `radarCpuDeviceIds()` 会把所有 `endpointId=radar_cpu` 的设备都视为可控雷达映射，这在单雷达调试阶段能跑，但业务含义不清：

- 是新增了一台真实雷达？
- 还是同一台雷达的另一个业务别名？
- 多个业务设备绑定同一端点时，哪个显示运行状态？

### 建议改法

在还没有 endpoint 绑定 UI 之前，新增雷达默认应为未绑定：

```ts
endpointId: undefined
```

然后明确显示“未绑定端点，不可采集”。只有 seed 出来的默认后端设备 `radar_001` 才绑定 `radar_cpu`。

后续再做 endpoint 绑定 UI：

- 下拉加载 `/api/endpoints` 或后端提供的端点列表。
- 显示端点占用状态。
- 对同一 endpoint 是否允许绑定多个业务设备给出明确规则。

## P2 - 勾选控制仍未在前端过滤非后端设备 / 未绑定设备

文件：`ui/src/components/layout/Sidebar.tsx:236`

现在所有雷达设备都能勾选：

```tsx
<input type="checkbox" checked={checked}
  onChange={() => toggleDeviceCheck(d.id)}
/>
```

即便设备不是后端台账设备，或者没有 `endpointId`，前端也允许勾选。最终只能等 `/api/start` 返回拒绝。

### 建议改法

前端不应该替代后端做最终鉴权，但可以做业务可用性提示：

```ts
const controllable = d.backendManaged === true && !!d.endpointId
```

行为建议：

- `controllable=false` 时禁用 checkbox。
- 列表显示“未绑定端点 / 不可采集”。
- 点击采集前，如果选中设备被拒绝，用后端 `rejectedDeviceIds` 做二次提示。

这样用户能在操作前知道为什么设备不可控，而不是等控制失败。

## P2 - `createAssigningId()` 的 collision 判断仍依赖事后 `get()`，建议后续补错误类型

文件：`core/business/DeviceRegistry.cpp:144`

这版已经比上版好：失败后先 `get(dev.id)`，如果不是 id collision 就停止重试。

但这个判断仍然是间接的。如果 `create()` 失败后数据库连接异常，`get()` 也可能返回空，从而提前 break；这符合“不盲目重试”的方向，但日志里仍只会说“重试耗尽”，定位不够清楚。

### 建议改法

短期可接受，不必马上大改。后续如果继续加强 `DataStore`，建议让 `insert()` 返回更明确的错误类型：

```cpp
enum class StoreError {
    None,
    PrimaryKeyConflict,
    ConstraintViolation,
    ConnectionError,
    SqlError,
};
```

然后 `createAssigningId()` 只对 `PrimaryKeyConflict` 生成新 id。

## 已确认修复项

- `DeviceRegistry.h` 注释已从旧的 `id=radar_cpu` 模型改为 `id=radar_001, endpointId=radar_cpu` 新模型。
- `apiDeviceToRadar()` 已对 API 返回设备标记 `backendManaged: true`。
- `RadarEditForm` 已显示后端设备 ID、通信端点、数据来源。
- `Sidebar` 已显示 `端点 radar_cpu` 或 `未绑定端点`。
- `createAssigningId()` 已避免对明显非 collision 的失败持续换 id 重试。

## 未验证项

尝试运行前端构建：

```powershell
npm run build
```

本机环境中 `npm` 不在 PATH，且 `ui/node_modules` 不存在，因此没有完成 TypeScript/Vite 构建验证。建议 Claude 在他的实现环境里必须跑一次：

```bash
cd ui
npm install
npm run build
```

## 给 Claude 的下一步执行清单

1. 移除 `App.tsx` 中正式设备列表的 `DEFAULT_DEVICE` 初始值，改成空数组 + `deviceLoadState`。
2. `/api/devices` 成功空数组时显示“后端台账为空”，不要 fallback 出默认雷达。
3. `/api/devices` 失败时显示“后端设备台账不可用”，禁用采集控制。
4. `DEFAULT_DEVICE` 只用于新增表单默认值，不进入 `allDevices`。
5. 新增雷达默认不要绑定 `radar_cpu`，除非用户通过端点绑定 UI 明确选择。
6. 禁止未绑定端点或非后端台账设备被勾选采集。
7. 更新 `doc/business/设备台账与端点映射.md`，把设备存储位置、SQLite 表、排查 SQL 写清楚。

## 架构结论

Milestone 1 还不能算完全完成。后端 `DeviceRegistry` 的基座已经起来了，但前端设备列表仍保留“本地默认设备兜底”的旧习惯。

下一步不要急着进入 RuntimeSettings、任务后端化或 TrackDisplayManager。先把设备台账的 source of truth、空状态、失败状态、端点绑定可见性彻底定死。这个基座定住以后，后面的任务、运行参数、航迹显示才能稳定引用同一套设备身份。
