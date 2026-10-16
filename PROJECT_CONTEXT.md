# 项目现状与交接

更新：2026-10-16。先读 AGENTS.md、本文件、[当前任务](doc/workflow/CURRENT_TASK.md)，再核对实际 Git 状态。

## 当前阶段

用户反馈当前未发现明显 bug，授权清理项目、提交推送并固化源码基线。随后按模块逐项细化，
每项完成由用户确认后单独固化，全部模块完成后再做整体部署。本轮不提前实施模块功能或更新部署包。

架构主链已实现：共享强类型数据域、Consumer 独立游标、固定 ScratchBuffer、ExecutionLane 合并通知；
Command 独立，经 ApplicationController → DeviceController → DcmpProtocolAdapter → 唯一本地 UDP 端点发送。
核心规范为 [09 数据域模型](doc/architecture/09-共享强类型数据域与游标读取模型.md)和新架构4.png。

## 已实现的系统闭环

- 接入与控制：雷达航迹/DP/SP 心跳、协议门禁、双心跳门控 50 ms 的 0x0001，采集按钮触发一次 0xFFFF；无 ACK、FIFO 或重试命令链。
- 设备与任务：设备 revision、任务模板及独立参数快照、激活/采集/完成/归档，多阵面会话整组事务及失败回滚。
- 实时显示：独立 Device/Task/System 状态快照、重连同步、航迹 snapshot/delta、在线 AMap 远山黛与离线 Leaflet 地图。
- 存储与查询：SQLite 唯一存储线程、航迹与事件独立提交游标、归档降级/gap、历史航迹/事件/运行记录分页查询和只读回放。
- 生命周期：同库 Core 路径独占、正常退出收口会话并排空归档、异常运行检测；本机接口来源门禁与有界诊断文件。
- 已有部署基础：隐藏控制台启动器、托盘、进程树管理、Web 本机终端、地图/字体离线资产收集。整体部署完善排在模块细化之后。

## 最近收敛

HTTP/WebSocket 检查本机 Host/Origin；有正文的写请求必须 JSON，原生 HTTP 和无正文生命周期命令保留。
归档事务失败保留 cursor，唯一存储运行时一秒单次重试，没有新数据也能恢复尾部；持续故障仍可能产生 retention gap。
识别类型在发布前限定 Unknown，与数据库一致。Core Qt 警告/错误保存在数据库旁 .diagnostics.log，最多两个 1 MiB 文件。
Web 显示最多 256 目标、每目标 20 点；浏览器旧路径每目标 2048 点、全页 32768 点，只裁剪显示，不伪造消亡。

## 验证与下一步

用户反馈当前运行未发现明显问题，作为本次阶段固化依据；代理仅执行静态检查，未编译或运行测试。
尚未获得全部故障注入、吞吐及干净电脑部署的逐项证据，不把阶段固化表述为完整产品验收。
版本、清理范围及验收项见[固化基线](doc/workflow/固化基线与验收.md)；后续顺序见[模块细化计划](doc/workflow/模块细化计划.md)。
下一项建议为“数据回放与归档”：先分清侧栏数据回放与日志归档入口，再细化历史查询和回放交互。
本轮只整理计划；开始下一模块及后续提交推送仍需对应用户授权。

## 本地资产与固定约定

- build、build-release 保留 CMake 配置及可执行程序；已清理对象文件、静态库和 TypeScript 增量缓存，下次构建会重新生成。
- 所有现有数据库、配置、release-fixed 中的 DP/SP 和部署资产保留；不覆盖用户运行数据。
- 在线地图固定 AMap 2.0 amap://styles/whitesmoke；离线 Leaflet + MBTiles/XYZ，不自动联网补瓦片。
- 地图开发路径 ui/maps/basemap.mbtiles、ui/maps/xyz；生产 runtime/web/maps/basemap.mbtiles、runtime/web/maps/xyz。更换后重启 Web。
- .env、地图、依赖、构建及部署包不提交 Git；独立协议 Excel 修改继续留本地，不还原、不提交、不据此更改协议。
- 用户提供的两份评估原件保留本地并忽略；过时迁移、原型、旧实施与交接文档已删除，历史以 Git 追溯。
- Core 端口：UDP 7788，HTTP 8088，WebSocket 8089/ws/v1；生产页面 8080。DP/SP 暂手动启动。

构建和打包由用户执行，见 [README](README.md)。本轮提交与远端同步以 git log/status/rev-list 实际结果为准。
