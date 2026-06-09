# Review: 41d7f90

Commit: `41d7f90` (`review-5872cb7修改完成`)

## 总体判断

这版比上一版干净很多，之前的 P0 基本都收掉了：

- `DeviceRegistryStatus` 已迁到 `ui/src/lib/devicesApi.ts`，不再从 Sidebar 反向导出给 App。
- `DEFAULT_DEVICE`、`nextDeviceId()`、`nextSubId()` 残留已经删除。
- `empty` 不再是独立 load state，而是由设备数组派生。
- `unavailable` 状态下新增设备按钮已禁用。
- 设备台账文档重复的“设备身份规则”章节已删除。

但是这里还有一个关键逻辑漏洞：`fetchDevices()` 失败时不会 throw，而是 resolve `{ ok:false }`，`App.tsx` 现在没有检查 `ok`，会把 API 失败误判成 `available + empty`。这会让“后端台账不可用”的 UI 状态基本失效。

## P1 - `/api/devices` 失败会被误判成 available + empty

文件：`ui/src/App.tsx:34`

当前代码：

```ts
fetchDevices()
  .then(({ radars, subs }) => {
    if (radars.length > 0 || subs.length > 0) {
      setDevices(radars)
      setSubDevices(subs)
    }
    setDeviceRegistryStatus('available')
  })
  .catch(() => {
    setDeviceRegistryStatus('unavailable')
  })
```

问题在于 `fetchDevices()` 内部调用 `apiCall()`，而 `apiCall()` 对 HTTP 非 2xx、API 返回 `ok:false`、网络异常都会返回对象：

```ts
return { ok: false, error: ... }
```

它不会 throw。

所以大多数失败场景都会进入 `.then()`，并且 `radars/subs` 都是空数组，最终状态变成：

```ts
deviceRegistryStatus = 'available'
deviceListEmpty = true
```

UI 会显示“后端台账为空，请添加设备”，而不是“后端设备台账不可用”。这会误导用户，也破坏我们刚建立的设备台账可用性状态。

### 建议改法

在 `App.tsx` 明确检查 `ok`：

```ts
fetchDevices()
  .then(({ ok, radars, subs }) => {
    if (!ok) {
      setDevices([])
      setSubDevices([])
      setDeviceRegistryStatus('unavailable')
      return
    }

    setDevices(radars)
    setSubDevices(subs)
    setDeviceRegistryStatus('available')
  })
  .catch(() => {
    setDevices([])
    setSubDevices([])
    setDeviceRegistryStatus('unavailable')
  })
```

也可以让 `fetchDevices()` 在失败时 throw，但要统一 API 层风格。当前项目更偏向返回 `ApiResult`，所以 App 层检查 `ok` 更贴合现有代码。

## P2 - 设备加载成功但空数组时最好显式清空本地列表

文件：`ui/src/App.tsx:35`

当前代码只有在 `radars/subs` 非空时才 `setDevices/setSubDevices`。启动时状态为空数组，所以看起来没问题。

但从状态机完整性看，成功加载后应该无论是否为空都以后端返回为准：

```ts
setDevices(radars)
setSubDevices(subs)
setDeviceRegistryStatus('available')
```

这样以后如果增加“刷新设备台账”功能，后端从有设备变成空表时，前端不会残留旧设备。

## P2 - `deviceListEmpty` 是全局空状态，在任务视图下语义不准确

文件：`ui/src/App.tsx:26`

当前：

```ts
const deviceListEmpty = devices.length === 0 && subDevices.length === 0
```

传给 Sidebar 时，无论是否处于任务上下文都使用这个全局状态：

```tsx
allDevices={activeTask ? allTaskDevices : allDevices}
deviceListEmpty={deviceListEmpty}
```

如果全局台账有设备，但当前任务没有绑定设备，Sidebar 显示的是 `allTaskDevices=[]`，但 `deviceListEmpty=false`，因此不会提示当前任务无设备。

这不是本轮设备台账 source of truth 的阻塞项，但后面做任务状态机时会变成体验问题。

### 建议改法

短期可以把传给 Sidebar 的 empty 改成当前视图的 empty：

```ts
const visibleDevices = activeTask ? allTaskDevices : allDevices
const visibleDeviceListEmpty = visibleDevices.length === 0
```

但文案要区分：

- 非任务视图：`后端台账为空，请添加设备`
- 任务视图：`当前任务未绑定设备`

## P2 - `unavailable` 时如果表单已经展开，不会自动收起

文件：`ui/src/components/layout/Sidebar.tsx:314`

现在只是在未展开状态下禁用“添加设备”按钮。如果状态变为 `unavailable` 前表单已经展开，表单仍然可见，确认添加按钮仍然可以点击并触发 API。

当前设备加载只在启动时执行，实际触发概率不高；但如果后续加刷新/重连，这会变成真实问题。

### 建议改法

可以先低成本处理：

```tsx
{formExpanded && deviceRegistryStatus !== 'unavailable' && (
  ...
)}
```

或者在状态变为 `unavailable` 时 `resetForm()`。

## 已确认修复项

- `DeviceLoadState` 已移除，替换为 `DeviceRegistryStatus = loading | available | unavailable`。
- 共享状态类型已移到 `devicesApi.ts`。
- `App.tsx` 未使用的 `DEFAULT_DEVICE`、`nextDeviceId()`、`nextSubId()` 已删除。
- `empty` 状态已由设备列表派生，不再手动维护。
- `unavailable` 状态下“添加设备”入口已禁用。
- 设备台账文档重复章节已删除。

## 未验证项

本机仍无法运行：

```powershell
npm run build
```

原因仍是当前环境 `npm` 不在 PATH，且 `ui/node_modules` 不存在。

从源码层面看，上一轮确定的 TypeScript P0 已基本修掉。但 Claude 仍应在他的环境中执行：

```bash
cd ui
npm run build
```

## 给 Claude 的下一步执行清单

1. `App.tsx` 加上 `fetchDevices()` 返回值的 `ok` 判断；`ok=false` 必须进入 `unavailable`。
2. 成功加载时无论设备数组是否为空，都 `setDevices(radars)`、`setSubDevices(subs)`。
3. 后续任务视图中，Sidebar 的 empty 状态改成“当前视图 empty”，并区分全局台账空和任务未绑定设备。
4. 可选：`unavailable` 时如果新增表单已展开，隐藏或自动收起表单。

## 架构结论

Milestone 1 已经接近收口。设备 source of truth、前端 fallback、端点绑定可见性这些主线基本立住了。

这轮剩下的问题不是架构方向，而是 API result 语义没有被 App 正确消费。修掉 `ok=false` 误判以后，设备台账的可用/空/不可用状态才算真的可靠。
