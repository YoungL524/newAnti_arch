# 项目上下文与新仓库启动交接

> **2026-09-18 协议/架构复核断点**：已以表内版本 `20260915` Excel 重新对齐公共封装、
> `0x0001/0x0100/0x0101/0x0303`、FPGA 128 字节状态区和 224 字节航迹；设备/任务 HTTP 统一为
> `/api/v1`，网络身份字段为 `deviceId/taskId`，成功/失败响应已统一为 `data` 与
> `error.code/message`，CRUD 状态码不再依赖中文文本判断。当前后端全量编译、19/19 CTest、8/8 前端测试、
> C++14/C11 交付头语法检查及中文 Doxygen 审计均通过。恢复后从以下三项继续：
> 1. 如需对 HTTP 状态码做 Socket 级黑盒验收，在现有 `StorageRuntimeContract` 之上增加最小路由测试；
> 2. 最新 Excel、新增 Core/Web/测试/模拟器均是正式成果；`output.txt` 是用户提供材料，不擅自删除；
> 3. 保持真实航迹拒绝未绑定上下文，待 Command Owner/ACK 和采集会话落地后，再由唯一 owner
> 调用 `TrackIngressProducer::bind()`；禁止伪造 ID 或增加第二条链路。

> **2026-09-18 控制平面前置评审**：任务选择只是 `Ready`，UI 已改为“已就绪”，不再伪装“进行中”；
> 开始采集按钮仍禁用，因 `ApplicationController/DeviceController/per-device FIFO/ACK` 尚未落地。
> 实现时必须同时满足：心跳并入 `DeviceController` 权威快照，不保留两个 `DeviceState` Owner；
> 设备 ACK 后按任务 faceMask 创建各阵面采集会话，再由唯一编排者绑定 `TrackIngressProducer`。
> Schema 8 只为位置仍为全零的内置单/四面阵台账补齐 `118.65303,31.93494,63.9288m`，
> 不覆盖用户修改值；同一默认已同步到 UI 新建表单和地图中心。

> 2026-09-17 当前基线：内部 UDP 为 `127.0.0.1:7788`，设备/任务 HTTP 为 `127.0.0.1:8088`，
> `DeviceState` 与航迹共用 `ws://127.0.0.1:8089/ws/v1`。协议唯一字段底表为表内版本 `20260915` 的最新版
> Excel；`0x0100` Adapter、TrackTopic、归档与 Web Consumer 已存在，但生产环境尚缺控制 ACK 后创建采集会话
> 并调用 `TrackIngressProducer::bind()` 的装配，故真实航迹会被明确拒绝而不是使用伪造身份。以下旧日期条目只作迁移记录。

> 2026-09-09 core收敛：ApplicationRuntime统一拥有心跳链依赖和端口启动、失败回滚、关闭顺序。
> main只创建Qt应用和运行时；已验证Web端口冲突释放UDP、重复启动保持已有绑定、停止再启动。
> 本版为心跳后端基线；React主界面状态对接、Command、事件归档装配仍待后续实现。

> 2026-09-09 心跳链已装配：NewAntiDrone可执行入口绑定127.0.0.1:7788，
> UDP -> DcmpProtocolAdapter -> HeartbeatStateOwner -> DeviceState -> ExecutionLane -> StateWebSocketService。
> 状态出口ws://127.0.0.1:8090，联试页面ui/heartbeat.html；2秒迟到黄、5秒超时灰、重新上线绿。
> CoreDataDomain包含三个LatestState；本轮仅DeviceState写入，Task/System等待真实owner。
> 当前入口不包含命令、归档装配或完整React台账API，不代表完整产品已完成。以下历史进度以本条为准。

> 2026-09-09：已新增 core/comm/UdpEndpoint，支持独占绑定、唯一同步原始数据入口、显式目标发送、
> 固定接收缓冲及每轮64包预算；真实UDP回归已通过。尚未装配sock2Internal运行实例、心跳Codec、
> 状态owner或Web状态出口；不得把传输类完成当作心跳端到端完成。
> 心跳DcmpProtocolAdapter已实现：小端解码0x0101/0x0303，严格校验封装及载荷，真实UDP测试通过。
> 统一线协议已确认小端；目前只同步交付强类型心跳，尚未连接状态owner与Web指示灯。

> 2026-09-08 文档收敛：当前阅读入口为 `doc/architecture/00-架构索引.md`。
> 以下旧链路和启动步骤是历史记录；当前线协议按 `doc/contracts/DCMP统一报文封装契约.md`。
> 公共封装与四类载荷已实现，DP 返回已嵌入224字节 DpTrack，完整载荷608字节，Adapter映射待接入；不能把旧 DPH 周期或字段当作当前协议。

> 快照日期：2026-08-21
> 用途：在切换目录、仓库或新会话后恢复当前项目背景、已确认架构、旧系统现状和下一步工作。
> 目标读者：第一次接触本项目的后端开发者或自动化开发代理。

## 1. 当前目标

本项目是多设备反无人机雷达上位机。核心目标是长期维护、实时显示、多设备接入、航迹存储、插件扩展及 7×24 小时稳定运行。

当前阶段决定停止继续扩展旧 `DataPacket/SystemBus` 架构，重新建立干净的后端基座。新代码必须先实现共享强类型数据域，再逐条迁移旧系统已验证的业务功能。

## 2. 仓库切换建议

推荐使用“新兄弟目录/新仓库 + 旧仓库只读归档”，不要在同一个源码树中建立 `Old version/` 并搬入全部旧代码。

推荐目录：

```text
D:\NewAntiDrone-newArch\
├─ newAnti_arch_legacy_20260821\   当前仓库，只读归档和联试对照
└─ newAnti_arch\                   新架构仓库，保持原工作路径
```

也可以保留当前目录不改名，新建 `newAnti_arch_v2`；二者的架构效果相同。若依赖脚本和工具大量写死 `newAnti_arch`，优先让新仓库继续使用原目录名。

不建议仓库内 `Old version/` 的原因：

- CMake、qmake、IDE 索引和全文搜索会持续看到两套同名类型；
- 自动化开发容易错误引用旧 `SystemBus/DataPacket`；
- 文档搜索会混合现行规范和历史规范；
- 仓库体积、构建清单和静态分析噪声增加；
- Git 本身已经保存历史，不需要再复制一份旧源码作为版本控制。

旧仓库应通过 Git commit/tag 和只读目录保留，而不是复制进新仓库。

## 3. 切换前必须完成

在移动或重命名当前目录前按顺序完成：

1. 提交当前架构文档和 `AGENTS.md`，确保工作区没有未确认修改；
2. 在旧仓库创建建议标签 `legacy-systembus-final-20260821`；
3. 记录 commit hash、编译器、Qt、CMake、测试结果和当前配置；
4. 完成一次旧系统核心功能联试：创建任务、启停接收、50 ms 数据流、航迹显示、Web snapshot/delta、存储、停止后再启动；
5. 保存一份可运行旧程序和必要配置，但不要把 `build/` 复制到新仓库；
6. 将旧目录改为只读参考，禁止在新旧两边同时开发同一功能；
7. 创建新仓库并首先复制本文件、`AGENTS.md` 和权威架构文档；
8. 新仓库第一次提交只包含目录骨架、文档、构建入口和空测试入口，不复制旧 `core/bus/`。

当前架构文档仍处于未提交状态时，不得直接重命名或丢弃当前工作区。

## 4. 新仓库必须准备的文档

### 4.1 第一批直接复制

| 文档 | 作用 |
| --- | --- |
| `AGENTS.md` | 编码规则、中文 Doxygen、架构图、数据平面和 Command 禁止项 |
| `PROJECT_CONTEXT.md` | 恢复当前对话上下文、旧系统现状、迁移顺序和已知问题 |
| `doc/architecture/09-共享强类型数据域与游标读取模型.md` | `HistoryTopic/LatestState/EventTopic/Doorbell/cursor` 权威规范 |

### 4.2 新仓库创建后立即补齐

| 建议文件 | 必须回答的问题 |
| --- | --- |
| `README.md` | 软件目标、当前完成度、构建方式、启动方式、测试入口 |
| `doc/architecture/00-架构索引.md` | 哪些文档是现行规范，哪些是 ADR、合同、迁移或测试文档 |
| `doc/architecture/02-Command控制平面.md` | ApplicationController、DeviceController、每设备 FIFO、ACK、超时、Result |
| `doc/architecture/03-线程与生命周期.md` | GUI、通信、存储、WorkerPool 的线程亲和及启动/停机顺序 |
| `doc/contracts/统一单位坐标时间契约.md` | 经纬高、绝对航向、俯仰、横滚、角度方向、长度单位、UTC/单调时间 |
| `doc/contracts/TrackPacket契约.md` | 字段、单位、可空性、track key、识别状态、source/sample 元数据 |
| `doc/contracts/设备状态与系统事件契约.md` | Device/Task/System State 及 SystemEvent severity 和字段 |
| `doc/contracts/厂家协议适配边界.md` | 包头、命令字、声明长度、包尾、CRC、ACK 关联和错误分类 |
| `doc/migration/旧系统资产映射.md` | 每个旧文件的保留、重写、参考或删除结论 |
| `doc/test/核心链路验收基线.md` | 创建任务、启停、接收、显示、存储、4设备压力和 7×24 门禁 |
| `doc/decisions/ADR-0001-共享强类型数据域.md` | 为什么选择共享 RingBuffer/cursor，而不是 Broker 或 per-consumer Queue |

旧 `06/07/08` 文档包含有价值的字段、容量、测试和历史审计，但混有旧 `SystemBus` 设计。不要原样复制到新仓库，应把仍有效的内容分别提炼进上述 contracts、migration 和 test 文档。

### 4.3 新仓库目录骨架

可以延续当前“根目录 + core + config + ui + doc + tests”的基本布局，但数据域、持久化和旧总线必须在目录上分开：

```text
newAnti_arch/
├─ CMakeLists.txt
├─ AGENTS.md
├─ PROJECT_CONTEXT.md
├─ cmake/                         CMake 公共函数和平台配置
├─ config/                        端点、设备和运行配置
├─ core/
│  ├─ app/                        ApplicationController、组合根和生命周期
│  ├─ model/                      TrackPacket、State、Event、Sample 等业务模型
│  ├─ datadomain/                 HistoryTopic、LatestState、EventTopic、Doorbell
│  ├─ comm/                       Endpoint、socket 和通信生命周期
│  ├─ protocol/                   厂家协议布局、校验、编码和解码
│  ├─ processor/                  Mapper、同步识别和轻量一对一流水线
│  ├─ control/                    DeviceController、Task owner、每设备命令 FIFO
│  ├─ readers/
│  │  ├─ web/                     WebReader 和浏览器序列化边界
│  │  ├─ storage/                 StorageReader
│  │  ├─ web/                     WebReader 和 WebBridge
│  │  └─ plugin/                  PluginReader 与插件能力边界
│  ├─ persistence/                SQLite、仓储和迁移；不放 Topic
│  ├─ metrics/                    Topic 水位、cursor lag、gap 和时延指标
│  └─ main.cpp
├─ tests/                         新架构单元、集成、压力和耐久测试
├─ tools/                         协议抓包和数据生成工具（按真实需要新增）
├─ ui/                            React 前端；可在后端基座稳定后迁入
└─ doc/                           architecture、contracts、migration、test、decisions
```

若继续使用目录名 `core/data/`，必须把它限定为数据库/持久化层；共享 Topic 仍放 `core/datadomain/`。`build/` 是本机构建产物，只加入 `.gitignore`，不作为源码目录复制或归档。

## 5. 已确认且不得反复讨论的架构决定

1. 逻辑中心化、执行去中心化；`CoreDataDomain` 由组合根持有，但不成为万能 service locator。
2. Producer 只产生业务事实并调用 `publish()/set()`。
3. `HistoryTopic<T>` 是共享有界 RingBuffer，Consumer 使用 `latest()/readAfter(cursor)` 主动读取。
4. Consumer 不维护 `Queue<DataPacket>`，只维护每 Topic 独立 cursor/revision 和轻量投影状态。
5. Doorbell 只携带 `newestSequence/revision`，允许重复、延迟和合并，不传业务数据。
6. Topic retention 与 Consumer 无关；慢 Consumer 不阻塞 Producer，落后时明确报告 sequence gap。
7. State 使用 `LatestState<T>` KeepLast(1)，Event 使用有界 `EventTopic<SystemEvent>`。
8. Info、Warning、Error、Fault、Alarm 第一版统一放入 `SystemEvent`，不建 ErrorTopic。
9. Command 不走普通 Topic，固定为 `ApplicationController -> DeviceController -> Per-Device FIFO -> ProtocolAdapter -> Device`。
10. 进程内使用强类型模型；网络、文件、录制回放等边界才使用 WireEnvelope/协议 DTO。
11. 第一版使用 `QMutex`，不实现 DDS 全套能力、复杂 QoS、lock-free 或跨进程共享内存。
12. `Yaw` 从头到尾都是以正北为 0°、顺时针为正的绝对航向，不与阵面方位叠加换算。

## 6. 旧系统当前真实链路

旧仓库的航迹主链为：

```text
UdpEndpoint
  -> CommunicationManager（补 endpointId）
  -> RadarDataProcessor
  -> GeneralPackage（厂家临时对象）
  -> TrackPacketMapper
  -> TrackRecognizer
  -> DataPacket/SystemBus
  -> TrackDisplayManager
  -> TrackDisplayUpdate/DataPacket/SystemBus
  -> TrackDisplayWebBridge
  -> WebSocket snapshot/delta
```

存储分支：

```text
DataPacket/SystemBus 中央 FIFO（默认容量 10000）
  -> TrackStorage 自有 FIFO（容量 4096）
  -> 存储线程
  -> SQLite
```

控制分支：

```text
RadarWorkModeController
  -> BusWorkModeSender
  -> SystemBus
  -> BusToCommRouter
  -> CommunicationManager
  -> UdpEndpoint
  -> Device
```

这些链路只用于功能参考、联试和结果对照，不能复制成新仓库的基础设施。

## 7. 已验证的业务基线

新仓库迁移时不得丢失：

- 设备配置加载与设备列表；
- 创建任务、开始任务、停止任务；
- 开始/停止 UDP 接收；
- 50 ms 厂家报文接入和标准航迹映射；
- 航迹显示及雷达绝对航向生效；
- WebSocket 新连接 snapshot、稳态 upsert/remove delta 和 revision；
- 航迹历史存储与查询；
- DPH 周期发送和设备工作模式控制；
- 停止后再次启动、断线和重连；
- 多设备独立状态及后续 4 台设备并发。

每迁移一条链路，只允许一个正式副作用执行者；shadow 路径只能比较，不能双显示、双写数据库或双发设备命令。

## 8. 当前已知问题和未完成事项

- `RadarDataProcessor` 仍存在按长度/近似长度猜测厂家结构的问题；目标适配器必须校验端点协议、包头、命令字、声明长度、包尾和 CRC，删除近似匹配。
- `RadarWorkModeController` 当前超时可能把 Starting/Stopping 乐观推进到 Working/Standby；新控制平面禁止无 ACK 伪成功。
- `DataPacket/SystemBus` 仍承担显示、存储、Web、参数、错误和命令用途，不能一次性删除。
- 当前存储链是中央 FIFO 加存储 FIFO 两级排队。
- 当前存在可用 `TrackPacket` 和统一 `SystemEvent`；上位机不存在独立点迹链，禁止实现
  `PointPacket/PointTopic`。`OpticalPacket` 与 Device/Task/System State 仍须等待真实合同。
- 统一度量衡与 TrackPacket 字段合同已在新仓库冻结；厂家 RCS 单位、Altitude 垂直基准、径向速度原始符号和时间同步能力仍需协议证据确认。
- `HistoryTopic` 已迁移为构造期预分配的内嵌槽位，Reader 使用固定 `TopicScratchBuffer`；
  WebReader 已接入共享 ExecutionLane，不再保存每 Reader 调度合并状态。

## 9. 新仓库第一阶段实施顺序

1. 定义数值型 `EndpointId/DeviceId/TaskId/AcquisitionId`；
2. 重写紧凑定长 `SampleMeta/TrackPacket/TrackKey` 合同；
3. 用 `sizeof`、字段偏移和复制基准验证布局；（已完成）
4. 将 `HistoryTopic` 改为内嵌预分配槽位和调用方固定 ScratchBuffer；（已完成）
5. 实现使用原子 pending bitmask 的合并唤醒 `ExecutionLane`；（已完成）
6. 简化当时的 `TrackDisplayReader`，删除其 `scheduled/running/requestedWatermark` 调度状态；（历史步骤，后因只保留 Web 前端而删除该类）
7. 完成持续发布、慢 Consumer、gap、内存上限和运行期分配测试；
8. 在该基线稳定后再接入协议、归档、Command、Web 和插件分支。

第一阶段禁止创建 `PointPacket/PointTopic`；不要创建空 `OpticalPacket/State/Event` 只为让架构图可编译，也不要复制旧 `core/bus/` 作为过渡基础。

## 10. 第一阶段 Topic 验收标准

- 容量 3 时发布 1～5，保留范围必须为 `[3,5]`，`latest()` 返回 5；
- `readAfter(0)` 必须报告缺口 `[1,2]` 并返回 3～5；
- `readAfter(3,1)` 返回 4 且 `hasMore=true`；
- cursor 大于当前 newest 时返回 `Ahead`，不能永久空等；
- 两个 Reader 使用不同 cursor，互不修改、互不阻塞；
- 读取不从 Topic 删除数据；
- Topic 使用预分配内嵌槽位；Reader 只复制到固定、可复用 ScratchBuffer，不持有槽位地址；
- `LatestState` 连续 set 只保留最后完整值，revision 单调；
- `EventTopic` 顺序读取并准确报告覆盖 gap，不暴露会误导状态语义的 `latest()`；
- Doorbell 不携带业务数据；突发更新由 ExecutionLane 合并，同一 Lane 最多保留一个待执行 Qt 唤醒；
- Reader 阻塞或数据库失败时 Producer 仍可继续覆盖 RingBuffer；
- 所有共享结构使用有界容量，锁内不执行 GUI、数据库、Web 或插件业务。

## 11. 环境与规则

- 当前后端技术栈：Qt 5.14.2、C++17、CMake、MinGW；
- 所有新增或修改代码必须具有准确的中文 Doxygen 注释；
- `.h` 负责文件、类型、接口和字段合同，`.cpp` 每个函数定义前写中文 Doxygen 实现说明；
- 新仓库必须复制并遵守根目录 `AGENTS.md`；
- 当前权威架构入口：`doc/architecture/09-共享强类型数据域与游标读取模型.md`。

## 12. 旧版本冻结验证记录

- 冻结日期：2026-08-21；
- 架构文档固化提交：`818f730`；
- 验证命令：`scripts/test_baseline.ps1 -BuildDir build-test`；
- 中文 Doxygen 审计：通过，共检查 84 个 C++ 文件；
- 后端 Debug 构建：通过，MinGW 与 Qt 5.14.2 配置成功；
- 后端 CTest：12/12 通过，总耗时约 50 秒；
- 前端 Vitest：2 个测试文件、8/8 用例通过；
- 前端生产构建：`tsc -b && vite build` 通过。

以上结果证明旧版在自动化覆盖范围内可编译、可测试，可作为新架构迁移的行为对照基线；它不等同于四设备实机并发、真实厂家报文联试或 7×24 小时耐久验收。旧仓库冻结后只用于查阅、差异比较和必要回退，不再承接新架构开发。

## 13. 新仓库当前实施进度

- 已实现 Qt 5.14.2/C++17/CMake/QtTest 构建骨架；
- 已实现内嵌预分配语义的 `HistoryTopic<T>`、`TopicScratchBuffer<T>`、`TopicDoorbell` 和基础测试；
- 已验证有界覆盖、Gap/Ahead、多 Reader 和 Doorbell 行为；HistoryTopic 内嵌预分配槽位、Reader 固定
  ScratchBuffer 和 ExecutionLane pending bitmask 合并唤醒均已落地；
- 已建立一键 Doxygen 审计、构建和 CTest 基线脚本；
- 已冻结 `doc/contracts/统一单位坐标时间契约.md` 与 `doc/contracts/TrackPacket契约.md`；
- 已实现 `SampleMeta`、真实 `TrackPacket/TrackKey`、`CoreDataDomain::trackTopic` 与 `TrackPublisher` 发布前门禁；
- 已定义零额外存储开销的强类型 `EndpointId/DeviceId/TaskId/AcquisitionId`，并完成紧凑
  `SampleMeta/TrackPacket/TrackKey` 迁移；FaceId 已因与 DeviceId 重复而删除；
- 当前 MinGW x64 实测 `SampleMeta/TrackPacket/TrackKey` 分别为 48/72/16 字节，角度、抵近正速度和厂家
  double 幅值门禁已由测试冻结；
- 已确认产品不提供 Qt 原生地图，原 `TrackDisplayReader` 及其专属测试已删除；实时显示唯一出口为
  `TrackWebSocketService -> React`，避免两套显示派生状态并存；
- 已删除没有真实来源的局部三轴位置/速度和 RFU 临时显示结构，复杂度预算已写入 `AGENTS.md`；
- 已冻结 `SystemEvent` 错误模型合同，实现 56 字节定长事实、Device/Track/System/Control 首批稳定事件码、
  `SystemEventPublisher` 发布门禁及 `CoreDataDomain::eventTopic`；四类事件共享同一 sequence 空间；
- 已增加实时链路压力验收：10 万条单 Producer/独立 cursor Reader 并发运行并最终追平；慢 Consumer 在
  ScratchBuffer 上停顿时 Producer 仍完成额外 1 万条发布，随后精确报告覆盖 gap；
- 4096 条 TrackTopic 槽位与 256 条显示 ScratchBuffer 当前合计 557056 字节（544 KiB），低于冻结的
  600 KiB 有效槽位预算，地址和容量在运行期保持不变；
- 四线程向八个 ExecutionLane slot 发出的 16 万次请求合并为一个初始 Qt 唤醒，证明唤醒数量不随请求数增长；
- 已实现 `core/archive/ArchiveReader`：Track/Event 共用一个存储 Lane slot，但分别维护 committed cursor、
  固定 ScratchBuffer、gap 与提交失败计数；一个 Topic 提交失败不阻止另一个 Topic；
- 已定义 `IArchiveRepository` 强类型同步事务端口，只有整批提交成功后 Reader 才推进对应 cursor；当前使用
  Fake Repository 完成回归验证，具体 SQLite/文件实现及表结构尚未冻结；
- 已形成 `doc/contracts/归档数据库契约.md` 设计草案：建议第一版采用 SQLite 单文件、WAL、单存储写连接、
  强类型事实表和稳定实体外键；保留期限、磁盘上限及是否全量保存 50 ms 观测仍待用户确认；
- 当前上位机不存在独立点迹链；光学与 State 只有真实合同形成后才能加入数据域；
- 已从冻结旧仓库提取雷达真实上行线缆布局：`GeneralPackage=1025 B`、`DataPackageHeader=768 B`、
  `TargetType=144 B`、航迹区偏移 836；当前协议只存在握手字和声明长度，没有包尾或 CRC 字段；
- 曾以旧设备 1025 字节报文完成 `UDP -> ProtocolAdapter -> TrackPublisher -> trackTopic` 端到端验证；
  浏览器确认航迹正常显示后，旧协议结构、UDP Producer、模拟器及专属测试已按决策删除；
- 已迁移旧 React UI 源码并通过 2 个 Vitest 文件 8/8 用例及生产构建；`.env` 密钥与 tsbuildinfo 未迁移；
- UI 目前是复用基线而非已接通的新后端：REST/WebSocket owner、`acquisitionId` 实时键和最终显示投影的收敛项见
  `doc/migration/旧UI迁移与新后端对接清单.md`；其中 `acquisitionId` Web 键和首版 WebReader 已完成；
- 已实现 `TrackWebSocketService` 独立 cursor、固定 ScratchBuffer、ExecutionLane 合并唤醒、每航迹 20 点尾迹与
  256 目标容量上限；新连接取 snapshot，稳态取 upsert/removal delta；
- 已冻结并实现 DeviceState 多设备完整快照、单活动任务 TaskState 和 SystemState 类型；三者共用
  RuntimeStateGate，已锁定元数据、容量、状态组合、尺寸、对齐和字段偏移门禁；尚未接入 CoreDataDomain；
- 已增加协议无关的真实 QWebSocket 回归：直接发布标准 TrackPacket，验收初始 snapshot、增量字段、独立
  cursor 不消费共享历史、目标容量淘汰 removal 以及新客户端当前态快照；
- 进程级联试及用户目视验收已证明 WebSocket snapshot/delta 能驱动 React 地图正常显示；
- 下一步等待正式设备协议后新增边界 Adapter，并由 DeviceConfigService/TaskService 正式 owner 提供 HTTP 视图，
  继续实现 Command 控制平面；不得为兼容旧 UI 恢复 DataPacket/SystemBus 或制造第二条航迹链。
