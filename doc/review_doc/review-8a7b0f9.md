# 全局架构与代码质量复核

基线版本：`8a7b0f9`（任务驱动逻辑优化）

## 结论

当前代码已经形成可持续迭代的应用基座，主链路没有偏离既定架构：

- 设备与任务以 core/SQLite 为唯一事实源；
- 原始航迹经 `RadarDataProcessor -> TrackDisplayManager` 进入显示数据管理；
- Web 实时航迹只经 `SystemBus -> TrackDisplayWebBridge -> WebSocket` 输出；
- 任务绑定设备后，航迹、事件和日志按 `taskId + deviceId` 自动归属；
- UI 的活动任务与历史查看上下文相互独立。

代码质量处于“主流程稳定、领域约束正在补齐、自动化覆盖仍偏弱”的阶段。此时适合继续完善任务历史与显示协议，不适合再引入第二套状态源或旁路数据流。

## 本轮已加固

1. 删除 UI 的 localStorage 任务 fallback；core 断开时保留最后一次后端快照并禁止任务写操作。
2. `TrackInteractionCommand` 增加 `taskId`；API 和 Manager 双重校验活动任务、设备绑定及航迹缓存。
3. 历史数据表只读；乐观交互状态增加超时回退，并正确识别省略的默认 interaction。
4. Completed/Archived 任务配置冻结；普通 update 不再有权修改任务状态和生命周期时间。
5. 同时最多一个活动任务；激活第二个任务不再静默完成第一个任务，数据库增加唯一约束。
6. 活动任务必须始终保留可控雷达；活动设备的 endpoint 不得解除或被其他设备迁走。
7. 设备硬删除增加任务引用与关联子设备保护；更新设备时保留 `createdAt/status`。
8. endpoint 热更新改为“运行时验证 -> 原子落盘”，失败时恢复旧运行配置。
9. 航迹显示缓存采用全局 50000 点预算，无单航迹点数上限；FIFO 淘汰使用 O(1) 队列，snapshot 仅选取最新 600 点。
10. 历史航迹和任务日志改为获取最新有界集合，再按时间正序展示。
11. 新增 TaskService/DeviceRegistry 领域测试，并为 ConfigManager 原子保存增加回归测试。

## 剩余架构债

### P1 - 任务历史缺少设备快照与退役模型

历史任务当前仍通过 `task_devices.device_id` 关联实时 `devices` 表。设备改名、改坐标或改型号后，旧任务会按新元数据解释。现有硬删除保护只能防止历史关联被破坏，不能保证历史语义不变。

下一步应引入任务激活时的设备快照（至少名称、类型、品牌、坐标、endpoint 归属）以及设备软退役字段。历史视图读取快照，实时设备管理读取当前台账。

### P1 - 航迹样式所有权尚未完全收口

`TrackDisplayManager` 已输出 `style/interaction`，但 `TrackLayer.tsx` 仍根据 `pointIndex` 和 interaction 在前端硬编码颜色、点径、渐变和箭头样式。它能工作，但与“UI 只渲染后端确定的显示状态”仍有差距。

下一步需要固定 Web 显示协议中的样式语义：由 Manager 输出点/线/航向标记所需状态，React 只做画布坐标转换与绘制。

### P2 - 自动化覆盖仍不足

当前缺少 `SystemBus`、`TrackDisplayManager`、Web API 路由和历史分页的自动化测试，也没有 CI 构建门禁。建议在继续扩展业务前补一条 Windows/Qt + UI build 的基础流水线。

### P2 - 历史能力仍是静态查看

当前历史接口是有界查询，不是时间轴回放。后续回放应增加游标/时间窗分页和统一播放时钟，不应一次把完整任务数据装入浏览器。

## 推荐下一步

先实现“任务设备快照 + 设备软退役”，再收口航迹样式协议。两项完成后，Milestone 4 的任务历史语义才真正稳定，Milestone 5 的 core-Web 固定协议也才算闭环。

## 验证说明

- 已通过 `git diff --check`。
- 已对新增 SQLite 索引与“最新 N 条后正序返回”查询做 SQL smoke test。
- 当前工作区没有 Qt/CMake/TypeScript 依赖，未执行 core 编译、CTest 和 `npm run build`；需在完整开发环境执行。
