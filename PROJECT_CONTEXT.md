# 项目上下文与新会话交接

## 当前接力断点（2026-10-13，先读本节）

本次交接版汇总活动日志、Web 终端、单机部署、离线字体与地图来源选择。用户已授权整理代码后提交并推送 GitHub。
分支为 `dev`，远程为 `origin`；最新提交与同步状态以 `git log -1`、`git status --short --branch` 和 `git rev-list --left-right --count HEAD...origin/dev` 为准。
`205d7cd` 是本次累计改动之前的历史基线，不得继续当成固定的最新提交号。

### 已实现及验证边界

1. **Core 与业务链**：UDP 航迹/心跳仍共用 `127.0.0.1:7788`，HTTP `8088`，WebSocket `8089/ws/v1`。
   数据域与唯一 DeviceController 控制 owner 不变；设备事件通过唯一 EventTopic/Web/Archive 链路。
   新数据库重复添加 `archived_utc_ms` 的初始化/升级错误已修复，兼容已有异常初始化文件，不删除用户数据库。
2. **Web 本机终端**：xterm.js + node-pty/ConPTY，开发与生产复用同一实现，限本机同源，输出有背压与容量上限。
   用户已反馈开发终端非常好用；生产终端与进程退出尚无完整专项验收记录。
3. **单机启动器**：Windows `NewAntiDroneLauncher` 收集后作为 `dcmp/NewAntiDrone.exe`，隐藏 Core/Web 控制台，托盘、单实例、Job 进程树所有权及 Core 正常停机 IPC 已实现。
   用户反馈部署版能打开；完整退出、故障回滚和干净电脑专项验收仍待确认。DP/SP 自动联启暂不推进，默认禁用，现场手动启动。
4. **字体图标**：Inter、JetBrains Mono、Material Symbols 已本地化，许可证随包；用户已反馈部署字体图标正常。
5. **地图**：保留明确的在线/离线入口；在线已恢复原 AMap JS API 2.0 与 `amap://styles/whitesmoke`（远山黛），沿用本机 Key/安全码。
   离线为 Leaflet + MBTiles 或 z/x/y，绝不因缺瓦片自动联网。两个真实 SDK 共用最小 MapViewport 投影/事件边界和原有 Canvas 绘制逻辑，不复制业务历史。
   先前用普通在线 XYZ 替代 AMap 是错误方案，已经撤销，不得再据历史记录恢复该替换。
6. **最新地图路径修复**：开发/预览与打包默认源统一 `ui/maps/basemap.mbtiles`；生产读取 `dcmp/runtime/web/maps/basemap.mbtiles`。
   原开发配置硬编码的测试文件已不存在，导致 unable to open database file；已修复并增加真实路径诊断。
   当前固定地图只读元数据为 PNG、13～17 级；与部署副本 SHA256 一致。用户仍需重启 Web 服务验证最新修复。

代理本轮及前项均未编译项目、未运行测试或业务服务。已做无产物类型、语法、中文注释和静态差异检查；
不得将源码实现或静态通过写成完整运行验收。在线远山黛恢复、来源切换和最新路径修复仍待用户明确反馈。

## 当前范围与下一步

- 优先由用户验证：重新构建并更新 UI，在线仍是原远山黛，离线地图能打开，切源不影响采集与选择；路径修复需重启开发或部署 Web。
- 航迹显示仍要求激活任务并开始本地采集，心跳不门控本地采集；阵面选择须与实际数据匹配。
- 本程序目前不是纯接收模式：采集切换发送 0xFFFF 至 127.0.0.1:1234；任务准备且 DP/SP 双心跳在线时周期发送 0x0001 至 127.0.0.1:2345。
- 暂不做分布式、DP/SP 自动联启、历史日志查询、历史航迹回放、下载程序/QGIS 自动联动或 GPS 精校。已有历史绘制组件不代表回放接口已接通。
- 用户用 QGIS 导出底图，项目位于外部 `D:\QGIS\project\gaode_Mystyle.qgz`；QGIS 不进入运行部署依赖。
- 新会话不自动开工新的功能，由用户决定下一项。具体最新清单见 CURRENT_TASK。

## 本地资产与 Git 边界

- `build`、`build-release`：用户当前开发/打包产物，保留；其中可能有测试数据库，不能一概删除。
- `release-fixed`：当前本地包，保留实际 DP/SP、配置和地图，不进入 Git。
- `ui/maps/basemap.mbtiles`：当前固定离线源；`ui/maps/xyz`：可选 XYZ 源。两者均按本地大文件忽略。
- 当前地图重复原件 `ui/test-yuhua3.mbtiles` 已在三份 SHA256 一致后去重，固定源和部署副本保留。
- `ui/.env`、node_modules、dist、构建缓存不提交；字体资源与许可证提交，其他电脑无需再联网下载字体。
- 协议 Excel `doc/protocol/★数据协议(四面阵雷达)_应用版（1.0）_20260923.xlsx` 仍有来源未核实的独立修改：保留在本地，排除本次代码提交，不还原也不据此改协议。
- 旧 HTML 演示 doc/1.html、doc/2.html、过期缓存已清理；更早失败包 release、build-baseline 与 output.txt 已在前项清理。

## 阅读和运行入口

先读 AGENTS.md → 本节 → doc/workflow/CURRENT_TASK.md，再核对 Git 实际状态。

- 架构：[09 共享数据域规范](doc/architecture/09-共享强类型数据域与游标读取模型.md)，固定总览 doc/architecture/新架构4.png。
- 部署：[单机部署契约](doc/contracts/单机部署契约.md)，打包入口 scripts/package_single_machine.ps1。
- 地图：[Web离线地图契约](doc/contracts/Web离线地图契约.md)，UI 入口 ui/src/components/map/MapRadarView.tsx。
- 终端：[Web本机终端契约](doc/contracts/Web本机终端契约.md)。
- 活动日志：[SystemEvent错误模型契约](doc/contracts/SystemEvent错误模型契约.md)。
- 快速构建与本地部署步骤见 README.md；新克隆需自行提供本地 .env 和离线地图，Git 不包含这些机器资产。

## 历史记录

本次整理前的完整上下文和逐轮记录保存在 doc/workflow/history/2026-10-13-PROJECT_CONTEXT.md、
doc/workflow/history/2026-10-13-CURRENT_TASK.md；只在需要追溯时阅读，历史状态不能覆盖本节与现行合同。
