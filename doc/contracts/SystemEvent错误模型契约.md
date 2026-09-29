# SystemEvent 错误模型契约

## 1. 目的与架构位置

本文定义雷达上位机进程内错误、故障、告警及相关运行事实的统一合同。事件生产者在事实已经发生后构造
`Sample<SystemEvent>`，并发布到唯一的 `EventTopic<SystemEvent>`；监控、日志、Web 和归档消费者分别维护
自己的 cursor，通过 `readAfter()` 顺序读取。

本合同遵守现行架构 `doc/architecture/新架构4.png`：

```text
Device / Track / System / Control Producer
                    |
                    | publish Sample<SystemEvent>
                    v
          EventTopic<SystemEvent>
                    ^
                    | readAfter(cursor)
       Monitor / Archive / Web / LogAlarmReader
```

`SystemEvent` 是“已经发生的事实”，不是命令、异常对象、当前状态或通用消息信封。不得为四个错误域分别创建
`ErrorTopic`，不得通过 `QVariant`、`std::variant` 或动态字符串载荷扩充它。

## 2. 错误域

`EventDomain` 说明事件由哪一类业务职责产生，而不是决定把事件路由给哪个消费者。

| 域         | 典型生产者                                              | 典型事实                                |
| --------- | -------------------------------------------------- | ----------------------------------- |
| `Device`  | `UdpEndpoint`、协议适配器、设备状态 owner                     | 通信中断、协议帧非法、设备上报故障                     |
| `Track`   | 航迹协议映射器、发布门禁、识别处理器                                 | 航迹字段越界、时间倒退、身份无效、Topic sequence gap |
| `System`  | 数据域、执行调度、存储和运行时基础设施                                | 容量覆盖、归档失败、资源耗尽、内部不变量被破坏             |
| `Control` | `ApplicationController`、`DeviceController`             | Busy、本地状态转换失败、控制数据报发送失败               |

归类以“拥有该事实的模块”为准。例如本地控制数据报发送失败属于 `Control`；设备主动上报自身故障属于
`Device`；存储消费者发现自身 cursor gap 属于 `System`。DP/SP 不提供命令 ACK，禁止构造确认超时事件。

## 3. 严重级别

| `EventSeverity` | 含义                      | 默认处置                      |
| --------------- | ----------------------- | ------------------------- |
| `Info`          | 正常但值得追溯的操作或状态变化         | 记录，可按策略归档                 |
| `Warning`       | 已降级或出现异常征兆，核心功能仍可继续     | 展示并计数，必要时归档               |
| `Error`         | 当前操作或一批数据失败，但系统仍能提供部分服务 | 展示、归档并关联失败操作              |
| `Fault`         | 设备或核心模块进入持续故障状态，需要恢复动作  | 告警、归档并驱动状态 owner 更新 State |
| `Alarm`         | 需要操作员及时确认或处置的高优先级事实     | 突出告警、归档；确认状态由独立业务状态管理     |

严重级别不等于错误域，也不承担状态机职责。同一个稳定错误码可以因现场上下文提升严重级别，但生产者必须
在错误码登记表中声明允许的级别范围。

## 4. 时间合同

事件使用现有 `SampleMeta`，不在 `SystemEvent` 内重复保存时间：

- `sourceTimeUtcMs`：事实实际发生的 UTC Unix 毫秒时间；设备能提供可信时间时使用设备时间，否则由最先确认
  该事实的进程内模块填写本机 UTC 时间。
- `receiveTimeUtcMs`：本进程首次接收相关外部输入的 UTC Unix 毫秒时间；纯内部事件可与
  `sourceTimeUtcMs` 相同。
- `receiveTimeSteadyNs`：本进程观察到事实的单调时钟时间，用于耗时、超时和先后关系判断。
- `sourceSequence`：外部协议确有序号时填写；内部事件不得伪造设备序号。
- `traceId`：关联一次接收、一次命令或一条处理链；同一失败事实的 `CommandResult` 和 `SystemEvent` 应复用。
- `endpointId`：事件可归因到接收端点时填写。

事件进入 `EventTopic` 后还会获得独立的 `topicSequence`。时间字段和 Topic sequence 用途不同：前者描述事实
时间，后者只供消费者定位、顺序读取和 gap 检测。

## 5. 建议的紧凑类型合同

以下为后续 C++ 实现的权威字段集合。字段顺序可为减少 padding 调整，但语义和单位不得擅自改变。

```cpp
enum class EventDomain : quint8
{
    Device = 1,
    Track = 2,
    System = 3,
    Control = 4
};

enum class EventSeverity : quint8
{
    Info = 0,
    Warning = 1,
    Error = 2,
    Fault = 3,
    Alarm = 4
};

enum class EventSubjectFlag : quint16
{
    None = 0,
    DeviceIdValid = 1U << 0,
    TaskIdValid = 1U << 1,
    AcquisitionIdValid = 1U << 2,
    TrackIdValid = 1U << 3,
    RequestIdValid = 1U << 4,
    Argument0Valid = 1U << 5,
    Argument1Valid = 1U << 6
};

using EventSubjectFlags = quint16;

struct SystemEvent
{
    AcquisitionId acquisitionId;  // flags 有效时关联一次采集会话
    quint64 requestId;             // flags 有效时关联一次控制请求，不等同于 traceId
    qint64 argument0;              // 错误码登记表规定含义的定长诊断参数
    qint64 argument1;              // 错误码登记表规定含义的第二个定长诊断参数
    DeviceId deviceId;             // 雷达设备标识；当前同时代表其单阵面
    TaskId taskId;                 // flags 有效时关联任务
    quint32 trackId;               // flags 有效时关联设备域内航迹
    EventCode eventCode;            // 稳定、可持久化的事件码
    quint32 messageTemplateId;      // 注册表中的中文显示模板；零表示仅按 eventCode 展示
    EventSubjectFlags flags;        // EventSubjectFlag 的位集合
    EventDomain domain;             // Device / Track / System / Control
    EventSeverity severity;         // Info / Warning / Error / Fault / Alarm
};
```

约束如下：

1. `SystemEvent` 必须定长、可复制、无 `QString`、无动态容器、无指针、无 `optional`。
2. 无效字段必须清零，并由 `flags` 判定；消费者不得把零值自行解释为有效业务标识。
3. `argument0/argument1` 只允许承载错误码登记表明确规定的数值，例如端口号、协议长度、超时毫秒数、丢失
   条数或厂家错误码；不得把它们当作无类型万能载荷。
4. 人类可读文本由 `messageTemplateId + eventCode + 定长参数` 在展示边界格式化。原始厂家文本若必须保留，
   应在协议原始记录或专用归档边界保存，不能进入实时 `SystemEvent` 热路径。
5. `requestId` 由控制平面分配，用于将本地受理和发送结果关联到原请求；`traceId` 用于跨模块处理链追踪，
   两者不得合并。

## 6. 事件码合同

`eventCode` 是跨版本持久化标识，采用 32 位无符号值：

```text
31                    24 23                    16 15                     0
+-----------------------+------------------------+------------------------+
| domain（登记值）       | component（登记值）    | reason（登记值）       |
+-----------------------+------------------------+------------------------+
```

- `domain` 必须与 `SystemEvent::domain` 一致，用于离线数据自校验；
- `component` 标识域内稳定组件类别，不使用 C++ 类名，避免重构导致历史码变化；
- `reason` 标识具体事实；已发布的含义永久保留，不得改名复用；
- 未登记事件码不得写入持久化存储；未知厂家码放入登记事件的 `argument0`。

当前登记的 `ControlTaskLifecycleChanged = 0x04010003` 属于 Control 域，必须携带有效
`TaskId` 与 `argument0`。`argument0` 的稳定取值为 `1=创建`、`2=激活`、`3=完成`、`4=归档`；
它记录本地任务事实，不表示设备确认。Web Reader 将事件码与参数映射为中文活动日志，
ArchiveReader 按独立 cursor 将同一事实写入 `system_events`。

2026-09-28 追加登记两条控制参数更新故障码，用于满足接口合同“500 必须形成 SystemEvent”的要求：

| 事件码 | 域 / 级别 | 必填上下文 | 事实含义 |
| --- | --- | --- | --- |
| `ControlTaskControlSettingsPersistFailed = 0x04010004` | Control / Error | `TaskId` | 任务控制参数快照未提交 SQLite；本地请求结果仍由 HTTP 返回 |
| `ControlTaskControlSettingsInvariantFailed = 0x04010005` | Control / Fault | `TaskId` | 任务快照已更新但设备运行副本替换失败；内部不变量被破坏 |

两条事件都由存储线程的事件 Owner 发布，只记录已发生事实，不改变任务或设备状态，也不重试；发布失败不回滚
已经确认的本地结果。

第一批实现前应建立独立事件码登记表。建议预留但暂不创建空壳枚举项；只登记当前代码能够真实产生、测试和
处置的事件。

## 7. CommandResult、State 与 SystemEvent 的边界

一次控制失败可以同时产生不同语义的结果，但不能互相替代：

```text
本地采集控制请求
   -> DeviceController
       |-> 本地请求结果返回唯一请求者；不表示设备收到或起控
       |-> LatestState<DeviceState>：本地采集或心跳当前状态变化时 set
      `-> EventTopic<SystemEvent>：发布值得观测或归档的已发生事实
```

- 普通参数校验失败应直接返回原请求者；只有需要审计、告警或运维统计时才额外发布事件。
- `Fault/Alarm` 事件不会自动代表故障仍然存在；当前故障状态属于相应 `LatestState`。
- 设备没有 ACK；`succeeded` 只表示本地状态、必要持久化和本地 UDP 发送成功，不表示设备执行成功。
- 50 ms 参数包随 DP/SP 心跳自动暂停和恢复，正常心跳门控不产生控制错误风暴。

## 8. 持久化与丢失语义

`EventTopic<SystemEvent>` 是有界事件日志，不是数据库，也不承诺无限期保存：

1. `ArchiveReader` 维护独立 `eventCommittedCursor`，按序 `readAfter()`；数据库事务成功后才推进 cursor。
2. 归档不得阻塞 Producer。归档落后超过 Topic 容量时必须记录 sequence gap，并产生受抑制的 System 域
   Error/Fault；不得循环发布事件造成错误风暴。
3. 数据库至少保存 `topicSequence`、完整 `SampleMeta`、完整 `SystemEvent` 和应用版本/事件码表版本。
4. 查询时根据持久化的事件码版本解析模板，不能把展示后的中文字符串作为唯一事实。
5. 如果未来提出“断电也不能丢一条审计事件”的硬要求，应在归档边界增加 WAL/journal；不得把内存 Topic 改成
   无界队列，也不得让慢磁盘反压实时 Producer。

建议第一版 EventTopic 容量按“可容忍的最长归档停顿时间 × 事件峰值速率”配置，并以覆盖次数和归档 gap 作为
运行指标。容量必须在启动时固定分配。

## 9. 生产门禁与错误风暴抑制

发布前至少验证：

- domain、severity、eventCode 的 domain 位互相一致；
- 所有 flags 之外的可选字段为零；
- eventCode 已登记，参数含义与登记表一致；
- SampleMeta 中事件发生时间与单调接收时间有效；
- Track 域事件携带 `trackId` 时必须同时携带 `deviceId`；Control 域关联请求时必须填写 `requestId`。

同一根因高频重复时，由事件 Producer 按错误码和主体进行有界聚合或限频，聚合结果使用参数记录重复次数与
时间窗。`EventTopic` 本身不做业务去重、限频、状态机或告警确认。

## 10. 第一阶段验收标准

1. 仅实现一个定长 `SystemEvent` 和一个 `EventTopic<SystemEvent>`，不新增四个错误 Topic。
2. `sizeof`、`alignof`、字段偏移、平凡复制性和无动态分配由自动化测试锁定。
3. Device、Track、System、Control 每个域至少有一个真实事件码和生产门禁测试。
4. EventTopic 覆盖、顺序读取、独立 cursor、gap 和固定批量读取测试通过。
5. 归档测试证明“提交成功才推进 cursor”，并证明慢归档不会阻塞事件 Producer。
6. CommandResult、LatestState 与 SystemEvent 的职责边界具有回归测试，不允许用事件代替同步结果。
