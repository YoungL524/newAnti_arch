# Review: cdd6a2d

Commit: `cdd6a2d` (`review-ff81749修改完成`)

## 总体判断

这版把 RuntimeSettings 进入最小实现前的关键边界基本补齐了：

- 参数范围已定义：`frequencyControl` / `waveControl` 为 `0..255`。
- 400 错误响应示例已补。
- `DphPublisher::applyRuntimeSettings()` 已明确只更新映射字段，不重置 `sceneNumber` / `sendCount` / timer / route / workMode。
- `RuntimeSettingsTypes.h` 已加入 `core/CMakeLists.txt`。
- `GenericPackets.h` 已记录 payload/source 编号区间。

架构上可以进入最小代码实现了。但当前提交还有两个很小的清理项，其中一个会影响 Markdown 文档渲染。

## P1 - `运行时参数管理.md` 多了孤立代码块结束符，后续文档会渲染错

文件：`doc/business/运行时参数管理.md`

在 `DphPublisher::applyRuntimeSettings 语义` 小节末尾，多了一个孤立的：

```text
```
```

它前面没有对应的代码块起始符，会导致后续 Markdown 渲染混乱。

### 建议改法

删除这一行孤立的 triple backtick。

## P2 - `core/main.cpp` 引入了无意义尾随空格

文件：`core/main.cpp`

这次 diff 只有：

```diff
-    printBanner();
+    printBanner(); 
```

这是无意义格式噪声，建议清理掉。小事，但这种噪声会污染 review diff。

## 已确认修复项

- `frequencyControl` / `waveControl` 参数范围已补。
- `applyRuntimeSettings()` 不重置 DPH 持续状态的语义已补。
- `RuntimeSettingsTypes.h` 已加入 CMake header 清单。
- payload/source 编号区间已记录。
- RuntimeSettings 的主边界已经可以支撑最小实现。

## 推荐 Claude 下一步

1. 删除 `doc/business/运行时参数管理.md` 中孤立的 ```。
2. 删除 `core/main.cpp` 的尾随空格。
3. 然后可以进入 RuntimeSettings 最小代码实现：
   - `RuntimeSettingsService`
   - `GET/PUT /api/runtime-settings`
   - `DphPublisher::applyRuntimeSettings()`
   - AppController/bridge 订阅 `SOURCE_RUNTIME_SETTINGS`
   - 基础单元测试或集成验证

## 架构结论

Milestone 2 的文档边界已经基本定住，可以进入最小实现。后续重点不是再扩字段，而是先把这条链路跑通：

```text
PUT runtime-settings
  -> RuntimeSettingsService 校验并更新快照
  -> SystemBus 发布 DphParamsChanged
  -> bridge queued invoke
  -> DphPublisher 更新本地缓存
  -> 50ms DPH 使用新参数发送
```
