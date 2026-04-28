# core 数据流图

> 基于 Phase 2.5 实际代码生成，2026-04-24

## 总览

```
                           ┌─────────────────────────────────────────────────────┐
                           │                   core 进程                          │
                           │                                                     │
  ┌──────────┐   UDP Rx    │  ┌─────────────┐  dataReceived   ┌───────────────┐  │
  │          │────────────►│  │  UdpEndpoint │───────────────►│ RadarData-    │  │
  │          │             │  │  (radar_cpu) │  (Qt signal)   │ Processor     │  │
  │          │             │  └─────────────┘                 │               │  │
  │  雷达端   │             │        ▲                         │ ①解析数据     │  │
  │  设备     │             │        │                         │ ②写库 tracks  │  │
  │          │   UDP Tx    │        │ sendTo()                │ ③发布到总线   │  │
  │          │◄────────────│  ┌─────┴───────────┐             └──┬────┬───────┘  │
  │          │             │  │ Communication-  │                │    │          │
  │          │◄────────────│  │ Manager         │                │    │          │
  └──────────┘   UDP Tx    │  │                 │                │    │          │
      (DPH)                │  └─────▲───────▲───┘                │    │          │
                           │        │       │                    │    │          │
                           │        │       │                    │    │          │
                           │   sendTo()  sendTo()                │    │          │
                           │        │       │                    │    │          │
                           │  ┌─────┴──┐ ┌──┴──────────┐        │    │          │
                           │  │ DPH-   │ │ BusToComm-  │        │    │          │
                           │  │Publish-│ │ Router      │◄───┐   │    │          │
                           │  │ er     │ │             │    │   │    │          │
                           │  └────────┘ │ ①订阅总线   │    │   │    │          │
                           │   50ms定时   │ ②解包载荷   │    │   │    │          │
                           │             │ ③路由发包   │    │   │    │          │
                           │             └─────────────┘    │   │    │          │
                           │                                │   │    │          │
                           │         ┌──────────────────────┴───┴────┘          │
                           │         │      SystemBus (单例)                     │
                           │         │      State: Running ──► ShutDown         │
                           │         │                                          │
                           │         │  publish ──► ThreadSafeQueue             │
                           │         │              ──► BusWorker 线程           │
                           │         │              ──► dispatchToSubscribers   │
                           │         └──────────────────────────────────────────┘
                           │                                          │          │
                           │                                 写库     │          │
                           │                                 ┌───────▼────────┐ │
                           │                                 │   DataStore    │ │
                           │                                 │   (SQLite)    │ │
                           │                                 │ tracks        │ │
                           │                                 │ system_events │ │
                           │                                 └────────────────┘ │
                           └─────────────────────────────────────────────────────┘

                           ┌────────────────────┐
  控制台 stdin ────────────►│ main.cpp           │
  (work/standby/quit)      │ → AppController    │
                           │   .sendWorkMode-   │
                           │    Command()        │
                           │   → SystemBus      │
                           │     .publish()     │
                           └────────────────────┘
```

---

## 数据流详解

### Flow ①  正向流：雷达 → 业务 → 总线 → 存储

```
雷达设备
  │  UDP 数据报
  ▼
UdpEndpoint::readPendingDatagrams()
  │  emit dataReceived(endpointId, rawBytes)
  ▼
CommunicationManager
  │  转发 signal: dataReceived(endpointId, data)
  ▼
RadarDataProcessor::onCommDataReceived(endpointId, data)
  │
  ├── data.size() == sizeof(GeneralPackage)
  │     │
  │     ├── insertTrack(ts, endpointId, tgt, sceneNum)
  │     │     └──► DataStore::insert("tracks", row)  ──► SQLite
  │     │
  │     └── SystemBus::publish(DataPacket)
  │           sourceType  = endpointSourceMap[endpointId]
  │           dispatchTarget = UI_OUTPUTWIDGET
  │           payloadType = PAYLOAD_TYPE_GENERAL_PACKAGE
  │
  └── data.size() == sizeof(workStandby)
        │
        └── SystemBus::publish(DataPacket)
              sourceType  = endpointSourceMap[endpointId]
              dispatchTarget = UI_OUTPUTWIDGET
              payloadType = PAYLOAD_TYPE_WORK_STANDBY
```

**关键连接**：`RadarDataProcessor` 构造时通过 `connect(commManager, &CommunicationManager::dataReceived, this, &RadarDataProcessor::onCommDataReceived)` 自动订阅。

---

### Flow ②  反向流：控制台 → 总线 → 路由 → 发包

```
控制台输入 "work" / "standby"
  │
  ▼
main.cpp::handleConsoleCommand()
  │  调用 controller.sendWorkModeCommand(mode)
  ▼
AppController::sendWorkModeCommand(mode)
  │  构造 DataPacket:
  │    sourceType     = UI_CONFIGWIDGET
  │    dispatchTarget = m_reverseTargetDevice (DEVICE_RADAR_NH_KU_1)
  │    payloadType    = PAYLOAD_TYPE_WORK_STANDBY
  │    payload        = packDataToPayload(workStandby{mode})
  │
  │  SystemBus::publish(std::move(packet))
  ▼
SystemBus 分发线程 (BusWorker)
  │  匹配 target == DEVICE_RADAR_NH_KU_1 的订阅
  │  回调 → Qt::QueuedConnection 投递到主线程
  ▼
BusToCommRouter::handlePacket(packet)
  │  resolveRoute(payloadType) → {endpointId: "radar_cpu", targetName: "workmode"}
  │  extractBytes(packet) → QByteArray(workStandby 结构体)
  │
  │  CommunicationManager::sendTo("radar_cpu", "workmode", bytes)
  ▼
UdpEndpoint::sendTo("workmode", bytes)
  │  UDP 发包
  ▼
雷达设备
```

**关键连接**：`BusToCommRouter` 通过 `SystemBus::subscribeByTarget(DEVICE_RADAR_NH_KU_1, callback)` 订阅反向流。

---

### Flow ③  DPH 定时心跳：core → 雷达

```
QTimer (50ms 周期)
  │  timeout signal
  ▼
DphPublisher::sendOnce()
  │  组装 DataPackageHeader (景幅号自增)
  │  QByteArray bytes = reinterpret_cast 序列化
  │
  │  CommunicationManager::sendTo("radar_cpu", "dph", bytes)
  ▼
UdpEndpoint::sendTo("dph", bytes)
  │  UDP 发包
  ▼
雷达设备
```

**注意**：DPH 不经过 SystemBus，直接走 CommunicationManager。

---

### Flow ④  通信错误处理

```
UdpEndpoint 错误
  │  emit error(endpointId, message)
  ▼
CommunicationManager
  │  emit errorOccurred(endpointId, message)
  ▼
AppController::onCommError(endpointId, message)
  │
  ├── insertSystemEvent("error", endpointId, message)
  │     └──► DataStore::insert("system_events", row) ──► SQLite
  │
  └── SystemBus::publish(errorPacket)
        sourceType  = SOURCE_COMMUNICATION
        payloadType = ERR_NETWORK
```

---

## 生命周期

### 启动顺序

```
main()
  │
  ├── 1. QCoreApplication 创建
  ├── 2. AppController 创建
  │
  └── AppController::initialize()
        │
        ├── 3. initializeDataAccess()
        │     ├── ConfigManager::loadConfigDir()    ← 加载 config/ 目录
        │     ├── DataStore 创建 + connectFromConfig()
        │     └── ensureDatabaseSchema()            ← CREATE TABLE IF NOT EXISTS
        │
        ├── 4. initializeCommunication()
        │     ├── CommunicationManager 创建
        │     └── 从 endpoints.json 添加端点
        │
        └── 5. initializeProcessorPipeline()
              ├── connectCoreSignals()              ← errorOccurred → onCommError
              │
              ├── configureRadarProcessor()
              │     ├── RadarDataProcessor 创建     ← 自动订阅 dataReceived
              │     └── registerEndpointSource("radar_cpu", DEVICE_RADAR_NH_KU_1)
              │
              ├── configureBusRouter()
              │     ├── BusToCommRouter 创建
              │     ├── setDefaultRoute()
              │     ├── registerRoute() × 3         ← 从 processor.json 或默认值
              │     └── subscribe(DEVICE_RADAR_NH_KU_1)  ← 订阅 SystemBus
              │
              └── configureDphPublisher()
                    ├── DphPublisher 创建
                    ├── setRoute() + setInterval()
                    └── (不自动 start，等 AppController::start())

  AppController::start()
        │
        ├── 校验 SystemBus::isRunning()             ← 总线未就绪则拒绝启动
        ├── CommunicationManager::startAll()        ← 端点开始监听
        └── DphPublisher::start()                   ← 50ms 定时器启动
```

### 关闭顺序

```
控制台 "quit" / SIGINT / SIGTERM
  │
  ▼
QCoreApplication::quit()
  │  aboutToQuit signal
  ▼
AppController::stop()
  │
  ├── DphPublisher::stop()                          ← 停止定时器
  ├── SystemBus::shutdown()                         ← Running → ShutDown (不可逆)
  │     ├── stopProcessing()                        ← ThreadSafeQueue::shutdown()
  │     │     └── BusWorker::run() 自然退出 (dequeue 返回 false)
  │     └── clearAllSubscriptions()
  └── CommunicationManager::stopAll()              ← 关闭所有端点

StdinReader::terminate() + wait()
```

---

## 配置驱动关系

```
config/
├── endpoints.json      ──► EndpointConfigLoader ──► CommunicationManager
│                             添加 UdpEndpoint 实例
│
└── processor.json      ──► AppController 读取后装配：
      │
      ├── endpointSources[] ──► RadarDataProcessor::registerEndpointSource()
      │     "radar_cpu" → DEVICE_RADAR_NH_KU_1
      │
      ├── defaultRoute{} ──► BusToCommRouter::setDefaultRoute()
      │     {endpointId: "radar_cpu", targetName: "workmode"}
      │
      ├── routes[] ──► BusToCommRouter::registerRoute()
      │     PAYLOAD_TYPE_WORK_STANDBY     → radar_cpu/workmode
      │     PAYLOAD_TYPE_TARGET_TYPE      → radar_cpu/workmode
      │     PAYLOAD_TYPE_GENERAL_PACKAGE  → radar_cpu/workmode
      │
      ├── dphPublisher{} ──► DphPublisher::setRoute() + setInterval()
      │     enabled=true, 50ms, radar_cpu/dph
      │
      └── reverseTargetDevice ──► AppController::m_reverseTargetDevice
            用于 sendWorkModeCommand() 的 dispatchTarget
```

---

## 线程模型

| 线程 | 归属 | 职责 |
|------|------|------|
| **主线程** (Qt event loop) | `QCoreApplication::exec()` | Qt 信号/槽分发、所有 QObject 槽函数执行 |
| **BusWorker** | `SystemBus` | 从 `ThreadSafeQueue` 取包 → `dispatchToSubscribers()` |
| **StdinReader** | `main.cpp` | 阻塞读 `stdin`，通过 signal 投递到主线程 |

> `RadarDataProcessor`、`BusToCommRouter`、`DphPublisher` 全部运行在主线程。
> `BusToCommRouter` 的回调通过 `Qt::QueuedConnection` 从 BusWorker 线程安全投递到主线程。
