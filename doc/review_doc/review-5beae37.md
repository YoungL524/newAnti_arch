# Review 5beae37

提交：`5beae37 review-3941e32修改完成`

## 总体结论

这版把上一轮两个 P1 的主方向修对了：

- `findByEndpointId()` 改名为 `findRadarByEndpointId()`，语义更清楚。
- 查询限定 `kind = Radar`，不会把非雷达设备当成航迹来源。
- 多个 Radar 绑定同一 endpoint 时返回 `std::nullopt`，避免航迹错误归属。
- 文档也同步为“宁可不显示，不挂错设备”。

但现在还有一个写入校验上的边缘问题：`create/update` 复用了 `findRadarByEndpointId()` 做冲突检测，而这个查询在“已有重复绑定”时会返回空，可能导致继续写入更多重复绑定。

## P1：写入冲突检查不能复用 `findRadarByEndpointId()`

位置：`core/business/DeviceRegistry.cpp:135-157`、`core/business/DeviceRegistry.cpp:183-191`

当前逻辑：

```cpp
auto existing = findRadarByEndpointId(dev.endpointId);
if (existing.has_value() && existing->id != dev.id) {
    return false;
}
```

但 `findRadarByEndpointId()` 的新语义是：

```cpp
if (rows.size() > 1) {
    qWarning() << "... 拒绝返回 ...";
    return std::nullopt;
}
```

这对“航迹归属查询”是正确的，但对“写入冲突检查”不够。  
如果历史数据库里已经存在两个 Radar 设备绑定同一个 endpoint，`findRadarByEndpointId()` 会返回空，于是 `create()` / `update()` 会认为没有 existing，可能继续创建第三个重复绑定。

建议拆成两个语义：

```cpp
std::optional<DeviceInfo> findRadarByEndpointId(const QString& endpointId) const;
bool isRadarEndpointOccupiedByOther(const QString& endpointId, const QString& selfId) const;
```

或者增加内部查询：

```cpp
QVector<DeviceInfo> findRadarsByEndpointId(const QString& endpointId) const;
```

写入校验使用“列表查询”，只要存在 `id != selfId` 的 Radar，就拒绝：

```cpp
auto devices = findRadarsByEndpointId(dev.endpointId);
for (const auto& d : devices) {
    if (d.id != dev.id) {
        qWarning() << "[DeviceRegistry] endpointId" << dev.endpointId
                   << "已被设备" << d.id << "绑定";
        return false;
    }
}
```

`findRadarByEndpointId()` 继续保留当前语义：只在唯一匹配时返回设备；0 个或多个都返回空。  
这样读路径和写路径的语义就不会互相污染。

## P2：文档建议区分“归属查询”和“唯一性校验”

位置：`doc/business/航迹显示数据管理.md:160-164`

当前文档已经说明 `findRadarByEndpointId()` 是按端点查询接口，但没有区分：

- `RadarDataProcessor` 用它做航迹归属查询；
- `DeviceRegistry::create/update` 用另一套逻辑做唯一性写入校验。

建议补一句：

```text
findRadarByEndpointId() 只用于航迹归属查询：唯一匹配才返回设备。
create/update 的 endpoint 冲突检查必须统计所有同 endpoint Radar 设备，不能复用 findRadarByEndpointId() 的返回值判断是否占用。
```

## P2：`createAssigningId()` 的失败原因仍然偏模糊

上一轮提到的点仍在：`createAssigningId()` 仍按 id collision 重试，endpoint 冲突时最终日志可能是“重试耗尽”。  
这不是 Milestone 3 阻断，但后续设备管理 UI 要给用户明确错误时，最好把失败原因结构化。

## 下一步

修完 P1 后，`DeviceRegistry` 这块就足够支撑 Milestone 3 最小实现。  
下一步可以开始：

- `TrackTypes.h`
- `TrackDisplayManager`
- `RadarDataProcessor -> TrackPoint`
- `WebApiController -> snapshot 推送`

