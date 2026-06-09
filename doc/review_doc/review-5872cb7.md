# Review: 5872cb7

Commit: `5872cb7` (`b6303c5修改完成`)

## 总体判断

这版方向是对的，已经把上一轮最关键的“前端默认设备兜底”往回收了：

- `devices` 初始值已经改为空数组，不再启动时直接塞 `DEFAULT_DEVICE`。
- 新增雷达默认值已经移除 `endpointId: radar_cpu`。
- Sidebar 增加了 `loading / empty / unavailable` 提示。
- checkbox 对不可控雷达做了禁用。
- `doc/business/设备台账与端点映射.md` 已经改成后端 `DeviceRegistry` 是业务设备台账 source of truth。

但这版有一个会直接导致前端编译失败的问题，另外 `deviceLoadState` 还只是启动加载状态，没有和增删设备后的状态同步。

## P0 - `DeviceLoadState` 没有导入，前端会编译失败

文件：`ui/src/App.tsx:4`

当前代码只默认导入了 `Sidebar`：

```ts
import Sidebar from './components/layout/Sidebar'
```

但后面使用了：

```ts
const [deviceLoadState, setDeviceLoadState] = useState<DeviceLoadState>('loading')
```

`DeviceLoadState` 是在 `Sidebar.tsx` 中 export 的 type，但 `App.tsx` 没有导入它。TypeScript 会报：

```text
Cannot find name 'DeviceLoadState'.
```

### 建议改法

短期直接修：

```ts
import Sidebar, { type DeviceLoadState } from './components/layout/Sidebar'
```

更推荐的结构是把跨组件共享 type 放到独立文件，例如：

```text
ui/src/lib/deviceLoadState.ts
```

或放入设备 API/model 层，而不是从 `components/layout/Sidebar` 反向导出给 `App` 用。`App` 是页面状态所有者，Sidebar 是展示组件；共享状态类型不应该由 Sidebar 定义。

## P0 - `App.tsx` 保留了未使用 import/function，严格 TS 配置会失败

文件：`ui/src/App.tsx:9`

当前仍然导入了 `DEFAULT_DEVICE`：

```ts
import { type AppDevice, type RadarDevice, type SubDevice, DEFAULT_DEVICE } from './components/map/types'
```

但 `DEFAULT_DEVICE` 已经不再使用。

文件：`ui/src/App.tsx:17`

当前仍然保留：

```ts
function nextDeviceId() { return `dev_${Date.now()}_${++deviceIdCounter}` }
function nextSubId() { return `sub_${Date.now()}_${++deviceIdCounter}` }
```

这两个函数也已经不再使用。

项目 `ui/tsconfig.app.json` 开启了：

```json
"noUnusedLocals": true
```

所以即使补了 `DeviceLoadState` import，这些未使用项也会继续导致构建失败。

### 建议改法

删除 `DEFAULT_DEVICE` import，并删除 `nextDeviceId()` / `nextSubId()`。`deviceIdCounter` 仍被 `nextTaskId()` 使用，可以保留。

## P1 - `deviceLoadState` 没有随增删设备同步，状态机会显示错误

文件：`ui/src/App.tsx:103`

当前添加设备成功后只做：

```ts
setDevices(prev => [...prev, radar])
```

如果启动时后端台账为空，`deviceLoadState` 会是 `empty`。用户添加第一台设备成功后，设备列表会出现新设备，但 `deviceLoadState` 仍然是 `empty`，Sidebar 仍显示：

```text
后端台账为空，请添加设备
```

删除最后一台设备时也有相同问题：列表已经空了，但 `deviceLoadState` 仍可能保持 `ready`。

### 建议改法

设备增删成功后同步维护状态：

```ts
setDeviceLoadState('ready')
```

删除时根据剩余设备数量判断：

```ts
setDevices(prev => {
  const next = prev.filter(d => d.id !== id)
  setDeviceLoadState(next.length === 0 && subDevicesRef.current.length === 0 ? 'empty' : 'ready')
  return next
})
```

更好的做法是把 `empty/ready` 从 `devices + subDevices` 派生出来，只把 API 加载是否失败作为独立状态：

```ts
type DeviceRegistryState = 'loading' | 'available' | 'unavailable'
const isEmpty = devices.length === 0 && subDevices.length === 0
```

这样不会出现“有设备但状态还是 empty”的不一致。

## P1 - `unavailable` 状态仍允许添加设备，语义不清

文件：`ui/src/components/layout/Sidebar.tsx:231`

当前 `unavailable` 只显示：

```text
后端设备台账不可用，设备管理功能受限
```

但下面的“添加设备”按钮和表单仍然可用。添加时会调用后端 API，大概率失败；用户体验上会变成“提示不可用，但还让我填表提交”。

### 建议改法

当 `deviceLoadState === 'unavailable'` 时：

1. 禁用“添加设备”按钮。
2. 不展开新增表单。
3. 提示“请先恢复后端连接/设备台账服务”。

只有 `empty` 和 `ready` 允许新增设备。

## P2 - `DeviceLoadState` 不建议定义在 Sidebar 组件文件中

文件：`ui/src/components/layout/Sidebar.tsx:19`

现在：

```ts
export type DeviceLoadState = 'loading' | 'ready' | 'empty' | 'unavailable'
```

这个 type 是应用层设备台账加载状态，不是 Sidebar 私有类型。让 `App.tsx` 从 Sidebar 导入它，会让状态所有者反向依赖展示组件。

### 建议改法

迁移到更中性的 model 文件：

```text
ui/src/lib/deviceTypes.ts
```

或：

```text
ui/src/lib/devicesApi.ts
```

由 `App.tsx` 和 `Sidebar.tsx` 分别 import。

## P2 - 设备台账文档存在重复章节

文件：`doc/business/设备台账与端点映射.md`

文档现在有两处“设备身份规则”：

1. 新增的 `### 设备身份规则`
2. 原文件末尾的 `## 设备身份规则`

内容高度重复。虽然不影响代码，但架构文档是后续实现准绳，重复规则容易在下一轮修改时产生分叉。

### 建议改法

保留一处即可。建议把规则放在“概述 / 设备存储位置”之后，删除末尾重复章节。

## 已确认修复项

- 正式设备列表初始值已经从本地默认设备改为空数组。
- `/api/devices` 空结果会进入 `empty`。
- `/api/devices` 请求失败会进入 `unavailable`。
- 新增雷达默认不再绑定 `radar_cpu`。
- checkbox 已按 `backendManaged === true && !!endpointId` 禁用不可控雷达。
- 设备台账文档已经改为后端 `DeviceRegistry + SQLite devices` 模型。

## 未验证项

本机仍无法运行前端构建：

```powershell
npm run build
```

原因同上一轮：当前环境 `npm` 不在 PATH，且 `ui/node_modules` 不存在。

但基于源码和 `tsconfig.app.json`，这版存在确定的 TypeScript 编译问题：`DeviceLoadState` 未导入，以及若干未使用项会触发 `noUnusedLocals`。

Claude 修完后必须跑：

```bash
cd ui
npm run build
```

## 给 Claude 的下一步执行清单

1. 修复 `App.tsx` 中 `DeviceLoadState` 的 type import。
2. 删除 `DEFAULT_DEVICE` import、`nextDeviceId()`、`nextSubId()`。
3. 让 `deviceLoadState` 随新增/删除设备同步，或把 `empty/ready` 改成由设备数组派生。
4. `unavailable` 状态禁用新增设备入口。
5. 把 `DeviceLoadState` 移到中性的 model/type 文件，避免 App 从 Sidebar 导入状态类型。
6. 删除 `doc/business/设备台账与端点映射.md` 中重复的“设备身份规则”章节。

## 架构结论

Milestone 1 的方向已经明显好转：设备列表不再默认展示前端伪设备，后端台账的 source of truth 基本立住了。

但这一版还不能合格交付，因为前端会编译失败。修完 P0 后，再把设备台账状态机补成“可用性状态 + 是否为空”的可靠模型，Milestone 1 就可以接近收口。
