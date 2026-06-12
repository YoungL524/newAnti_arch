# Review 3941e32

提交：`3941e32 review-6d59cf5修改完成`

## 总体结论

这轮开始把 Milestone 3 需要的 `endpointId -> deviceId` 映射能力落到 `DeviceRegistry`，方向正确：

- 新增 `DeviceRegistry::findByEndpointId()`。
- `create()` / `update()` 对雷达设备的非空 `endpointId` 做了冲突检查。
- `doc/business/航迹显示数据管理.md` 补了 endpoint 映射约束、snapshot 输出裁剪语义、前端迁移注意事项。

但当前实现里还有一个关键语义偏差：**文档说 endpoint 映射必须唯一，冲突时应避免错误归属；代码却在重复绑定时返回第一个设备。**  
这个建议先修，再让 `RadarDataProcessor` 依赖它。

## P1：`findByEndpointId()` 在重复绑定时返回第一个，会造成航迹错误归属

位置：

- `core/business/DeviceRegistry.cpp:135-143`
- `core/business/DeviceRegistry.h:59-60`
- `doc/business/航迹显示数据管理.md:152-164`

当前实现：

```cpp
if (rows.size() > 1)
    qWarning() << "... 绑定了 ... 个设备，使用第一个";
return DeviceInfo::fromVariantMap(rows.first());
```

这和 Milestone 3 的安全语义冲突。  
航迹显示协议里的 `deviceId` 是 Web 固定协议的一部分，一旦把 endpoint 的数据归到错误业务设备，后续 `(deviceId, trackId)`、interaction、数据转发都会跟着错。

建议改成：

```cpp
if (rows.size() > 1) {
    qWarning() << "[DeviceRegistry] endpoint_id" << endpointId
               << "绑定了" << rows.size() << "个雷达设备，拒绝返回，避免航迹错误归属";
    return std::nullopt;
}
```

文档也同步改成：

```text
如果 endpointId 绑定多个 Radar 业务设备：
  记录 qWarning，返回空映射；RadarDataProcessor 丢弃该 TrackPoint，不向 TrackDisplayManager 输出。
```

不要“使用第一个”。这个系统里宁可不显示，也不要把航迹挂错设备。

## P1：`findByEndpointId()` 查询没有限制 Radar kind，可能返回非雷达设备

位置：`core/business/DeviceRegistry.cpp:137-139`

当前查询：

```cpp
SELECT * FROM devices WHERE endpoint_id = ?
```

但 Milestone 3 的链路是雷达航迹链路，文档也写的是：

```text
同一时刻只能绑定一个 Radar 业务设备
```

如果某个非雷达设备误配置了相同 endpointId，当前 `findByEndpointId()` 可能返回水平仪/GPS/光电设备。后续 `RadarDataProcessor` 用它填 `TrackPoint.deviceId` 就会把雷达航迹归到非雷达设备上。

建议把接口语义定成雷达专用，至少查询时过滤：

```cpp
SELECT * FROM devices WHERE endpoint_id = ? AND kind = ?
```

绑定 `static_cast<int>(DeviceKind::Radar)`。

更清晰的命名可以是：

```cpp
std::optional<DeviceInfo> findRadarByEndpointId(const QString& endpointId) const;
```

如果担心以后其他设备也要按 endpoint 查，可以保留通用接口，但 Milestone 3 的 RadarDataProcessor 应调用雷达专用接口。

## P2：`createAssigningId()` 遇到 endpoint 冲突时日志会误导为“id 重试耗尽”

位置：`core/business/DeviceRegistry.cpp:164-177`

`createAssigningId()` 现在只知道 `create(dev)` 返回 false，然后按 id collision 的思路重试。  
但 `create()` 现在可能因为 endpointId 冲突失败，这种情况下重试生成新 id 没意义，最后日志也会变成：

```text
创建设备失败，重试耗尽: id=...
```

这会误导排查。

建议后续把 `create()` 失败原因拆清楚，至少在 `createAssigningId()` 里补一句：

```text
如果 endpointId 非空且已被占用，不应进入 id collision 重试路径。
```

短期也可以接受，因为 `create()` 已经有 endpoint 冲突 warning，但这块后面做设备管理 UI 时会影响错误提示。

## P2：建议补最小验证清单

建议在 `doc/test` 或设备管理验证清单里补：

1. 创建两个 Radar 设备绑定同一个非空 endpointId：第二个失败。
2. 更新 Radar 设备到已占用 endpointId：失败。
3. `findByEndpointId()` 找不到 endpoint：返回空。
4. `findByEndpointId()` 遇到重复历史脏数据：返回空，不返回第一个。
5. 非 Radar 设备同 endpointId 不应被 RadarDataProcessor 用作航迹 deviceId。

## 下一步

修完上面两个 P1 后，就可以继续 Milestone 3 最小实现：

- 定义 `TrackTypes.h`。
- `RadarDataProcessor` 输出 `TrackPoint`，填入业务 `deviceId`。
- 新建 `TrackDisplayManager`。
- `WebApiController` 改为推送 snapshot，不再解析 `GeneralPackage/TargetType`。

