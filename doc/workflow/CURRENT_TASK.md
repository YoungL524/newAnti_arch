# 当前任务：清理、交接与 GitHub 同步

## 状态（2026-10-13）

用户授权清理项目文件、更新 AGENTS/PROJECT_CONTEXT/相关文档，并提交推送一版代码。
本次交接提交包含活动日志、Web 终端、单机部署、本地字体图标、在线 AMap 远山黛恢复、离线 MBTiles/XYZ 及固定路径修复。
实际提交号和推送同步状态由 Git 核对，不在文档硬编码自身提交号。

## 清理与保留

- 删除无引用的 doc/1.html、doc/2.html 演示和两份 tsbuildinfo、旧 vitest 缓存。
- 原地图 test-yuhua3.mbtiles 与 ui/maps/basemap.mbtiles、部署副本三份哈希一致后删除重复原件；保留后两份，约释放 263.9 MiB。
- 保留当前构建目录、release-fixed、依赖、用户配置、业务数据及协议 Excel 独立修改。
- 上一版长交接文档已完整归档至 history/2026-10-13-PROJECT_CONTEXT.md、history/2026-10-13-CURRENT_TASK.md，当前入口只保留现行状态。

## 最新功能断点

1. 用户已确认开发终端可用、部署字体图标正常，并反馈另一台电脑部署可打开。
2. 在线原 AMap 远山黛已恢复源码；离线 Leaflet 支持 MBTiles/XYZ。当前只运行被选择的引擎，视图位置保存，数据和绘图逻辑共用。
3. 开发与打包地图默认 ui/maps/basemap.mbtiles，生产 runtime/web/maps/basemap.mbtiles；缺文件提示真实路径。
4. 最新地图为 PNG、13～17 级；最新路径修复和在线/离线切换仍待用户重启/构建后验证。
5. Core 接收保持 127.0.0.1:7788，DP/SP 手动启动；代码仍有原控制发送，不是纯接收模式。

## 验证与提交范围

代理只执行 TypeScript --noEmit、Node 语法、PowerShell 解析、中文 Doxygen 审计和 Git 差异/暂存检查。
不编译、不运行测试、不启动服务；数据库新库/旧库迁移、进程退出、地图交互等运行行为不能据此标记已验收。
协议 Excel 排除本次提交，地图/构建/部署包/.env 由忽略规则排除；提交后工作区允许只剩协议 Excel 的独立修改。

## 新会话恢复

1. 阅读 AGENTS.md、PROJECT_CONTEXT 顶部及本文件，执行 git status --short --branch、git log -1。
2. 确认本地 HEAD 与 origin/dev 状态，保留 Excel 和忽略的本地机器资产。
3. 先接收用户对现有修复的反馈；没有新授权时不自动进入下载程序联动、GIS、分布式或控制扩展。
4. 如需更新部署，只构建/替换对应产物，UI 更新不能只复制 Core EXE。步骤见 README 与部署/地图合同。
