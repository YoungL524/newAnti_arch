# Review d0a757d

提交：`d0a757d review-58d6bf2修改完成`

## 总体结论

这版把上一轮大部分问题修掉了：

- interaction 不再固化在缓存点里，而是在 `snapshot()` 阶段动态合并当前状态。
- 增加了 60s 时间窗口裁剪。
- 增加了 5000 点 snapshot 输出预算，并按 timestamp 保留最新点。
- 增加了基础样式策略：前 3 点、普通点、选中、追踪使能。
- `TrackDisplayManager` 内部增加 1s timer，定时 emit `snapshotReady(snapshot())`。

方向是对的，但有一个关键回退：`pointIndex` 又变成了缓存内相对序号，和前面定下的“绝对递增序号”契约冲突。

## P1：`pointIndex` 被改成相对序号，FIFO/时间裁剪后会重置

位置：`core/business/TrackDisplayManager.cpp:80-96`

当前实现：

```cpp
uint32_t idx = 0;
for (const auto& pt : cache.points) {
    ...
    dp.pointIndex = idx++;  // 相对序号，从 0 开始
    applyStyle(dp, dp.pointIndex, interaction);
}
```

这和业务文档冲突：

```text
pointIndex 是该 (deviceId, trackId) 自接收以来绝对递增序号
不是缓存数组下标，FIFO 裁剪后不会重置
```

现在一旦某条航迹因为 500 点/60s 窗口发生裁剪，下一次 snapshot 里保留下来的第一个点又会变成 `pointIndex = 0`。这会导致：

- 前 3 个点强调样式被重复触发；
- 前端无法区分“真正的起始 3 点”和“窗口裁剪后的当前前 3 点”；
- 后续如果 pointIndex 用于轨迹显示逻辑，会出现状态回退。

建议恢复绝对递增设计：

```cpp
struct CachedPoint {
    TrackPoint point;
    uint32_t pointIndex = 0;
};

struct TrackCache {
    QVector<CachedPoint> points;
    uint32_t nextPointIndex = 0;
};
```

`addPoint()` 时赋值：

```cpp
cache.points.append({pt, cache.nextPointIndex++});
```

`snapshot()` 只读取缓存中的绝对 `pointIndex`，不要重新用数组下标计算。

## P2：60s 时间窗口只在新点到达时裁剪，静默航迹会继续出现在 snapshot

位置：`core/business/TrackDisplayManager.cpp:27-35`、`core/business/TrackDisplayManager.cpp:66-112`

当前时间窗口裁剪在 `addPoint()` 里：

```cpp
const qint64 cutoff = pt.timestampMs - kTimeWindowMs;
while (!cache.isEmpty() && cache.first().timestampMs < cutoff)
    cache.removeFirst();
```

这意味着如果某条航迹 60s 内没有新点，它的旧点不会被清理，仍会在每 1s 的全量 snapshot 里输出。  
但文档定义的是“当前显示窗口内所有点”，更合理的窗口基准应是当前时间或 snapshot 时间，而不是“该 track 最新点时间”。

建议：

- `addPoint()` 可以继续做一次局部裁剪。
- `snapshot()` 也应按 `snap.timestamp - 60s` 过滤候选点。
- 如果某个 cache 被过滤到空，后续可以考虑清理空 cache 和无用 interaction。

这样 1s snapshot 才真正表示“最近 60s 的当前可见点集”。

## P2：`TrackDisplayManager` 构造即启动 timer，后续生命周期可能不够可控

位置：`core/business/TrackDisplayManager.cpp:11-20`

现在构造后自动：

```cpp
m_timer->start(kSnapshotIntervalMs);
```

这符合“每 1s 输出 snapshot”的文档，但后续接入 `AppController` 时要注意生命周期：

- 如果 Web 服务未启动，manager 仍会每秒生成 snapshot。
- 如果采集停止，是否继续推空 snapshot 需要明确。
- 如果以后任务切换，需要 reset/stop/start 能力。

当前不阻断，但建议后续接入时增加：

```cpp
void start();
void stop();
void clear();
```

至少让 `AppController` 能在采集/任务生命周期里控制它。

## 关于 DDS

这轮顺手回答一下：目前项目**还不算 DDS 系统**。

现在更准确的定位是：

```text
反无人机上位机 / 综合运营管理系统
  + 本地事件总线
  + 领域服务
  + 固定 Web 数据协议
```

它和 DDS 有一点相似：都有“发布/订阅”的味道，例如 `SystemBus::publish()`、按 source/target 订阅、数据包类型分发。  
但严格意义上的 DDS（Data Distribution Service）通常包括：

- 标准化 topic；
- DataWriter / DataReader；
- QoS 策略，如可靠性、历史深度、deadline、durability；
- 类型发现和参与者发现；
- 跨进程/跨网络的数据分发；
- RTPS 等通信协议；
- 多节点实时数据空间。

我们当前的 `SystemBus` 是进程内事件总线，不是 DDS middleware。  
所以可以说：

```text
架构思想上接近“数据分发/发布订阅”，但不是 DDS 系统。
```

如果以后要接入真正 DDS，比较自然的位置是在 core 的设备/数据接入层或 SystemBus 外围加适配器，而不是把当前上位机内部结构直接叫 DDS。

