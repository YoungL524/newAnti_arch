# Review: e0771cf

## 结论
这一版把“航迹交互从前端本地状态迁移到后端快照驱动”这条线继续往前推了，方向是对的。  
但现在有一个会直接破坏路由分发的高优先级问题，需要先修掉；另外前端还有几处类型回退，说明交互链路虽然通了，但契约还没收紧。

## 问题 1
`core/web/WebApiController.cpp` 里重复注册了 `/api/devices/` 的 `PUT` 前缀路由。

前面已经有一段设备更新 handler，后面又新增了一段 `Track Interaction API`，并且两段都在调用 `m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), ...)`。  
`core/web/WebService.cpp` 里 `handlePutPrefix` 的实现是 `m_putPrefixHandlers[prefix] = std::move(handler)`，也就是说后注册的会覆盖前一个。

这会导致：
`/api/devices/{id}` 设备更新路由被覆盖，或者交互路由和设备编辑路由互相抢占。

建议：
把后面那段重复的 `Track Interaction API` 整段删掉，只保留前面那个统一入口，继续走已经抽出来的 `handleTrackInteraction` 分支。

## 问题 2
航迹交互 handler 出现了两套实现，分发和返回结构不一致。

当前文件里除了统一的 `handleTrackInteraction`，还有一段新的 `suffix.split('/')` + `toUInt()` 的处理逻辑。  
这段逻辑不仅重复，而且少了 `deviceId / trackId` 回传，路径校验也比前面的 helper 更松。

建议：
只保留一个交互解析函数，设备更新和交互共用它。交互 API 的返回也保持统一，避免前端后面再补兼容分支。

## 问题 3
前端交互链路已经接上，但类型契约还在退回到 `any`。

具体位置：
`ui/src/hooks/useCoreApi.tsx`
`ui/src/components/map/TrackLayer.tsx`
`ui/src/components/panels/DataTable.tsx`

这里主要是这几处：
`(t as any).interaction`
`style?: any`
`interaction?: any`
`(row as any).deviceId / trackId / interaction`

建议：
把这些地方收敛成 `RadarTrack` 或一个局部精确类型，不要让交互刚打通就重新掉回宽松类型。否则后面一旦改协议，前端很难靠编译发现问题。

## 问题 4
`TrackLayer` 和 `DataTable` 的点击行为目前只负责“翻转 selected”，`trackingEnabled` 仍然沿用旧值。

这本身不一定错，但意味着“选中”和“追踪使能”两个交互语义还没有真正被 UI 显式拆开。  
如果这版只是先把单点点击打通，可以接受；但后面最好补一个明确入口，不要让用户误以为已经能完整控制航迹状态。

## 正面点
这版已经比前面稳很多了：
前端不再自己改本地样式，而是通过后端 `interaction` + 下一帧 `snapshot` 去刷新显示，这个方向符合我们前面定死的架构边界。

