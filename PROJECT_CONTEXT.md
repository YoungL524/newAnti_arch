# 项目上下文与新仓库启动交接

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
| `doc/architecture/01-业务架构.md` | 保留不依赖 SystemBus 的业务范围和角色 |
| `doc/architecture/03-数据架构.md` | 核心数据所有权和共享数据域总览 |
| `doc/architecture/04-技术架构.md` | Qt/C++、线程、端口和目标内部技术结构 |

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
│  │  ├─ display/                 DisplayReader 和显示投影
│  │  ├─ storage/                 StorageReader
│  │  ├─ web/                     WebReader 和 WebBridge
│  │  └─ plugin/                  PluginReader 与插件能力边界
│  ├─ persistence/                SQLite、仓储和迁移；不放 Topic
│  ├─ metrics/                    Topic 水位、cursor lag、gap 和时延指标
│  └─ main.cpp
├─ tests/                         新架构单元、集成、压力和耐久测试
├─ tools/                         test_comm、协议抓包和数据生成工具
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
- 当前只存在可用 `TrackPacket`；`PointPacket`、`OpticalPacket`、Device/Task/System State 和统一 SystemEvent 需要真实合同后再实现。
- 系统统一度量衡文档尚需补齐，特别是坐标系、绝对航向、俯仰、横滚、时间基准和高度定义。
- 新共享数据域接口目前只完成架构文档，生产代码尚未实现。

## 9. 新仓库第一阶段实施顺序

1. 建立 CMake、Qt 5.14.2/C++17、测试入口和 `core/datadomain/` 目录；
2. 实现 `TopicTypes`、非模板 `TopicDoorbell`、`HistoryTopic<T>`、`LatestState<T>`、`EventTopic<T>`；
3. 用 `HistoryTopic<TrackPacket>` 完成容量覆盖、`latest()`、分页 `readAfter()`、Gap/Ahead、多 cursor、共享只读对象和 Doorbell 测试；
4. 建立最小通信入口和严格 `RadarProtocolAdapter`，同步调用 Mapper 与 Recognizer；
5. 实现 `DisplayReader`，维护每 Topic cursor 和显示投影；
6. 实现 `StorageReader`，数据库 commit 成功后推进 committed cursor；
7. 实现 `DeviceController` 与每设备命令 FIFO，再迁移 ACK/状态/Event；
8. 最后迁移 Web、插件、DPH、历史查询和其他分支。

第一阶段不要创建空 `PointPacket/OpticalPacket/State/Event` 只为让架构图可编译，也不要复制旧 `core/bus/` 作为过渡基础。

## 10. 第一阶段 Topic 验收标准

- 容量 3 时发布 1～5，保留范围必须为 `[3,5]`，`latest()` 返回 5；
- `readAfter(0)` 必须报告缺口 `[1,2]` 并返回 3～5；
- `readAfter(3,1)` 返回 4 且 `hasMore=true`；
- cursor 大于当前 newest 时返回 `Ahead`，不能永久空等；
- 两个 Reader 使用不同 cursor，互不修改、互不阻塞；
- 读取不从 Topic 删除数据；
- 多 Reader 得到同一个不可变业务对象，只复制只读句柄；
- `LatestState` 连续 set 只保留最后完整值，revision 单调；
- `EventTopic` 顺序读取并准确报告覆盖 gap，不暴露会误导状态语义的 `latest()`；
- Doorbell 不携带业务数据，合并通知后版本不倒退；
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
