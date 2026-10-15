# NewAntiDrone

Qt/C++ Core + React Web 雷达上位机，采用共享强类型数据域、Consumer 游标读取和独立 Command 控制平面。
开始工作先读 [AGENTS.md](AGENTS.md)、[PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) 和 [当前任务](doc/workflow/CURRENT_TASK.md)。

## 当前功能

- Core：航迹/心跳 UDP 入口、设备/任务 API、WebSocket 显示、SQLite 归档及设备活动日志。
- Web：设备任务界面、实时航迹、原 AMap 在线远山黛、本地 MBTiles/XYZ 地图、字体图标与交互终端。
- Windows 启动器：隐藏 Core/Web 控制台、托盘与单实例、进程树管理、Core 正常停机；DP/SP 默认禁用，暂由现场手动启动。

UDP 接收为 `127.0.0.1:7788`；Core HTTP 为 `127.0.0.1:8088`，WebSocket 为 `127.0.0.1:8089/ws/v1`；
生产浏览器统一访问 `http://127.0.0.1:8080`。显示航迹需激活任务并开始本地采集；这仍可能发送现有设备控制报文。
用户已反馈开发终端及部署字体图标可用；其他运行验收边界见 PROJECT_CONTEXT。

历史任务日志与航迹查询/回放已实现源码，待用户编译验收：选择左侧历史任务，活动日志可翻页；地图下方可查询起止时间、
播放/暂停、页内定位、下一页及本页全览。航迹每页最多 1000 条，旧页会被替换；日志每页最多 200 条。
回放按采集时设备安装 revision 定位；没有归档历史 DP 动态位姿，不能把重建位置当成当时实时位姿的精确复现。
回放是数据库只读操作，不启动采集或发送控制命令；实时与历史视图分开，切回当前任务即可退出回放。

侧栏底部“系统日志”打开全局归档查询：支持任务/设备 UUID、事件域、精确级别及本地起止时间筛选。
空条件包含无任务的系统事件，每页最多 200 条；查询和翻页替换当前页，不影响采集或回放。源码待编译运行验收。

侧栏“程序运行记录”可按启动时间和退出标记查询程序历史运行，每页最多 200 条；未标记退出不等于进程仍存活，
异常检测时间不是退出时间。系统闭环待运行清单见 [系统闭环验收](doc/workflow/2026-10-15-系统闭环验收.md)。

源码基线已补会话整组事务、正常关停收口及同库 Core 独占锁。实时地图只保留最近 256 个显示目标，各 20 个近期点；
浏览器旧路径每目标最多 2048 点、全页最多 32768 点，超限裁剪显示前缀，完整已提交观测仍从归档查询。
当前改动与待验收边界见 [固化前隐患收敛](doc/workflow/2026-10-15-固化前隐患收敛.md)，未经编译及运行确认。

## 本地准备

- Windows x64，Qt 5.14.2 / MinGW 7.3 对应工具链，CMake。
- Node 24+（本地地图使用内置 SQLite）；开发安装依赖，生产包自带 Node。
- 本地高德 Key/安全码放 `ui/.env`，参考 `ui/.env.example`，不提交实际值。
- 离线地图放 `ui/maps/basemap.mbtiles`，可选 XYZ 放 `ui/maps/xyz/z/x/y.png`（亦支持 jpg/jpeg/webp）。
  地图大文件不进 Git，新克隆需要自行复制；无地图时其他本地业务仍能工作。

开发 UI：

```powershell
Push-Location ui
npm ci --ignore-scripts
npm run dev
Pop-Location
```

Core 需用户自行构建并启动。字体随仓库与构建产物提供；离线不会访问 Google Fonts，在线地图才加载 AMap SDK。
更换地图文件后重启 Web 服务，避免只读连接仍指向旧文件。

## 用户构建与部署

在对应 Qt/MinGW 工具环境中，由用户执行：

```powershell
cmake -S . -B build-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/mingw73_64"
cmake --build build-release --target NewAntiDrone NewAntiDroneLauncher
Push-Location ui
npm run build
Pop-Location
./scripts/package_single_machine.ps1 -BuildDirectory ./build-release -QtBinDirectory "C:/Qt/Qt5.14.2/5.14.2/mingw73_64/bin"
```

打包输出默认 `release`，必须是全新或空目录；不会构建或覆盖既有部署和数据库。
可用 `-MapFile`、`-XyzTileDirectory` 指定地图输入；`-MapFile ''` 配合 XYZ 输入可只收集目录地图。
收集结果包含程序/网页/地图 SHA256 清单；时间戳检查仅防明显旧产物，不替代运行验收。
部署复制完整 dcmp 目录，用户入口为 `dcmp/NewAntiDrone.exe`；现场电脑不需要 Qt、Node 或 QGIS。

更新当前本地 release-fixed 的 UI（先从托盘退出）：

```powershell
Push-Location ui
npm run build
Pop-Location
Copy-Item ./ui/dist/* ./release-fixed/dcmp/runtime/web/dist/ -Recurse -Force
Copy-Item ./ui/server/production.mjs ./release-fixed/dcmp/runtime/web/server/production.mjs -Force
Copy-Item ./ui/server/offline-map.mjs ./release-fixed/dcmp/runtime/web/server/offline-map.mjs -Force
New-Item -ItemType Directory ./release-fixed/dcmp/runtime/web/maps -Force
Copy-Item ./ui/maps/basemap.mbtiles ./release-fixed/dcmp/runtime/web/maps/basemap.mbtiles -Force
```

重新打开并 Ctrl+F5。如果只改 Core，用户重新编译后替换 `runtime/core/NewAntiDroneCore.exe`；
只复制 Core 不会更新 UI。完整交付优先重新收集到空目录，不沿用被手动修改后的旧 SHA256 清单。

## 合同与验证

- [架构规范](doc/architecture/09-共享强类型数据域与游标读取模型.md)
- [单机部署](doc/contracts/单机部署契约.md)
- [离线地图与在线恢复](doc/contracts/Web离线地图契约.md)
- [Web 本机终端](doc/contracts/Web本机终端契约.md)
- [活动日志](doc/contracts/SystemEvent错误模型契约.md)

代理遵守仓库规则：不编译、不运行测试；可做类型、语法、中文 Doxygen 和差异静态检查。
用户负责构建、设备联试与部署验收，不将静态通过表述为现场已验收。
