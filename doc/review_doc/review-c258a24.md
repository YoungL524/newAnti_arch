# Review: c258a24

Commit: `c258a24` (`review-0b34e03修改完成`)

## 总体判断

这版把上一轮的核心 P0 修掉了：

- `visibleDeviceListEmpty` 已移动到 `activeTask`、`allTaskDevices`、`allDevices` 声明之后。
- 新增了 `visibleDevices`，并统一传给 `Sidebar` 和 `BottomPanel`。
- `MapRadarView` 仍然只接收雷达列表，没有把 subDevices 传进去。

业务逻辑方向已经比较稳了。现在剩下一个很小但会直接挡构建的问题：旧的 `deviceListEmpty` 变量还留在 `App.tsx`，但已经不再使用。

## P0 - `deviceListEmpty` 未使用，严格 TS 构建会失败

文件：`ui/src/App.tsx:26`

当前代码：

```ts
const deviceListEmpty = devices.length === 0 && subDevices.length === 0
```

但后面已经改成：

```ts
const visibleDevices = activeTask ? allTaskDevices : allDevices
const visibleDeviceListEmpty = visibleDevices.length === 0
```

`deviceListEmpty` 不再被使用。

项目 `ui/tsconfig.app.json` 开启了：

```json
"noUnusedLocals": true
```

所以这会触发 TypeScript 编译错误。

### 建议改法

直接删除这一行即可：

```ts
const deviceListEmpty = devices.length === 0 && subDevices.length === 0
```

当前 `visibleDeviceListEmpty` 已经覆盖全局视图和任务视图，不需要保留旧变量。

## P2 - `visibleDevices` 可以保持当前写法，暂不必过度 memo

文件：`ui/src/App.tsx:78`

当前：

```ts
const visibleDevices = activeTask ? allTaskDevices : allDevices
const visibleDeviceListEmpty = visibleDevices.length === 0
```

这没问题。`allTaskDevices` 和 `allDevices` 已经分别 useMemo，`visibleDevices` 只是二选一引用，不需要为了形式再套一层 `useMemo`。

## 已确认修复项

- `visibleDeviceListEmpty` 不再读取未初始化的 `activeTask` / `allTaskDevices`。
- `visibleDevices` 已统一用于 Sidebar 和 BottomPanel。
- `allDevices` 的重复声明已删除。
- `MapRadarView` 仍按雷达设备列表渲染，边界正确。

## 未验证项

本机仍无法运行：

```powershell
npm run build
```

原因仍是当前环境 `npm` 不在 PATH，且 `ui/node_modules` 不存在。

不过这版的构建问题可以从源码和 `noUnusedLocals` 配置直接确认：`deviceListEmpty` 是未使用局部变量。

Claude 修完后必须跑：

```bash
cd ui
npm run build
```

## 给 Claude 的下一步执行清单

1. 删除 `ui/src/App.tsx` 中未使用的 `deviceListEmpty`。
2. 跑 `cd ui && npm run build`。
3. 打开前端确认首屏、全局设备空状态、任务未绑定设备状态都正常显示。

## 架构结论

设备台账 Milestone 1 已经基本收口。当前问题不是架构问题，只是清理残留变量导致的构建阻塞。

删除 `deviceListEmpty` 并通过前端构建后，可以开始考虑下一步：endpoint 绑定 UI，或进入 RuntimeSettings / 任务状态机的后续里程碑。
