# Review 58d6bf2

提交：`58d6bf2 review-06cbd57修改完成`

## 总体结论

这轮开始实现 Milestone 3 的最小基础：

- 新增 `TrackTypes.h`。
- 新增 `TrackDisplayManager.h/.cpp`。
- `TrackKey` 使用 `(deviceId, trackId)` 复合 key。
- `pointIndex` 使用 `nextPointIndex++`，是绝对递增序号，不受 FIFO 裁剪影响。
- CMake 已加入新文件。

方向是对的，而且这一步没有把 `TrackDisplayManager` 做成 DeviceRegistry/endpoint 查询器，边界保持住了。  
但当前 `TrackDisplayManager` 还没完全满足业务文档里的核心行为，建议先修下面两个 P1，再继续接 `RadarDataProcessor` 和 `WebApiController`。

## P1：`setInteraction()` 不会影响已缓存点，下一次 snapshot 仍可能是旧状态

位置：`core/business/TrackDisplayManager.cpp:56-63`

当前逻辑：

```cpp
auto& interaction = m_interactions[key];
interaction.selected = selected;
interaction.trackingEnabled = trackingEnabled;
```

问题是已缓存的 `TrackDisplayPoint` 在 `addPoint()` 时已经拷贝了当时的 interaction：

```cpp
dp.interaction = interaction;
cache.points.append(dp);
```

因此如果用户选中一条已有航迹，但这条航迹之后暂时没有新点进来，`m_interactions` 虽然变了，`cache.points` 里的旧点仍然是旧 interaction。下一次 `snapshot()` 直接返回缓存点，前端看不到选中/追踪使能状态变化。

这和文档不一致：

```text
更新后 TrackDisplayManager 更新指定 (deviceId, trackId) 的 interaction 状态
下次 snapshot 输出时将携带更新后的 interaction
```

建议两种改法选一种：

1. `snapshot()` 生成输出时动态合并 interaction，不信任缓存点里的 interaction。
2. `setInteraction()` 同步更新该 `TrackKey` 下所有已缓存点的 `interaction`。

我更建议第 1 种：缓存里保存点的几何/运动数据，snapshot 阶段叠加当前 interaction。这样 interaction 是真正的“当前显示状态”，不会和历史点拷贝分叉。

## P1：没有实现 60s 时间窗口，5000 点输出也没有优先保留最新点

位置：`core/business/TrackDisplayManager.cpp:33-50`

业务文档要求：

```text
每条 (deviceId, trackId) 最多保留 500 个点
同时按时间窗口限制：最近 60s
单次 snapshot 最多输出 5000 个点；超过时只裁剪本次输出集合
```

当前实现只做了每条 500 点：

```cpp
if (cache.points.size() > kMaxPointsPerTrack)
    cache.points.remove(0, cache.points.size() - kMaxPointsPerTrack);
```

没有按 `timestamp` 裁剪最近 60s。

另外 `snapshot()` 现在按 `QHash` 遍历顺序追加，达到 5000 点直接返回：

```cpp
for (auto it = m_caches.begin(); it != m_caches.end(); ++it) {
    ...
    if (snap.tracks.size() >= kMaxSnapshotPoints) return snap;
}
```

`QHash` 遍历顺序不稳定，这不等价于“优先保留最新显示窗口”。在高目标数场景下，可能保留了一批较旧点，却丢掉了其他 track 的新点。

建议：

- `addPoint()` 后按时间窗口删除该 track 内超过 60s 的旧点。
- `snapshot()` 先收集所有当前缓存点，再按 `timestamp` 从新到旧排序。
- 如果超过 5000，只保留最新 5000 个点。
- 最后如前端需要稳定绘制顺序，可以再按 `timestamp` 升序输出这 5000 个点。

## P2：样式策略还没有体现“前 3 个点”和稳定点差异

位置：`core/business/TrackDisplayManager.cpp:30`

当前所有点都是默认绿色：

```cpp
dp.style = TrackDisplayPoint::Style();
```

文档和前面架构讨论里提到过：

```text
前 3 个点的尺寸/颜色是什么？
第四个点开始的尺寸/颜色是什么？
选中/追踪使能时样式怎么变？
```

建议第一版可以先做最小规则：

```text
pointIndex < 3：强调色，size 稍大
普通稳定点：标准色
selected：覆盖为选中样式
trackingEnabled：覆盖/叠加追踪样式
```

不需要复杂，但要把“样式由 TrackDisplayManager 管”这件事落实。

## P2：`snapshotReady` 信号目前没有触发机制

位置：`core/business/TrackDisplayManager.h:33-35`

现在信号定义了，但没有定时器，也没有外部触发封装。  
这不阻断当前最小类实现，但下一步接 `WebApiController` 时要明确：

- 是 `AppController` 放一个 1s timer 调 `snapshot()` 并推送；
- 还是 `TrackDisplayManager` 自己持有 `QTimer`，每 1s emit `snapshotReady(snapshot())`。

业务文档写的是 `TrackDisplayManager` 每 1s 输出 snapshot。若按文档，我建议 manager 内部持有 timer，并提供 `start()/stop()` 或构造后自动启动。

## 下一步建议

先修 `TrackDisplayManager` 内部逻辑，再接链路：

1. interaction 在下一次 snapshot 必须立即反映到所有缓存点。
2. 实现 60s 时间窗口。
3. 5000 点输出裁剪必须保留最新点。
4. 补最小样式策略。
5. 再接 `RadarDataProcessor -> TrackPoint -> TrackDisplayManager`。

