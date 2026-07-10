# Review e2acbe3

## 结论

本轮确认了“前端 UI 没有航迹显示”的主要原因。

地图绘制组件本身没有明显断裂，问题出在实时航迹进入地图前被多层过滤：

1. 后端 `TrackPoint` 已有 `taskId`，但 `TrackDisplayPoint` 和 WS JSON 没有继续传递它。
2. 前端在存在 active task 时优先按 `taskId` 过滤，只能退化到 `deviceId in activeTaskDeviceIds`。
3. 没有 active task 时，前端当前逻辑直接把所有实时航迹过滤为空。
4. 地图只为 `checkedDeviceIds` 创建 TrackLayer，但默认 `checkedDeviceIds` 是空数组，Sidebar 没有自动勾选逻辑。

这几项叠加后，即使 core 正常收到并推送航迹，UI 也可能完全不显示。

## P1. 后端实时航迹 DTO 丢失 taskId

涉及文件：

- `core/business/TrackTypes.h`
- `core/business/TrackDisplayManager.cpp`
- `core/web/TrackDisplayWebBridge.cpp`
- `ui/src/lib/protocol.ts`

当前内部 `TrackPoint` 已经有：

```cpp
QString deviceId;
QString taskId;
```

但 `TrackDisplayPoint` 只有：

```cpp
QString deviceId;
```

`TrackDisplayManager::snapshot()` 也只复制了：

```cpp
dp.deviceId = pt.deviceId;
```

最后 `TrackDisplayWebBridge::pushSnapshot()` 的 JSON 同样没有写 `taskId`。

前端虽然把协议改成：

```ts
taskId?: string
```

但实际永远收不到该字段，因此：

```ts
if (t.taskId === currentTaskId) return true
```

永远无法命中。

### 建议 Cursor 修改

1. 给 `TrackDisplayPoint` 增加：

```cpp
QString taskId;
```

2. 在 `TrackDisplayManager::snapshot()` 中复制：

```cpp
dp.taskId = pt.taskId;
```

3. 在 `TrackDisplayWebBridge::pushSnapshot()` 中输出：

```cpp
t[QStringLiteral("taskId")] = dp.taskId;
```

4. 如果 `taskId` 为空，建议仍然输出空字符串，固定 WS 协议字段，不要让前端猜字段是否存在。

这是本轮最优先的修复。

## P1. 无 active task 时前端会丢弃全部实时航迹

位置：

- `ui/src/hooks/useCoreApi.tsx`
- `applyTracksSnapshot()`

当前逻辑：

```ts
const visibleTracks = currentTaskId
  ? msg.tracks.filter(...)
  : []
```

也就是说，只要：

- 系统启动后还没有 active task
- active task 请求尚未加载完成
- active task 被完成/归档

实时航迹就会全部变成空数组。

这里必须明确业务策略。

### 推荐策略

系统是任务驱动的，但 UI 不应该在任务数据尚未加载完成的瞬间把诊断链路彻底隐藏。

建议区分：

```text
任务数据仍在 loading：暂不应用任务过滤
确认没有 active task：不归属任务的实时航迹是否显示，由产品规则决定
有 active task：严格按 taskId 过滤
```

当前最小修复可以先改为：

```ts
const visibleTracks = currentTaskId
  ? msg.tracks.filter(t => t.taskId === currentTaskId)
  : msg.tracks
```

如果业务最终决定“无任务绝不显示航迹”，也应该在 UI 明确显示：

```text
当前无激活任务，实时航迹已隐藏
```

不能表现成静默无数据，否则用户会以为通信或解析坏了。

## P1. checkedDeviceIds 默认为空，地图不会创建任何 TrackLayer

涉及文件：

- `ui/src/hooks/useCoreApi.tsx`
- `ui/src/components/layout/Sidebar.tsx`
- `ui/src/components/map/MapRadarView.tsx`

默认状态：

```ts
checkedDeviceIds: []
```

Sidebar 只有用户手动点击 checkbox 才调用：

```ts
setCheckedDevices(...)
```

没有发现设备/任务加载后自动选中雷达的逻辑。

而地图实时层只渲染：

```ts
devices.filter(d => checkedDeviceIds.includes(d.id))
```

所以默认打开 UI 时，哪怕 `tracks` 状态里有数据，也不会创建 `TrackLayer`。

### 建议 Cursor 修改

在设备和 active task 加载完成后，自动初始化勾选设备：

```text
有 active task：默认勾选该任务绑定的、可控的雷达设备
无 active task：默认勾选后端台账中绑定 endpoint 的雷达设备
```

同时要保留用户后续手动取消勾选的能力，不能每次轮询都重新覆盖用户选择。

建议只在以下时机初始化一次：

- 首次设备加载完成
- activeTaskId 真正发生切换

不要在每次 status poll 时自动重置。

## P2. deviceId fallback 会掩盖 taskId 链路错误

当前过滤：

```ts
if (t.taskId === currentTaskId) return true
return !!t.deviceId && taskDevices.has(t.deviceId)
```

这个 fallback 让旧 WS 协议暂时还能工作，但也会掩盖后端没有传 `taskId` 的问题。

在后端完成 taskId 贯通后，建议收紧为：

```ts
return t.taskId === currentTaskId
```

可以在一轮兼容期内保留 fallback，并记录 warning/计数；确认所有实时点都有 taskId 后再删除。

否则一个设备如果在任务切换边界产生延迟数据，仅按 deviceId 可能把旧任务数据显示到新任务。

## P2. acquisition gating 仍可能隐藏已收到的航迹

位置：

- `ui/src/hooks/useCoreApi.tsx`
- `flushTracksToState()`

当前：

```ts
if (!prev.acquisition) return prev
```

也就是说，WS snapshot 已经收到并写入 `tracksRef`，但只要 `/api/status` 合成的 acquisition 还是 false，就不会进入 React state。

本轮已经增加了 acquisition 变 true 时补刷，这能解决 WS 早于 status 的情况。

但如果后端状态语义异常，航迹数据会一直被隐藏。建议增加诊断状态，例如：

```text
lastWsTrackCount
visibleTrackCount
acquisition
activeTaskId
checkedDeviceIds
```

至少在开发环境打印一次过滤前后数量，方便定位到底是“没收到”还是“被过滤”。

## P2. tracks 消息识别方式较脆弱

位置：

- `ui/src/hooks/useCoreApi.tsx`
- `ws.onmessage`

当前通过字符串判断：

```ts
raw.includes('"type":"tracks"')
raw.includes('"type": "tracks"')
```

对当前 Qt 紧凑 JSON 通常能工作，但依赖序列化空格格式。

这不是当前无航迹的首要原因，但建议后续改成更稳定的消息分类方案，例如：

- 后端使用独立 WS channel/message prefix
- 或前端统一 parse 后，再把 tracks 的重处理放入 RAF
- 或使用轻量顶层 type 提取，而不是匹配固定空格格式

## 架构判断

本轮前端试图让实时航迹 task-aware，方向正确，但实现只改了前端协议和过滤逻辑，后端 DTO 没同步升级。

完整链路应该是：

```text
RadarDataProcessor
  TrackPoint.taskId
      -> TrackDisplayManager
  TrackDisplayPoint.taskId
      -> SystemBus snapshot
  TrackDisplayWebBridge JSON.taskId
      -> UI WsTrackDisplayPoint.taskId
      -> activeTaskId 过滤
```

目前断在 `TrackPoint -> TrackDisplayPoint`。

## 建议 Cursor 修复顺序

1. 贯通后端 `TrackDisplayPoint.taskId` 和 WS JSON `taskId`。
2. 修正无 active task 时静默过滤全部航迹的行为，并增加明确 UI 提示。
3. 在首次设备/任务加载时初始化 `checkedDeviceIds`。
4. 增加开发期过滤诊断：收到多少、task 过滤后多少、设备勾选后多少。
5. 后端 taskId 稳定后，逐步移除 deviceId fallback。

## 当前最可能的直接原因

按概率排序：

1. `checkedDeviceIds` 为空，地图根本没有创建 TrackLayer。
2. `activeTaskId` 为空或 active task 设备列表为空，`visibleTracks` 被过滤为 `[]`。
3. 后端 WS 没有 `taskId`，只能依赖可能为空/滞后的 deviceId fallback。
4. acquisition 状态为 false，`flushTracksToState()` 拒绝刷新。

这四项都可以在不怀疑编译和 core 接收链路的前提下解释“UI 完全没有航迹”。
