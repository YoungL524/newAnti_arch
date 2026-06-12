# Review da02163

提交：`da02163 review-d0a757d修改完成`

## 总体结论

这版把上一轮核心问题修掉了：

- `pointIndex` 恢复为绝对递增序号，缓存裁剪后不重置。
- `snapshot()` 使用 `cp.pointIndex`，不再用数组下标重新计算。
- `snapshot()` 按当前时间过滤 60s 窗口。
- timer 不再构造即启动，增加了 `start()` / `stop()` / `clear()`。
- 空 snapshot 不再 emit `snapshotReady`。

当前 `TrackDisplayManager` 的核心契约基本成立，可以继续推进链路接入。  
剩下的问题不是阻断项，但建议尽快处理，避免长期运行后积累状态。

## P2：`snapshot()` 只过滤过期点，不清理静默 track 的缓存和 interaction

位置：`core/business/TrackDisplayManager.cpp:78-116`

当前 `snapshot()` 会按当前时间过滤：

```cpp
const qint64 cutoff = snap.timestamp - kTimeWindowMs;
...
if (pt.timestampMs < cutoff) continue;
```

这样前端不会继续看到 60s 外的旧点，这是对的。  
但这些旧点仍然留在 `m_caches` 里，对应的 `m_interactions` 也会一直留着。如果某个 track 静默后永远不再来点，`addPoint()` 里的清理逻辑就不会再触发。

长期运行后可能出现：

- `m_caches` 保存大量过期点；
- `m_interactions` 保存大量已经不可见的 track 状态；
- snapshot 每秒仍然遍历这些过期 cache，只是最后不输出；
- 多任务/多批次运行时，内存状态变得越来越厚。

建议增加一个非 const 的清理路径：

```cpp
void TrackDisplayManager::pruneExpired(qint64 nowMs);
```

在 timer timeout 里先清理再 snapshot：

```cpp
const qint64 now = QDateTime::currentMSecsSinceEpoch();
pruneExpired(now);
auto snap = snapshot(now);
```

或者让 `snapshot()` 去掉 `const`，内部顺手清理。  
我更建议拆成 `pruneExpired()`，语义清楚，也方便单元测试。

清理规则：

```text
每个 TrackKey 删除 timestampMs < now - 60s 的点；
如果该 TrackKey 的 points 为空，则删除 m_caches[key] 和 m_interactions[key]。
```

## P2：`start/stop/clear` 建议整理到同一个 public 区域

位置：`core/business/TrackDisplayManager.h:11-25`

现在 header 里有两个 `public:` 段：

```cpp
public:
    explicit TrackDisplayManager(...);
    ...

signals:
    ...

public:
    void start();
    void stop();
    void clear();
```

这不是功能 bug，但基础类接口建议保持干净。  
可以把 `start()` / `stop()` / `clear()` 放到第一个 public 段，`signals` 放在 public API 后面。

## 下一步建议

这版不需要继续在 `TrackDisplayManager` 内部无限打磨。  
补完 P2 后，可以正式接链路：

1. `AppController` 持有 `TrackDisplayManager`。
2. `RadarDataProcessor` 引入 `DeviceRegistry` 只读依赖或映射函数，输出 `TrackPoint`。
3. 未绑定/重复绑定 endpoint 时丢弃 `TrackPoint` 并 warning。
4. `TrackDisplayManager::snapshotReady` 接到 `WebService`，推送全量 `tracks` snapshot。
5. 前端从单点增量 append 改成 snapshot 替换。

