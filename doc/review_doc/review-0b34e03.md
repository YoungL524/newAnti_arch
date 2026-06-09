# Review: 0b34e03

Commit: `0b34e03` (`review-41d7f90修改完成`)

## 总体判断

这版基本按 `review-41d7f90` 的方向修了：

- `fetchDevices()` 已检查 `ok`，失败时会进入 `unavailable`。
- 成功路径无论设备数组是否为空，都会 `setDevices(radars)` / `setSubDevices(subs)`，以后端返回为准。
- Sidebar 已区分“当前任务未绑定设备”和“后端台账为空”。
- `unavailable` 时会自动收起新增表单。

但当前提交有一个 P0：`visibleDeviceListEmpty` 在 `activeTask` / `allTaskDevices` 声明之前就被读取，组件首屏渲染会直接触发 temporal dead zone 错误。也就是说，这版方向对，但前端当前不可运行。

## P0 - `visibleDeviceListEmpty` 在 `activeTask` / `allTaskDevices` 初始化前读取，首屏会崩

文件：`ui/src/App.tsx:27`

当前代码：

```ts
const deviceListEmpty = devices.length === 0 && subDevices.length === 0
const visibleDeviceListEmpty = activeTask ? allTaskDevices.length === 0 : deviceListEmpty
```

但 `activeTask` 在后面才声明：

```ts
const activeTask = useMemo(() => tasks.find(t => t.id === activeTaskId) ?? null, [tasks, activeTaskId])
```

`allTaskDevices` 也在更后面才声明：

```ts
const allTaskDevices = useMemo(() => [...taskDevices, ...taskSubDevices], [taskDevices, taskSubDevices])
```

这是 JavaScript 的 temporal dead zone 问题，不是单纯的 TypeScript 类型问题。组件函数执行到 `visibleDeviceListEmpty` 这一行时，`activeTask` 还没有初始化，会直接报类似：

```text
ReferenceError: Cannot access 'activeTask' before initialization
```

### 建议改法

把 `visibleDeviceListEmpty` 移到 `activeTask` 和 `allTaskDevices` 都声明之后：

```ts
const allTaskDevices = useMemo(() => [...taskDevices, ...taskSubDevices], [taskDevices, taskSubDevices])
const allDevices = useMemo(() => [...devices, ...subDevices], [devices, subDevices])

const visibleDevices = activeTask ? allTaskDevices : allDevices
const visibleDeviceListEmpty = visibleDevices.length === 0
```

然后 JSX 里复用同一个 `visibleDevices`：

```tsx
<Sidebar
  allDevices={visibleDevices}
  deviceListEmpty={visibleDeviceListEmpty}
  ...
/>
```

`MapRadarView` 仍然只接收雷达：

```tsx
<MapRadarView devices={activeTask ? taskDevices : devices} />
```

这样能避免重复计算，也能保证当前视图设备列表和 empty 判断完全一致。

## P1 - 设备加载失败后会清空已有设备，后续刷新场景要确认产品语义

文件：`ui/src/App.tsx:36`

当前启动加载失败时：

```ts
if (!ok) {
  setDevices([])
  setSubDevices([])
  setDeviceRegistryStatus('unavailable')
  return
}
```

启动阶段这样做没问题，因为本来没有可信设备。

但如果后续增加“刷新设备台账 / 重连后重新拉取”功能，某次刷新失败会清空已有已确认设备，UI 会从有设备直接跳成不可用空列表。这个语义要提前定一下：

- 如果 `devices` 只代表“当前已确认的后端快照”，失败清空是合理的。
- 如果希望离线时保留最后一次成功快照用于查看，则需要单独的 `stale` 状态。

当前 Milestone 1 可以接受，不需要现在实现 `stale`。但建议在设备状态机文档里先记一条：当前版本采用“失败即不可用，不展示旧设备”的语义。

## P2 - `unavailable` 时收起表单已做，但确认按钮仍没有状态保护

文件：`ui/src/components/layout/Sidebar.tsx:103`

这版新增：

```ts
useEffect(() => {
  if (deviceRegistryStatus === 'unavailable') setFormExpanded(false)
}, [deviceRegistryStatus])
```

这能解决大部分 UI 情况。

不过如果用户已经点了“确认添加”，请求过程中后端变为不可用，确认按钮本身没有 pending / unavailable 保护。这个不是当前阻塞项，因为 `onAddDevice()` 失败会返回 false，表单不会 reset。

后续可以统一加：

```ts
const canEditRegistry = deviceRegistryStatus === 'available'
```

并在新增、删除、编辑入口都复用它。这样设备台账 UI 的可写状态会更一致。

## 已确认修复项

- `fetchDevices()` 的 `ok=false` 已进入 `unavailable`。
- 成功加载后总是用后端返回覆盖前端设备数组。
- 失败加载会清空设备数组，避免残留旧的不可确认设备。
- Sidebar empty 文案已区分任务视图和全局设备台账。
- `unavailable` 状态下会自动收起新增表单。

## 未验证项

本机仍无法运行：

```powershell
npm run build
```

原因仍是当前环境 `npm` 不在 PATH，且 `ui/node_modules` 不存在。

但这版的 P0 可以从源码执行顺序直接确认，不依赖构建环境：`visibleDeviceListEmpty` 读取了尚未初始化的 `activeTask` / `allTaskDevices`。

Claude 修完后必须跑：

```bash
cd ui
npm run build
```

并至少打开前端确认首屏不会白屏。

## 给 Claude 的下一步执行清单

1. 把 `visibleDeviceListEmpty` 移到 `activeTask`、`taskDevices`、`taskSubDevices`、`allTaskDevices`、`allDevices` 声明之后。
2. 建议新增 `visibleDevices = activeTask ? allTaskDevices : allDevices`，Sidebar 的 `allDevices` 和 `deviceListEmpty` 都从它派生。
3. 保持 `MapRadarView` 使用雷达列表，不要把 subDevices 传进去。
4. 在设备台账文档或注释中明确当前语义：加载失败时不保留旧快照，直接进入 `unavailable`。

## 架构结论

设备台账 Milestone 1 的业务语义已经基本定住：后端台账是 source of truth，前端不再伪造正式设备，端点绑定决定可控性，空状态和不可用状态已经区分。

当前阻塞点是实现顺序错误导致前端首屏崩溃。修掉这个 P0 后，Milestone 1 可以认为接近收口，下一步再考虑 endpoint 绑定 UI 或进入后续 RuntimeSettings/任务状态机。
