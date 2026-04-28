## Stage A: RadarDataProcessor 接管正向流
**Goal**: 用 `RadarDataProcessor` 替代 `AppController`/`DataProcessorThread` 的正向收包处理，完成解析、总线发布和航迹写库。
**Success Criteria**:
- 通信层收包后由 `RadarDataProcessor` 处理
- `tracks` 表持续写入有效航迹
- `system_events` 不再出现无意义 `comm_rx`
**Tests**:
- 启动 `core` + `test_comm`
- `work` 后 `tracks` 表持续新增记录
- `standby` 后 `tracks` 表停止新增
**Status**: Complete

## Stage B: BusToCommRouter 接管反向流
**Goal**: 用 `BusToCommRouter` 接管反向总线订阅与发包职责。
**Success Criteria**:
- `AppController` 不再维护旧线程式反向流组件
- `BusToCommRouter` 订阅 `SystemBus` 并调用 `CommunicationManager::sendTo`
- `workStandby` 等反向命令可正常路由到 `radar_cpu/workmode`
**Tests**:
- 运行时 `BusToCommRouter` 成功订阅并输出订阅日志
- 从总线注入 `workStandby` 数据包后，对端收到命令
- `core` 现有 `sendWorkModeCommand()` 功能不受影响
**Status**: Complete

## Stage C: 删除 DataProcessorThread
**Goal**: 从工程与代码中完全移除 `DataProcessorThread`，清理遗留依赖。
**Success Criteria**:
- `core.pro` 不再编译 `DataProcessorThread`
- 代码中无 `DataProcessorThread` 引用
- 架构只保留 `RadarDataProcessor` + `BusToCommRouter`
**Tests**:
- 全工程可编译
- 运行路径与阶段 B 一致
**Status**: Complete

## Stage D: 架构收口与配置驱动化
**Goal**: 消除 `AppController` 中的硬编码路由/设备映射，收口到配置与专用模块。
**Success Criteria**:
- 端点 sourceType 与反向路由规则不再硬编码在 `AppController`
- DPH 发送策略具备独立配置或独立组件承载
- 架构文档同步到当前实现
**Tests**:
- 增加新端点时不需要修改 `AppController` 业务代码
- 现有 `radar_cpu` 路径行为不变
**Status**: Complete

## Stage E: 入口治理与代码质量基线
**Goal**: 为 `core` 建立贴合 Qt/协议工程的评分基线，并优先清理入口文件热点。
**Success Criteria**:
- `test_comm` 不再纳入主项目质量评分
- `core/main.cpp` 的职责拆分完成，入口复杂度下降
- 评分结果更聚焦 `core` 主流程代码
**Tests**:
- 使用项目级 `.fuckucoderc.json` 重新分析时仅覆盖 `core`
- `core/main.cpp` 不再内联命令分发与退出清理细节
- 启动横幅、命令帮助、工作/待机/退出行为保持不变
**Status**: In Progress

## Stage F: AppController 装配收口与质量治理
**Goal**: 在保持 `processor.json` 配置驱动架构的前提下，拆分 `AppController` 的初始化与装配职责。
**Success Criteria**:
- 数据访问层、通信层、processor 装配流程分别独立
- `AppController.cpp` 不再集中承载端点源映射、反向路由与 DPH 装配细节
- 数据库建表、端点添加、错误事件写入等关键返回值具备显式处理
**Tests**:
- `processor.json` 中的 endpointSources/defaultRoute/routes/dphPublisher 行为保持不变
- 缺失配置时仍按既有默认值回退
- 重新评分后 `core/AppController.cpp` 热点下降或问题类型收敛
**Status**: Complete

## Stage G: DataStore 稳定性与错误处理治理
**Goal**: 清理 `DataStore` 中影响核心层稳定性的连接释放、事务与查询执行问题，作为 `SystemBus`/`AppController` 的配套支撑治理。
**Success Criteria**:
- 数据库连接建立失败、关闭、移除的路径明确且无悬空连接对象风险
- SQLite PRAGMA、事务提交/回滚、查询执行具备显式错误处理
- `DataStore` 对外接口与 `AppController` 调用语义保持不变
**Tests**:
- `connectFromConfig()` 对 SQLite/网络数据库分支行为保持不变
- 批量插入、查询、DDL 执行失败时日志更明确
- 重新评分后 `core/data/DataStore.cpp` 热点下降或错误处理项收敛
**Status**: In Progress（已暂停，等待核心层治理完成后再评估）

## Stage H: SystemBus 契约收紧
**Goal**: 明确 SystemBus 生命周期状态机、错误传播边界和调用端校验，巩固核心总线契约。
**Success Criteria**:
- `SystemBus` 具备显式 `State` 枚举（`Running` / `ShutDown`）和对应查询方法
- `ISystemBus` 接口增加 `isShutdown()` 契约
- `publish()` 仅在成功入队后计数，队列满时输出诊断日志
- `BusWorker` 停止路径移除无效 `quit()` 调用，统一通过队列 `shutdown` 退出
- 所有消费端的 `publish()` 返回值得到显式处理
- `AppController::start()` 校验总线运行状态
**Tests**:
- 编译通过，无新增警告
- 日志中可见 `[SystemBus]` 中文前缀和关闭日志
**Status**: Complete

## Stage I: AppController 边界收口
**Goal**: 消除 `AppController` 与组件之间的配置重复，明确装配器职责边界。
**Success Criteria**:
- `BusToCommRouter` 构造函数不再预注册路由，路由配置权统一归 `AppController`
- 配置回退路径（`processor.json` 缺失）具备诊断日志
- `AppController::start()` 在总线未就绪时拒绝启动
**Tests**:
- 正常配置与缺失配置两种场景下日志输出正确
- 编译通过，行为与重构前一致
**Status**: Complete
