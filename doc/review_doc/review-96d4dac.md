# Review 96d4dac

提交：`96d4dac review-c218047修改完成`

## 总体结论

这轮主要补齐 RuntimeSettings 的业务文档与验证清单，没有核心代码改动。  
我复核后认为这版可以通过：

- `doc/business/运行时参数管理.md` 已经把 `SystemBus::publish()` 同步触发 bridge、`Qt::QueuedConnection` 延后执行 `DphPublisher.applyRuntimeSettings()` 的真实时序写清楚。
- 事务语义已经明确为 `validate -> publish -> set`，并说明 publish 失败不提交本地快照。
- 新增 `doc/test/运行时参数管理-验证清单.md`，覆盖 validate 失败、publish 失败、成功应用、非法输入等关键路径。
- 验证清单里的 “API 返回 400” 与当前 `WebService` 实现一致：handler 返回 `ok:false` 时，`WebService` 会用 HTTP 400 返回 JSON。

当前没有发现需要 Claude Code 继续修改的阻断问题。

## 轻量建议：Milestone 2 可以正式收口

RuntimeSettings 后端链路已经具备稳定边界：

- UI 不直接操作 `DataPackageHeader`。
- `RuntimeSettingsService` 负责业务校验和当前参数快照。
- Web 层只负责 JSON 解析与 API 表达。
- `SystemBus` 负责运行时参数变更事件。
- `DphPublisher` 只接收 bridge 传来的 settings 并映射到 DPH 字段。
- `sceneNumber` / `sendCount` 不参与可写 settings。
- 工作/待机状态仍由 `RadarWorkModeController` 管理。

因此建议不要继续在 Milestone 2 上细抠，除非后面真实联调暴露问题。  
下一步可以进入：

```text
Milestone 3：TrackDisplayManager / 固定航迹显示协议
```

## Milestone 3 启动提醒

进入 Milestone 3 时，先不要把问题定义成“航迹管理”。  
按照之前架构讨论，上位机不负责航迹起批、消失、融合等数据处理职责；上位机要做的是“航迹显示数据管理”：

- 接收 `RadarDataProcessor` 输出后的航迹点。
- 转成 Web 固定航迹显示协议。
- 维护每条航迹在当前 UI 状态下的显示样式。
- 支持前 N 个点样式、稳定点样式、选中、追踪使能、数据转发等状态。
- UI 订阅的是“当前应显示的航迹状态”，不是原始协议点。

建议 Claude Code 下一轮先补设计文档或最小接口草案，再开始写代码。

