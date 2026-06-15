# Review: 97544c4

## 结论
这一版完成了上一轮建议里的两个关键点：

- `setTrackInteraction` 改成了 `Promise<boolean>`，交互命令不再是“发出去就算完”。
- 增加了 `interactionError`，航迹交互失败会在 UI 上提示，不再只写 `console.warn`。

这说明“航迹交互第一阶段”的工程闭环基本完成。没有发现新的后端架构问题。

## 建议优化 1
`TitleBar` 现在有三类错误/提示状态：

- `noSelectionHint`
- `commandError`
- `interactionError`

它们都是 `absolute top-full right-4` 的浮层。当前 `interactionError` 只在 `commandError` 存在时加 `mt-10`，但没有考虑 `noSelectionHint`。  
如果用户先触发“未勾选设备提示”，随后又触发航迹交互失败，这两块提示有重叠风险。

位置：
`ui/src/components/layout/TitleBar.tsx`

建议：
不要继续给每个提示单独算 `mt-*`。建议把这三类提示收敛成一个统一的提示栈，例如：

`<div className="absolute top-full right-4 mt-1 flex flex-col gap-2">`

里面按状态渲染每条提示。这样后续再加设备错误、任务错误、转发错误，也不会继续堆定位逻辑。

## 建议优化 2
`setTrackInteraction` 已经返回 `Promise<boolean>`，但当前调用方还没有使用返回值。

位置：
`ui/src/components/map/TrackLayer.tsx`  
`ui/src/components/panels/DataTable.tsx`

这不是阻塞问题，因为失败提示已经由 `useCoreApi` 统一处理。  
但如果后面要做按钮 loading、点击防抖、或者失败后恢复交互态，调用方需要 `await` 这个结果。

建议：
暂时可以不改；等做“追踪使能”明确 UI 时，再让调用方使用返回值。

## 架构判断
这版符合我们定下来的边界：

- 前端只发交互意图。
- 后端维护航迹显示状态。
- UI 显示仍等待 snapshot 更新。
- 错误反馈由 core hook 统一收口。

下一步可以从“选中”推进到“追踪使能”的完整业务动作。重点不是再补点击小细节，而是定义：

- 追踪使能入口在哪个 UI 上。
- 一次只能追踪一条还是允许多条。
- 追踪使能后 snapshot 样式如何变化。
- 追踪使能后航迹数据是否转发，以及转发目标是谁。

