# 轻量航迹包与分层边界（历史迁移稿）

> 总线消息设计已由 `doc/architecture/08-现有航迹链路审计与统一入口.md` 修订：系统只使用 `DataPacket + command + std::variant payload`。本文只保留航迹字段和分层讨论，不再作为 SystemBus 包结构依据。

> 本文已按 `doc/architecture/06-航迹模型契约与字段映射.md` 修订。
> 旧的“NormalizedTrackBatch 上一次总线、IdentifiedTrackBatch 再上一次总线”方案已废止。

## 1. 固定结论

航迹核心主干只流转一个轻量 `TrackPacket`：

```text
Communication
  -> InputProtocolAdapter
       `-> 内部临时 GeneralPackage
  -> TrackPacket(Pending)
  -> RecognitionModule
  -> TrackPacket(Completed)
  -> SystemBus（只发布一次）
       |-> TrackDisplayManager
       |-> TrackStorage
       `-> 后续消费者
```

边界规则：

- `GeneralPackage` 只属于厂家协议适配层；
- 识别前后不更换航迹结构；
- 未完成识别的 TrackPacket 不进入全局总线；
- 总线消费者共享 `shared_ptr<const TrackPacket>`；
- 显示、存储和后续消费者不能修改权威航迹；
- `TrackDisplayUpdate` 是 Web 表现层投影，不是核心航迹包；
- 当前一包一条航迹，暂不引入 Batch；
- 控制、错误、日志、配置暂不强制迁入 TrackPacket。

## 2. 为什么不再使用万能 DataPacket 承载航迹

现有旧 `DataPacket` 包含固定 `rawData[4096]`、错误文本、源文件、行号、UI dispatchTarget 和 payloadType，实际大小约 4448 字节。

它适合早期快速统一接口，但不适合高频航迹主干：

- 一条航迹也要携带固定大缓冲；
- 按值入队可能复制整个包；
- `GeneralPackage` 穿透协议边界；
- payloadType + rawData 依赖人工解包；
- 扩展 union 会持续放大所有消息；
- `BusEnvelope::shared_ptr<const void>` 仍然缺少类型安全。

新航迹链路不再使用旧 DataPacket。现有工作模式、DPH、错误等消息保持原状，按实际需要分别迁移，不进行一次性总线重写。

## 3. 唯一内部航迹模型

```cpp
struct TrackPacket {
    uint16_t schemaVersion;
    uint64_t traceId;

    QString endpointId;
    QString deviceId;
    QString taskId;

    qint64 receivedAtMs;
    qint64 ingressSteadyNs;
    qint64 normalizedSteadyNs;
    qint64 recognizedSteadyNs;
    uint32_t sceneNumber;

    uint32_t trackId;
    double rangeM;
    double azimuthRad;
    double elevationRad;
    double radialSpeedMps;
    double rcs;
    double altitudeM;
    double xM, yM, zM;
    double vxMps, vyMps, vzMps;

    RecognitionStatus recognitionStatus;
    RecognitionType recognitionType;
};
```

完整字段来源、单位和暂缓字段见架构文档 06。

## 4. 生命周期和复制规则

```text
UDP bytes
  -> 栈上 GeneralPackage
  -> 创建一次 TrackPacket
  -> RecognitionModule 处理同一对象
  -> move 到 shared_ptr<const TrackPacket>
  -> SystemBus 分发同一只读对象
```

禁止：
- 适配、识别、总线各深拷贝一次；
- 为每个订阅者复制 TrackPacket；
- 发布后修改 TrackPacket；
- 把原始 4KB 缓冲挂到每条航迹；
- 用 `shared_ptr<const void>` 发布新航迹。

## 5. SystemBus 的定位

SystemBus 负责：
- 识别完成航迹的发布和订阅；
- 有界排队；
- 模块解耦；
- 发布失败、丢弃和队列高水位统计。

SystemBus 不负责：

- 厂家协议解析；
- 目标识别；
- 同步数据库写入；
- 显示样式生成；
- 因为存在总线就自动获得并行能力。

当前 SystemBus 只有一个 worker，订阅回调串行执行。回调只能快速校验和投递，耗时识别不在总线之后执行；存储等耗时消费者应使用自己的有界队列。

航迹通道的目标接口：

```cpp
using TrackPacketPtr = std::shared_ptr<const TrackPacket>;
using TrackCallback = std::function<void(TrackPacketPtr)>;

bool publishTrack(TrackPacketPtr packet);
SubscriptionId subscribeTracks(TrackCallback callback);
```

是否复用 `SystemBus` 内部队列或增加类型化队列属于实现选择，但对模块暴露的接口必须保持类型安全。

## 6. 显示链路

```text
TrackPacketPtr
  -> TrackDisplayManager
  -> 显示缓存、裁剪、样式
  -> TrackDisplayUpdate（snapshot / delta）
  -> TrackDisplayWebBridge
  -> React
```

`TrackDisplayUpdate` 只用于 Web 显示同步，不能回流成为存储、识别或其他核心模块的输入。

## 7. 存储链路

```text
TrackPacketPtr
  -> TrackStorage 自有有界队列
  -> 存储线程私有 DataStore
  -> 100 条或 200ms 批量事务
  -> SQLite
```

该链路已经落地。默认队列容量 4096 条；数据库不可用或积压不会阻塞航迹分发和显示。
队列满时只丢弃归档副本，并记录丢弃数与高水位，不会无限增长。只有归属活动任务、
即 `taskId` 非空的标准航迹会写入 `tracks` 表。

## 8. 性能与时延节点

主链路记录：

```text
T0  Communication 收到完整帧
T1  InputProtocolAdapter 生成 TrackPacket
T2  RecognitionModule 开始
T3  RecognitionModule 完成
T4  SystemBus 收到发布
T5  TrackDisplayManager 收到
T6  WebSocket broadcast 返回
```

重点指标：

- `T1-T0`：协议解码和标准字段映射；
- `T2-T1`：识别前等待；
- `T3-T2`：识别处理；
- `T5-T4`：一次总线分发；
- `T6-T0`：后端到 Web 发送完成的端到端耗时。

使用单调时钟统计 P50/P95/P99、最大值、样本数、丢弃数和队列高水位。墙上时间只用于日志、Web 和数据库。

当前已经实现的 `decode`、`processorTotal`、`ingressToDisplay`、`snapshotBuild`、`snapshotBusDispatch`、`webQueue`、`webPrepareAndBroadcast` 等指标继续作为迁移前基线。

## 9. 当前代码差距

当前已经具备：

- UDP 接入和 `GeneralPackage` 解码；
- endpoint -> device -> task 映射；
- TrackPoint 显示路径；
- SystemBus 和显示 Snapshot；
- 航迹批量写 SQLite；
- 后端时延统计。

仍需迁移：

1. 新增轻量 TrackPacket；
2. 提取 `GeneralPackage -> TrackPacket` 纯转换函数；
3. 实现透传识别并填写同一包；
4. 增加类型安全航迹发布接口；
5. 显示从 `trackPointReady` 切换到 TrackPacketPtr；
6. 存储从 Processor 内部缓冲切换到独立消费者；
7. 停止发布包含 GeneralPackage 的旧 DataPacket；
8. 新航迹路径不再使用 BusEnvelope 的 void payload。

## 10. 实施顺序

1. 锁定当前字段和时延基线；
2. 新增 TrackPacket 及字段映射测试；
3. 新增透传识别测试；
4. 旁路产生并校验 TrackPacket，不影响旧链路；
5. 接入一次类型化总线发布；
6. 迁移显示消费者；
7. 迁移存储消费者；
8. 全部测试和性能基线通过后删除旧航迹路径。

## 11. 架构底线

以下情况视为继续偏离：

- 新航迹包重新包含 `GeneralPackage` 或固定 rawData；
- 识别前后创建两套重复航迹对象；
- 航迹在识别前后各发布一次总线；
- 显示或存储解析厂家协议；
- 发布后任意消费者修改 TrackPacket；
- 新航迹消息继续依赖 void payload；
- 存储同步阻塞总线 worker；
- 为尚未存在的算法、厂家和插件增加抽象。
