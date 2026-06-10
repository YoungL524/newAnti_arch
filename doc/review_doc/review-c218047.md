# Review c218047

提交：`c218047 review-8c898cf修改完成`

## 总体结论

这轮主要是同步 `doc/business/运行时参数管理.md`，没有核心代码改动。  
RuntimeSettings 的关键事务语义已经写进文档：

- JSON 解析失败：不 validate、不 publish、不 set。
- validate 失败：不 publish、不 set。
- publish 失败：API 返回失败、不 set。
- publish 成功：再提交 RuntimeSettingsService 本地快照。

整体方向正确，Milestone 2 后端可以继续按“已基本收口”处理。

## P2：数据流文档里 bridge 与 set 的时序建议再精确一点

位置：`doc/business/运行时参数管理.md:23-31`

当前文档写成：

```text
RuntimeSettingsService.validate(settings)
  → SystemBus publish DphParamsChanged
  → RuntimeSettingsService.set(settings) （仅 publish 成功后提交）
  → AppController bridge 订阅 SOURCE_RUNTIME_SETTINGS
  → Qt::QueuedConnection invoke DphPublisher::applyRuntimeSettings(settings)
```

业务语义上这样容易读懂，但从代码实际时序看，`SystemBus::publish()` 会同步执行订阅回调。也就是说：

```text
publish()
  → AppController bridge 回调同步触发
  → bridge 内部 QMetaObject::invokeMethod(..., Qt::QueuedConnection) 入队
publish() 返回 true
  → RuntimeSettingsService.set(settings)
Qt 事件循环后续执行
  → DphPublisher.applyRuntimeSettings(settings)
```

所以更准确的表达不是“set 之后 bridge 才订阅/触发”，而是：

```text
RuntimeSettingsService.validate(settings)
  → SystemBus publish DphParamsChanged
      → AppController bridge 同步收到事件，并 queued invoke DphPublisher.applyRuntimeSettings(settings)
  → publish 成功后 RuntimeSettingsService.set(settings)
  → Qt 事件循环执行 DphPublisher.applyRuntimeSettings(settings)
```

这不是当前功能 bug，但建议 Claude Code 把文档改得更精确一点。  
原因是 RuntimeSettings 链路的核心契约已经变成“publish 失败不 commit；publish 成功后 commit；DPH 应用由 queued invoke 延后执行”。如果文档时序写得太抽象，后面做测试或排查事件链路时容易产生误判。

## P2：测试计划仍建议补一条记录

上一轮建议里提到“如果暂时不方便 mock SystemBus，至少写进 doc/test 或后续测试计划”。这轮文档同步了业务链路，但还没有看到测试计划落点。

建议 Claude Code 后续补一个轻量文档或测试用例条目，固定三件事：

1. validate 失败不 publish、不 set。
2. publish 失败不 set。
3. publish 成功后 set，并最终触发 DphPublisher.applyRuntimeSettings。

这不需要阻断当前开发，但它是 Milestone 2 后续不回退的保护线。

## 下一步建议

这轮不建议继续在 RuntimeSettings 后端链路上打磨太久。  
把上面文档时序修准确后，可以进入下一阶段：

```text
Milestone 3：TrackDisplayManager / 固定航迹显示协议
```

下一阶段的重点不是航迹起批/消失管理，而是：

- 接收 `RadarDataProcessor` 输出后的航迹点；
- 维护“前端应该看到的航迹显示状态”；
- 统一输出固定的 Web 航迹显示协议；
- 支持选中、追踪使能、前 N 个点样式、稳定点样式、数据转发等显示/业务状态。

