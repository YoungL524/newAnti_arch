# Review: dc0df65

## 结论
这次改动分两块：追踪/航迹显示体验继续增强，另外新增了一组 `.drawio` 架构图。  
整体方向是好的，尤其是架构图已经开始按“业务 / 应用 / 数据 / 技术 / 核心流程”拆开，这比单张大图更适合后续定基座。

但这轮有几个需要尽快修正的问题：代码里需要补全局保护和快照成本控制，图里有几处把“未来设计”画成了“当前实现”，容易误导后续实现方。

## 问题 1
`TrackDisplayManager` 移除单航迹点数上限是业务要求，但仍需要系统级保护。

位置：
`core/business/TrackDisplayManager.cpp`

当前逻辑是：

- 每次 `addPoint()` 都 append 到 `cache.points`
- 只在整条航迹 5 秒无新点时删除
- snapshot 最多裁到 8000 点，但底层 `m_caches` 不裁剪

如果业务要求“单航迹完整保留”，那不应该再简单恢复单航迹点数上限。  
真正需要补的是整体资源保护和 snapshot 成本控制，否则某条航迹持续有新点时，`cache.points` 会持续增长。  
这会造成两个风险：

- 后端内存持续涨。
- 每 500ms snapshot 都要遍历历史全部点，再排序、裁剪到 8000，点多后 CPU 会被拖上去。

建议：
不要恢复“单航迹点数上限”，而是换成这些保护：

- 后端缓存保留完整单航迹，但 snapshot 构建时只取显示需要的窗口或按比例抽样。
- 增加全局缓存水位，例如总点数、总内存、单次 snapshot 最大处理点数。
- 当超过水位时优先降级显示输出，而不是截断业务航迹数据。
- 后续历史完整航迹应进入持久化/历史中心，TrackDisplayManager 只承担实时显示缓存。

这样既满足完整航迹保留，又避免实时显示链路被拖垮。

## 问题 2
前端 `pendingInteractionRef` 乐观覆盖缺少兜底清理。

位置：
`ui/src/hooks/useCoreApi.tsx`

当前做法是点击后先把 pending interaction 覆盖到本地显示，等后端 snapshot 回来且状态一致后再删除 pending。  
这个体验方向是对的，但如果后端接受了请求、随后该航迹消失，或者 WebSocket 长时间没有包含对应 track，pending 会一直留在 `pendingInteractionRef` 里。

建议：
pending interaction 要加生命周期：

- 成功收到一致 snapshot 后删除。
- 请求失败后删除。
- 如果超过一个短 TTL 仍未被后端确认，也要删除并提示或静默回滚。
- 停止采集时已经 clear，这一点保留。

## 问题 3
`05-核心流程.drawio` 的“上行航迹显示”把 `TrackPoint` 画成经过 `SystemBus` 分发，但当前代码不是这样。

位置：
`doc/architecture/05-核心流程.drawio`

图中流程是：

`协议适配 -> publish TrackPoint event -> SystemBus -> TrackDisplayManager`

但当前代码实际是：

`RadarDataProcessor::trackPointReady -> Qt signal -> TrackDisplayManager::addPoint`

`RadarDataProcessor` 确实会把 `GeneralPackage` 发布到 `SystemBus`，但 `TrackPoint` 这条显示链路目前是 signal 直连。  
这张图如果作为架构基座，会让后续实现者误以为 TrackDisplayManager 已经订阅总线。

建议二选一：

- 如果当前设计决定保留直连，就把图改成“通信/Processor 可直连 TrackDisplayManager，SystemBus 承担其他业务事件”。
- 如果架构决定 TrackPoint 也必须上总线，就需要补代码实现，而不是只改图。

我倾向当前阶段保留直连：航迹点是高频显示数据，直连能少一次队列和拷贝；架构图里明确这是性能通道即可。

## 问题 4
`05-核心流程.drawio` 的 DPH 参数流程写成了 partial params，但当前 API 仍要求完整字段。

位置：
`doc/architecture/05-核心流程.drawio`  
`core/web/WebApiController.cpp`

图里写的是：

`PUT /api/runtime partial params`  
`apply partial update`  
`merge full snapshot`

但当前后端 `PUT /api/runtime-settings` 仍然要求 `frequencyControl` 和 `waveControl` 都存在，缺任何一个都会返回 “不能为空”。  
这会让图和代码契约不一致。

建议：
短期把图改成“PUT full RuntimeSettings DTO”。  
等后续真的实现 partial update，再同步改 API 和图。

## 问题 5
`doc/系统架构.drawio` 仍保留旧的数据访问表达：`.ini / .csv / .json / .log`，并写了“数据访问统一落这些文件”。

位置：
`doc/系统架构.drawio`

现在项目已经有 SQLite `DataStore`、`DeviceRegistry`、配置 json、日志和回放文件等多种数据载体。  
这张图如果作为总览，会弱化“后端 DeviceRegistry/SQLite 是设备台账 source of truth”这个已经定下来的架构决策。

建议：
把旧图里的数据层改成更稳定的表达：

- SQLite/DataStore：业务台账、后续任务、历史索引。
- config/*.json：通信端点、处理链配置。
- 日志/回放文件：运行记录和测试数据。

不要再写“统一落 .ini/.csv/.json/.log”。

## drawio 总体评价
新增的 5 张架构图方向是对的：

- `01-业务架构.drawio` 能表达业务能力边界，也写清楚了上位机不负责航迹起批/融合/消失。
- `02-应用架构.drawio` 分层基本清楚。
- `03-数据架构.drawio` 把 DeviceInfo、EndpointConfig、TrackDisplaySnapshot、固定 DTO 拆出来，是有价值的。
- `04-技术架构.drawio` 对技术栈和线程模型有帮助。
- `05-核心流程.drawio` 最有用，但也最需要和代码契约保持一致。

建议后续把 `.drawio` 当作“架构契约”，不是美化材料。图上出现的链路，要么已经由代码支撑，要么明确标注“规划中 / 当前未实现”。
