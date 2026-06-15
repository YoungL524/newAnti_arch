# Review: 008b4b9

## 结论
这一版已经把上一轮最关键的问题修掉了：`/api/devices/` 的 `PUT` 前缀路由不再被重复注册，设备编辑和航迹交互重新回到了同一个统一入口。  
前端的 `interaction` 和 `style` 类型也基本收紧，航迹交互链路现在符合我们前面定的方向：前端发交互意图，后端维护状态，下一帧 snapshot 驱动 UI 显示。

这轮没有发现新的高优先级阻塞问题，可以认为“航迹选中交互第一阶段”基本闭合。

## 已关闭问题
上一版提到的重复注册问题已经解决：

`core/web/WebApiController.cpp`

后面那段重复的 `Track Interaction API` 已删除，现在只剩：

- 设备 registry 存在时：`/api/devices/` 的 `PUT` handler 先尝试 `handleTrackInteraction`，不匹配再走设备更新。
- 设备 registry 不存在但 track manager 存在时：保留一个 fallback handler。

这个分发结构是合理的。

## 仍建议优化 1
`setTrackInteraction` 失败时目前只 `console.warn`，用户界面没有反馈。

位置：
`ui/src/hooks/useCoreApi.tsx`

这对调试没问题，但对业务操作不够稳。用户点击航迹后，如果后端拒绝、网络失败、路径错误、trackId 不合法，界面上不会出现任何提示，只会停留在旧 snapshot 状态。  
在现场使用时，这种“点了没反应”会很难判断是没点中、网络问题、还是后端拒绝。

建议：
给交互操作增加一个轻量错误状态，比如 `interactionError`，或者复用现有 `commandError` 但文案区分“启停失败”和“航迹交互失败”。  
不用做复杂弹窗，顶部短提示即可。

## 仍建议优化 2
`setTrackInteraction` 的类型现在声明成返回 `void`，但实现是 async。

位置：
`ui/src/hooks/useCoreApi.tsx`

当前调用方不会等待结果，这和“前端不立即改样式、等待后端 snapshot”这个设计并不冲突。  
但从契约上看，交互命令是一次会失败的远程操作，最好把返回类型显式改成：

`Promise<boolean>`

这样后续无论是地图点击、表格点击，还是追踪使能按钮，都能知道命令是否被后端接受。

## 仍建议优化 3
`TrackLayer` 的点击命中逻辑目前只检查每条航迹的最后一个点，也就是只能点航迹头。

位置：
`ui/src/components/map/TrackLayer.tsx`

这可以作为第一阶段接受，因为航迹头是最明确的交互目标。  
但后续如果用户习惯点击航迹线或历史点，这里会表现为“点不中”。建议先明确产品规则：  
如果只允许点击航迹头，就把命中半径、鼠标样式、选中态反馈做清楚；如果允许点击整条航迹，则命中检测要扩展到线段距离。

## 架构状态
Milestone 3 当前进度可以这样判断：

- `TrackDisplayManager` 作为后端航迹显示状态管理者的方向成立。
- Web snapshot 固定协议已经开始承担 UI 显示输入。
- 前端已经不再本地伪造选中样式，而是通过 API 把交互意图交给后端。
- 当前还缺的是“追踪使能”的明确 UI 入口，以及交互失败反馈。

建议下一步不要继续在选中点击上做太细，可以推进“追踪使能”这个完整业务动作：  
用户选择某条航迹后，后端状态改变，snapshot 样式改变，同时明确这条航迹的数据后续是否需要转发、转发给谁。

