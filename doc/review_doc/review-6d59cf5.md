# Review 6d59cf5

提交：`6d59cf5 review-948df3c修改完成`

## 总体结论

这版 `doc/business/航迹显示数据管理.md` 已经把上一轮两个 P1 修正到位：

- `endpointId -> deviceId` 映射责任固定为 `RadarDataProcessor`。
- `TrackDisplayManager` 不再接触 endpoint，也不查询 `DeviceRegistry`。
- 内部状态使用 `(deviceId, trackId)` 复合 key。
- interaction API 改为 `/api/devices/{deviceId}/tracks/{trackId}/interaction`。
- `pointIndex` 定义为绝对递增序号。
- 全量 snapshot 增加了 5000 点输出上限。

整体上，这份文档已经可以支撑 Milestone 3 的最小实现。  
但在开写代码前，还需要补一个和 Milestone 1 强相关的设备绑定约束。

## P1：需要定义 endpointId 到业务 deviceId 的唯一性约束和查询接口

位置：`doc/business/航迹显示数据管理.md:8-21`、`doc/business/航迹显示数据管理.md:149-150`

文档现在定了：

```text
endpointId
  → RadarDataProcessor 查询 DeviceRegistry / endpoint binding
  → TrackPoint.deviceId = 业务设备 id
```

这个方向是对的，但当前代码里的 `DeviceRegistry` 只有：

```cpp
std::optional<DeviceInfo> get(const QString& id) const;
QVector<DeviceInfo> list() const;
```

`devices` 表里 `endpoint_id` 也没有唯一约束。也就是说，如果后续用户创建了两个雷达设备并绑定同一个 `endpointId = radar_cpu`，`RadarDataProcessor` 就无法稳定判断这个 UDP 数据来自哪个业务设备。

建议 Claude Code 在文档里补充并在后续实现时落实：

```text
对于会产生航迹数据的采集 endpoint，同一时刻只能绑定一个 Radar 业务设备。
endpointId -> deviceId 映射必须唯一。
```

同时建议 `DeviceRegistry` 增加只读查询接口：

```cpp
std::optional<DeviceInfo> findByEndpointId(const QString& endpointId) const;
```

并明确冲突策略：

```text
如果 endpointId 未绑定业务设备：
  RadarDataProcessor 丢弃该 TrackPoint 或标记为 unknown，不向 TrackDisplayManager 输出。

如果 endpointId 绑定多个业务设备：
  记录 error/warning，不输出 TrackPoint，避免错误归属。
```

是否在 SQLite 上加唯一索引要谨慎，因为空 endpointId 会有很多未绑定设备。可以先不做全表唯一索引，而是在 `DeviceRegistry::create/update` 里对非空 `endpointId` 做应用层校验：

```text
endpointId != "" 时，同 kind=Radar 的设备中不能重复。
```

这点要和设备管理 Milestone 1 的台账契约保持一致，否则 Milestone 3 的 `deviceId` 固定协议会在数据源入口失真。

## P2：全量 snapshot 的 5000 点上限要说明是“输出裁剪”还是“缓存删除”

位置：`doc/business/航迹显示数据管理.md:41-47`

当前写法：

```text
单次 snapshot 最多输出 5000 个点；超过时丢弃更旧点，优先保留最新显示窗口
```

这里的“丢弃”建议说清楚：

- 如果是缓存删除，会影响每条 track 的历史显示缓存。
- 如果只是本次 snapshot 输出裁剪，不影响内部缓存状态。

我建议定义为：

```text
5000 点是单次 snapshot 输出预算；超过时只裁剪本次输出集合，不额外删除 TrackDisplayManager 内部缓存。
内部缓存仍由每条 TrackKey 的 500 点 / 60s 策略维护。
```

这样职责更清楚，也避免全局输出预算反过来破坏单条航迹缓存。

## P2：前端迁移需要明确“替换渲染”对 trackRate/DataTable 的影响

位置：`doc/business/航迹显示数据管理.md:152`

文档已经写了：

```text
前端：从单点增量 track 改为全量 tracks snapshot 替换渲染
```

建议再补一句迁移影响：

- `useCoreApi.tsx` 当前对 `msg.type === 'tracks'` 是 `tracksRef.current = [...tracksRef.current, row].slice(-500)` 增量追加。
- 改成 snapshot 后，应以 `msg.tracks` 替换当前显示数据，而不是 append。
- `trackRate` 也不能再简单 `++`，应改为 snapshot 点数、snapshot 频率，或单独由后端提供统计字段。
- `DataTable` 如果展示“航迹列表”而不是“点列表”，可能需要按 `(deviceId, trackId)` 聚合最后一个点。

这不是后端阻断问题，但如果不提前写清楚，前端很容易把 snapshot 当成增量消息继续追加，导致显示重复。

## 是否可以开写代码

可以。  
建议先补上 P1 的唯一映射约束，然后开始最小实现：

1. `DeviceRegistry::findByEndpointId()` 和非空 endpoint 绑定冲突检查。
2. `TrackTypes.h` 定义 `TrackKey`、`TrackPoint`、`TrackDisplayPoint`、`TrackDisplaySnapshot`。
3. `RadarDataProcessor` 从 `GeneralPackage/TargetType` 生成 `TrackPoint`，并填入业务 `deviceId`。
4. `TrackDisplayManager` 维护缓存、interaction、snapshot。
5. `WebApiController` 改为推送 manager 的 snapshot，不再解析 `GeneralPackage`。
