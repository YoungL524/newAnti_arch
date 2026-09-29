# 当前任务：Python GUI 控制链路联试

## 状态

待用户验证

## 目标与实现

- 用户要求以后测试程序统一使用 Python，并明确授权删除此前测试程序的源码和 exe。
- 新增 `tools/control_link_gui.py`：Tkinter 窗口监听设备台账 DPH 目标端口；发送 DP/SP 在线心跳；校验并显示 `0x0001` 的启动/保持采集（1）和待机/停止采集（0）报文。
- 删除旧 `tests/` C++ 用例、CMake 测试目标、C++ 心跳模拟器、前端 Vitest 用例和旧基线测试脚本；删除 `build` 与 `build-baseline` 中对应测试 exe，保留正式 `NewAntiDrone.exe`。
- 使用说明见 `tools/README.md`。此前控制参数面板任务的记录已归档至 `doc/workflow/archive/TASK-20260928-01-control-settings.md`；该任务的运行验收尚未完成。

## 用户验收

1. 在设备台账把 DPH 目标指向窗口监听地址，启动窗口并确认 DP/SP 心跳使设备显示在线。
2. 开始采集后应持续看到有效 944 字节 `0x0001` 报文，`F_UI_STBY_Ctrl=1`；停止后应看到一次 `F_UI_STBY_Ctrl=0`，随后不再收到周期报文。
3. 取消任一路心跳，等待 DCMP 超时后应暂停控制发送；恢复双心跳后应续发。无效包应显示错误且不改变最近有效控制状态。

代码由代理静态检查；按仓库规则，代理不编译、不运行测试或联试。旧 CMake 构建目录仍有缓存，用户编译时须先重新配置 CMake。

## 2026-09-29 全项目静态复核

- 对照 `AGENTS.md`、新架构4和 09 文档核查数据 Topic、Consumer 游标、ExecutionLane 与独立命令 Owner；未发现新增中央消息总线、控制 FIFO、ACK 或第二个设备命令发送路径。
- 对照现行协议核查 `0x0001`、心跳、航迹及新登记的 `0xFFFF`。发现 DP 交付头 `doc/protocol/toDP.h` 缺少 `0xFFFF`，已补同名枚举、20 字节载荷和布局断言；运行发送链仍只使用 `0x0001`。
- 中文 Doxygen 审计、Python AST 解析和 Git 差异空白检查通过。用户已报告上一轮后端编译通过，但本轮新增 `0xFFFF` 类型和 Python GUI 尚未经用户编译、运行或设备联试；当前任务保持“待用户验证”。
