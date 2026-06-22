# Review 2fd430c

## 结论

这一版把 `review-086e44a` 的核心问题基本修完了：`/api/reset` 已补齐，前端 Fault 状态点击按钮会走 reset；`DeviceRegistry::remove()` 已加事务；工作模式文档也明确了“超时成功是临时乐观确认”。这轮小闭环质量不错。

当前没有明显阻塞性问题。接下来要注意的是：文档和 API 已经开始使用“多设备独立状态机”的表达，但代码里实际仍是单个 `RadarWorkModeController`，只是通过 `deviceIds` 映射到 `radar_cpu` 这个单控制器。这个作为单雷达 MVP 可以接受，但不能当作多设备控制已经完成。

## P1 / 架构一致性

### 1. 文档写“每台雷达独立状态机”，但代码仍是单控制器

当前文档 `doc/business/雷达工作状态控制.md` 写了：

- 每台雷达有独立状态机
- GET `/api/status` 返回每台雷达独立状态
- 任意设备 Fault 时只复位故障设备

但代码实际是：

- `AppController` 只有一个 `std::unique_ptr<RadarWorkModeController> m_workModeController`
- `WebApiController` 只有一个 `RadarWorkModeController* m_wmc`
- `/api/start`、`/api/stop`、`/api/reset` 虽然接收 `deviceIds`，但循环里命中第一个 mapped id 后就 `break`
- `buildDeviceStatusArray()` 把所有映射到 `radar_cpu` 的设备都合并到同一个 `m_wmc->state()`

这不是当前版本的 bug，但它是架构描述和实现能力的偏差。

建议：

- 如果当前阶段只支持一个可控雷达 CPU，就在文档里明确写成“当前实现为单控制器 MVP，API 保留 deviceIds 形态，为多控制器扩展预留”。
- 如果要兑现“每台雷达独立控制”，下一阶段需要把 `m_workModeController` 提升为 `QHash<QString, unique_ptr<RadarWorkModeController>>` 或类似结构，key 可以是业务 `deviceId` 或 `endpointId`。
- `/api/status` 应返回每台设备自己的状态，而不是所有 radar_cpu 映射设备共享同一个状态。

我建议短期先改文档，不急着实现多控制器；否则会在设备管理、任务绑定、DPH 下行链路还没完全稳定时扩大复杂度。

## P2 建议

### 2. /api/reset 的设备选择语义需要更精确

当前 reset 的行为：

- body 为空时 accepted `radar_cpu`
- body 有多个 deviceIds 时，只要有一个映射到 `radar_cpu`，就执行一次 `m_wmc->reset()`，然后 `break`
- acceptedDeviceIds 可能包含多个设备，但实际只有一个控制器被 reset

在单控制器阶段这可以工作，但 UI 可能误以为多个设备都被独立复位。

建议：

- 当前阶段 response 里加一个说明字段，例如 `controllerId: "radar_cpu"` 或 `mode: "single-controller"`。
- 或者只返回实际执行的那一个 accepted deviceId，避免“accepted 多个，但只 reset 一次”的语义歧义。
- 多控制器真正落地后，再让 acceptedDeviceIds 表示逐设备执行结果。

### 3. reset 前端乐观更新可以接受，但最好尽快触发一次状态刷新

`useCoreApi.tsx` 在 Fault reset 成功后直接：

```ts
state: 'Standby'
acquisition: false
```

这对交互体验是好的。但权威状态仍然来自 `/api/status` 或 WS status。建议 reset 成功后主动触发一次 status reload，或者等待下一次 WS 时保持 `commandPending` 到状态回推。

当前 1s WS status 足够快，所以不是 blocker；只是后续多设备状态机落地后，最好减少纯前端猜测。

### 4. DeviceRegistry::remove() 已有事务，但还可以补错误语义

这一版已加事务，方向对。

但失败时 API 仍然只返回：

```json
{ "ok": false }
```

建议后续给 `DeviceRegistry` 增加类似 `lastError()`，至少能区分：

- 设备不存在
- task_devices 清理失败
- devices 删除失败
- commit 失败

这不急，但管理系统后续需要可诊断性，不能长期只有 bool。

## 已修复确认

- `RadarWorkModeController::reset()` 已返回 bool，Web API 可以判断是否成功。
- `POST /api/reset` 已注册，支持 `deviceIds` 和节流。
- 前端 Fault 状态下点击按钮已走 `/api/reset`，不再误走 start/stop。
- `DeviceRegistry::remove()` 已使用事务包住 task-device 清理和 device 删除。
- 工作模式文档已补“过渡超时（临时乐观确认）”，明确当前 Working 是上位机控制语义，不等于真实设备 ACK。

## 下一步建议

1. 短期：把工作模式文档里的“每台雷达独立状态机”改成“目标架构 / 当前单控制器 MVP”，避免文档超前于实现。
2. 中期：推进任务上下文绑定航迹、日志、历史数据，这比现在立刻做多控制器更符合 Milestone 4。
3. 后续需要多雷达时，再集中做 `WorkModeControllerRegistry`，不要在 WebApiController 里继续堆单控制器映射逻辑。

## 给 Cursor 的最小修改清单

1. 修改 `doc/business/雷达工作状态控制.md`，标注当前实现是单 `radar_cpu` 控制器，deviceIds 是预留扩展契约。
2. `/api/reset` response 增加实际控制器/单控制器模式说明，或只返回实际执行的 accepted id。
3. 可选：reset 成功后前端主动刷新一次 `/api/status`。

