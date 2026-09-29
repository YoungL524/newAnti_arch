# 最小控制 Owner 设计

> 状态：最小 Owner、采集会话编排和 50 ms `0x0001` 已实施；2026-09-29 新增独立 `0xFFFF` 工作/待机一次性发送，仍无 FIFO。

## 1. 固定调用链

```text
HTTP/UI
  -> ApplicationController
  -> DeviceController
     -> DcmpProtocolAdapter
     -> m_sock2DpSp
     -> DP
```

数据面仍是 `ProtocolAdapter -> TrackIngressProducer -> TrackTopic <- Consumer`。控制参数快照不得进入
Topic；WebSocket 不发送设备命令，周期数据报不经过Command FIFO。

## 2. 仅新增两个 Owner

### 2.1 ApplicationController

职责是在任务激活、完成和采集启停时编排唯一设备控制入口：

- 校验当前任务、设备绑定和 `faceMask`；
- 任务激活时准备控制快照，完成时清除；`0x0001` 固定发送到 `127.0.0.1:2345`；
- 调用 TaskService 创建/结束本地 collection session；
- 绑定/解绑唯一 `TrackIngressProducer`；
- 调用 DeviceController 建立或结束本地采集；
- 请求 TaskService 推进 TaskState，并把本地采集处理结果返回 HTTP 请求者；结果不表示设备收到或起控。

它不持有 Socket、定时器、协议载荷、FIFO、DeviceState 或航迹数据。开始采集以本地会话和 PPI 成功为核心；
控制UDP发送不存在ACK等待阶段。`TaskService` 继续作为 TaskState 唯一写入者，
ApplicationController 不得绕过它直接调用 `LatestState<TaskState>::set()`。

### 2.2 DeviceController

它取代而不是并列保留 `HeartbeatStateOwner`，成为 `LatestState<DeviceState>` 的唯一写入者，并拥有：

- DP/SP 最近心跳时刻和 100 ms 期限检查定时器；
- 当前任务的一份最新 `RadarControlSettings` 业务控制快照；其初值来自任务持久化参数快照；
- 50 ms 参数发送定时器；
- 借用的 `DcmpProtocolAdapter` 编码入口和 `UdpEndpoint` 发送入口；
- 当前设备 revision 的部署快照和任务 `faceMask`；`0x0001` 发送目标固定为 `127.0.0.1:2345`。

它不拥有任务数据库、采集会话、TrackTopic、Web 服务或第二个 Socket。

当前实现由DeviceController接管心跳期限与DeviceState唯一写入，保存当前任务的单份参数运行副本、逐设备
`faceMask`和任务设备通信目标；50 ms到期时临时编码并提交一包，不保存协议报文或周期队列。

任务模板及任务参数快照由 TaskService/SQLite 持久化，DeviceController 不拥有模板仓库。ApplicationController
激活任务时把已校验的任务参数与设备快照交给 DeviceController；运行中的“应用参数”先由 TaskService 保存任务快照，
成功后再原子替换 Owner 内存值。共享模板只有用户执行“另存为模板/更新模板”时才变化。

## 3. 唯一周期发送

- 50 ms 定时器不向 FIFO 投递项目；每次到期直接读取当前最新控制快照并编码 `0x0001`。
- Adapter 在本次发送栈内把控制快照、设备部署参数和任务 `faceMask` 映射为临时
  `DcmpToDpControlPayload`；协议载荷不得在 Owner 内长期驻留。
- 只有“当前任务已激活且快照准备成功、DP 在线、SP 在线”三个条件同时成立时发送；采集按钮不参与门控。
- 任一心跳失效立即停止发送；条件恢复后下一次定时触发自动续发，不补发离线期间的周期数。
- 参数更新原子替换最新快照；旧值不排队，因此高频界面修改不会形成延迟积压。
- 单次发送失败只保留最近错误诊断，不停止本地采集，不改变 TaskState，不启动无界重试；事件和计数尚未实现。
- 天线模式、波形写入及其他稳态参数属于最新快照，每次50 ms完整发送；DeviceController 不对这些字段制造
  一次性命令语义。
- RF复位是面向指定阵面的独立动作状态，不能仅凭一次 FP 在线心跳宣告成功；在
  复位保持时间和恢复条件冻结前只保留合同，不实施推测性状态机。

## 4. 状态与线程

上述对象全部运行在拥有 `m_sock2DpSp` 的 Qt 主线程：HTTP 请求通过 queued invocation 进入该线程，
DeviceController 的两个 QTimer 与 Socket 同线程。SQLite 仍由唯一存储线程拥有；ApplicationController 通过现有
StorageRuntime 边界提交低频会话事务。不得在 50 ms 回调中阻塞访问 SQLite。

DeviceState 同时表达心跳/连接状态和本地采集状态，两者正交：心跳灰色时仍可保持 `Collecting`；
恢复在线只恢复参数发送，不创建新会话。删除 `HeartbeatStateOwner` 后，其期限检查逻辑迁入 DeviceController，
不允许两个类同时写完整 DeviceState。

## 5. 开始与结束顺序

### 任务激活

新建任务和沿用旧任务都在激活成功后准备参数、阵面和设备快照；双心跳在线即开始向 `127.0.0.1:2345` 周期发送。准备失败撤销本次激活，保留草稿。

### 开始

1. ApplicationController 校验 Ready 任务和非零 faceMask。
2. TaskService 提交各启用阵面的本地 collection session。
3. 绑定 TrackIngressProducer，TaskState/DeviceState 进入 Running/Collecting，UI PPI 立即开始。
4. DeviceController 向固定 DP 目标提交一次 `0xFFFF` 工作命令；`0x0001` 周期不因开始采集改变。

### 结束

1. DeviceController 向固定 DP 目标提交一次 `0xFFFF` 待机命令；离线不阻塞本地停止。
2. ApplicationController 解绑 TrackIngressProducer并闭合本地 session。
3. DeviceState 进入 Idle，UI PPI 停止；任务仍处于 Ready 时 `0x0001` 周期发送继续。完成任务后清除快照并停止发送。

结束时即使控制包本地发送失败，也必须允许本地会话收口，不能锁死任务。

## 6. 失败与回滚边界

- 开始操作在 session 提交前失败：不改变 PPI、绑定或状态。
- session 已提交但绑定失败：ApplicationController 立即闭合本次 session，再返回 failed。
- 本地采集期间控制数据报发送失败：保留本地采集，不伪造设备状态，也不启动无界重试。
- 参数快照校验失败：拒绝整次更新，继续发送上一份有效快照。

## 7. 实施时允许的最少公开类型

第一版只新增以下三个公开类型：

1. `ApplicationController`；
2. `DeviceController`；
3. `RadarControlSettings`：任务持久化与当前活动任务共同使用的语义化参数值类型。

HTTP继续复用已冻结的统一成功/失败响应，不另增设备确认结果类型。`RadarControlSettings`不含包头、
备份字和厂家结构偏移，
发送边界临时映射成 `DcmpToDpControlPayload` 后立即释放。模板、任务快照和 Owner 运行副本允许保存同一值类型，
但分别承担复用基线、历史事实和当前发送状态；不得再为 UI 或 Adapter 创建第四份长期配置模型。

不得新增控制总线、抽象设备基类、Command variant、通用 DTO 工厂、第二个状态 Owner 或每周期消息对象。
