# Review 2cbe0e6

提交：`2cbe0e6 review-da02163修改完成`

## 总体结论

这版把上一轮的两个 P2 都修掉了：

- 新增 `pruneExpired(nowMs)`，timer timeout 时先清理过期点，再生成 snapshot。
- 过期 track 的 cache 和 interaction 会一起删除，避免静默航迹长期积累内存。
- 新增 `snapshot(qint64 nowMs)` 重载，timer 内部复用同一个时间戳，避免重复取系统时间。
- `start()` / `stop()` / `clear()` 已整理到同一个 public API 区域。

当前 `TrackDisplayManager` 的基础行为已经足够支撑下一步链路接入。没有发现阻断问题。

## P2：缓存裁剪隐含“同一 track 点按时间递增到达”的假设

位置：`core/business/TrackDisplayManager.cpp:42-47`、`core/business/TrackDisplayManager.cpp:56-67`

当前裁剪逻辑都是从队首删除：

```cpp
while (!cache.points.isEmpty() && cache.points.first().point.timestampMs < cutoff)
    cache.points.removeFirst();
```

这在实时雷达流里通常成立，因为同一 `(deviceId, trackId)` 的点基本按时间递增到达。  
但如果未来接入历史回放、补包、乱序数据，旧点可能被 append 到队尾，这种情况下队首检查无法清掉队尾的过期点。

这个不是当前实时链路阻断项。建议二选一：

1. 在文档或代码注释里明确：`TrackDisplayManager` 假设同一 TrackKey 的输入点按时间递增。
2. 或者把 prune 改成 remove-if 风格，扫描整个 `points` 删除过期点。

当前阶段我建议先选第 1 种，等回放功能进入 Milestone 时再处理乱序。

## 下一步建议

可以进入主链路接入：

1. `AppController` 持有并启动/停止 `TrackDisplayManager`。
2. `RadarDataProcessor` 通过 `DeviceRegistry::findRadarByEndpointId()` 将 endpoint 映射成业务 `deviceId`。
3. `RadarDataProcessor` 输出 `TrackPoint` 到 `TrackDisplayManager`。
4. `WebApiController` 订阅 `snapshotReady` 并推送全量 `tracks` snapshot。
5. 前端 `useCoreApi.tsx` 从单点 append 改为 snapshot replace。

注意：下一步接链路时，重点不要让 `WebApiController` 重新解析 `GeneralPackage/TargetType`。它只应该序列化 `TrackDisplaySnapshot`。

