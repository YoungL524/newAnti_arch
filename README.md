# NewAntiDrone 新架构基座

本仓库正在重建反无人机雷达上位机后端。目标架构是“共享强类型数据域 + Consumer 游标读取 + 独立 Command 控制平面”，权威约束见 [共享强类型数据域与游标读取模型](doc/architecture/09-共享强类型数据域与游标读取模型.md)。

## 当前完成度

当前已经建立：

- Qt 5.14.2、C++17、CMake 与 QtTest 骨架；
- `Sample<T>` 的最小不可变事实包装；
- `HistoryTopic<T>` 共享有界 RingBuffer；
- `latest()`、`readAfter(cursor, maxItems)`、Gap/Ahead 与覆盖统计；
- `LatestState<T>` 的 KeepLast(1)、原子完整替换和 revision；
- 不暴露 `latest()` 的 `EventTopic<T>` 有界事件日志；
- 不携带业务数据、支持跨线程合并水位的 `TopicDoorbell`；
- 紧凑 `SampleMeta/TrackPacket/TrackKey/SystemEvent` 及其发布前门禁；
- `DeviceState/TaskState/SystemState` 完整状态类型、统一门禁与布局基准；
- Web 侧 `TrackWebSocketService` 与共享 `ExecutionLane` 合并调度；
- 存储侧 `ArchiveReader` 的双 Topic 独立 cursor、固定 ScratchBuffer 和提交后推进语义；
- `SqliteArchiveRepository` 的六表初始化、WAL、运行实例诊断和强类型批量事务；
- `DeviceConfigService` 的 SQLite 配置真源、不可变 revision、端点校验和运行时句柄激活；
- `TaskService` 的任务持久化、多次采集会话、启停状态及 AcquisitionId 归档绑定；
- 旧 React UI 的视觉与交互基线，以及新 WebReader/控制面待接接口清单；
- 协议无关的 `TrackPublisher -> TrackTopic <- TrackWebSocketService -> QWebSocket` 自动化回归；
- 容量覆盖、分页、多 Reader、并发 Producer、慢 Consumer、gap、唤醒合并和 SQLite 文件级测试。

当前数据域至 SQLite 的归档闭环、设备配置 owner、任务/采集会话 owner，以及首个
`TrackTopic -> TrackWebSocketService -> React` 显示出口已经完成验证。用于验证的旧协议 Producer、模拟器和
硬编码联试组合根已经在确认地图正常显示后删除；正式协议确定后只需新增边界 Adapter，不得改动 Topic 之后的链路。

## 构建与测试

已验证工具链：Qt 5.14.2、MinGW-w64 GCC 7.3.0、CMake 4.3.4。

在 PowerShell 中执行一键基线：

```powershell
./scripts/test_baseline.ps1
```

指定独立构建目录：

```powershell
./scripts/test_baseline.ps1 -BuildDir build-debug
```

脚本会依次运行中文 Doxygen 审计、CMake 配置、编译和 CTest。`build*` 是本地产物，已由 `.gitignore` 排除。

## 阅读入口

1. `AGENTS.md`：不可违反的架构和注释规则；
2. `PROJECT_CONTEXT.md`：旧系统基线、迁移顺序和已知问题；
3. `doc/architecture/00-架构索引.md`：现行文档、优先级和待补合同；
4. `doc/contracts/统一单位坐标时间契约.md`：SI、RFU/ENU/WGS-84、6DOF 和 UTC/steady；
5. `doc/contracts/TrackPacket契约.md`：SampleMeta、标准航迹、TrackKey 和发布门禁；
6. `doc/contracts/运行状态模型契约.md`：三类完整 State、唯一 Owner 和状态转换；
7. `core/model/Sample.h`：Topic 共享事实的当前最小实现；
8. `core/datadomain/TopicTypes.h`：cursor、gap、记录和统计合同；
9. `core/datadomain/HistoryTopic.h`：RingBuffer 写入与主动读取；
10. `core/datadomain/TopicDoorbell.h`：只通知水位的 Qt Doorbell；
11. `core/archive/ArchiveReader.h`：双 Topic cursor 与提交后推进边界；
12. `core/archive/SqliteArchiveRepository.h`：真实 QSQLITE 单写者归档实现。
13. `core/config/DeviceConfigService.h`：设备 revision 与运行时句柄 owner；
14. `core/task/TaskService.h`：任务和多次采集会话 owner。
15. `core/web/TrackWebSocketService.h`：协议无关的 TrackTopic Web Consumer；
16. `doc/migration/旧UI迁移与新后端对接清单.md`：React UI 的复用边界与待接 API。

UI 独立验收：

```powershell
cd ui
npm ci
npm test
npm run build
```

高德地图配置从 `ui/.env.example` 创建本机 `ui/.env`，真实密钥不得提交。

## 已完成的端到端验证

2026-09-03 曾以旧设备协议和 CSV 模拟器作为一次性输入夹具，验证
`UDP -> ProtocolAdapter -> TrackPublisher -> TrackTopic <- TrackWebSocketService -> React` 全链路，浏览器地图已正常显示真实报文映射出的航迹。旧协议结构、模拟器、硬编码 HTTP 视图及其测试随后全部清退；该结果只作为架构验收证据，不再作为可运行入口。
其后新增的 WebSocket 测试直接构造标准 TrackPacket，持续保护初始 snapshot、增量 upsert/removal、独立 cursor 和有界 Web 派生状态，不再依赖任何厂家协议夹具。
# 心跳链联试

运行 `scripts/test_baseline.ps1 -BuildDir build-baseline` 后，在包含 Qt 5.14.2 bin 的 PATH 环境中
启动 `build-baseline/core/NewAntiDrone.exe`。内部UDP绑定 `127.0.0.1:7788`，接收正式
`0x0101/0x0303` 心跳。打开 `ui/heartbeat.html` 查看正式状态域输出；WebSocket为
`ws://127.0.0.1:8090`。无需启动旧后端。页面只展示状态，不产生模拟心跳。

绿色为在线，黄色为主动离线或2秒迟到，灰色为未收到或5秒超时。DP/SP独立判定，位图低四位
对应1～4号阵面。当前可执行入口只装配心跳链，其他服务仍按后续步骤接入。
