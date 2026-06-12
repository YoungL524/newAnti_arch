# Review 06cbd57

提交：`06cbd57 review-5beae37修改完成`

## 总体结论

这版把上一轮 P1 修到位了：

- `findRadarByEndpointId()` 保持“航迹归属查询”语义：唯一匹配才返回设备，0 个或多个都返回空。
- 新增私有 `findRadarDevicesByEndpoint()`，专门用于写端冲突检查。
- `create()` / `update()` 不再复用归属查询，而是遍历所有同 endpoint 的 Radar 设备，只要存在 `id != selfId` 就拒绝写入。

这个边界现在是清楚的：  

```text
读路径：宁可不返回，不挂错设备。
写路径：只要 endpoint 已被其他 Radar 占用，就拒绝继续制造重复绑定。
```

当前没有看到阻断 Milestone 3 继续推进的问题。`DeviceRegistry` 这块已经可以支撑 `RadarDataProcessor -> TrackPoint.deviceId` 的最小实现。

## P2：`createAssigningId()` 的失败原因后续仍建议结构化

位置：`core/business/DeviceRegistry.cpp:181-195`

这个问题不阻断当前 Milestone 3，但后续设备管理 UI 会遇到：`createAssigningId()` 仍然把 `create()` 失败统一当成 id collision 重试路径处理。

如果失败原因是 endpoint 冲突，重试生成新 id 没有意义，最终日志仍可能表现为：

```text
创建设备失败，重试耗尽
```

建议后续有时间时把 `create()` 的失败原因结构化，例如：

```cpp
enum class DeviceRegistryError {
    None,
    IdConflict,
    EndpointConflict,
    StoreError,
};
```

或者短期增加一个内部 `isRadarEndpointOccupiedByOther(endpointId, selfId)`，在 `createAssigningId()` 开始前先判断 endpoint 冲突，直接返回明确错误。

## 下一步建议

可以开始 Milestone 3 的最小代码实现：

1. 新增 `TrackTypes.h`：`TrackKey`、`TrackPoint`、`TrackDisplayPoint`、`TrackDisplaySnapshot`。
2. `RadarDataProcessor` 通过 `DeviceRegistry::findRadarByEndpointId()` 把 `endpointId` 映射成业务 `deviceId`。
3. 未绑定或重复绑定时不输出 `TrackPoint`。
4. 新建 `TrackDisplayManager`，先实现缓存、绝对 `pointIndex`、snapshot 输出。
5. `WebApiController` 后续改为推送 snapshot，不再直接解析 `GeneralPackage/TargetType`。

