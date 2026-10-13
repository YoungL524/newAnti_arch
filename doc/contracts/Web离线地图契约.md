# Web 离线地图契约

## 当前范围（2026-10-13）

**固定路径纠正**：开发/预览和打包默认地图统一为 `ui/maps/basemap.mbtiles`，生产仍为
`runtime/web/maps/basemap.mbtiles`。原开发配置硬编码的 ui/test-yuhua.mbtiles 已不存在，导致开发服务打不开，
不影响读取另一固定路径的旧部署服务。本次从用户现有 test-yuhua3.mbtiles 复制到固定开发路径，原文件保留，
并核对与当前部署地图 SHA256 一致；当前地图是 PNG、13～17 级，下面 12～16 级统计为初次接入文件。
更换地图后重启 Web 服务重开只读连接；缺失/权限错误提示包含实际读取路径，不靠猜测改名。

用户提供 `ui/test-yuhua.mbtiles`，要求离线仍显示 QGIS 导出的底图，优先简单显示。
不实施 GIS 编辑、测绘校准或坐标纠偏系统，不修改雷达协议、Core、控制或航迹所有权。

只读检查此文件得到：PNG、41,275,392 字节、1426 张瓦片、12～16 级；
范围为西/南/东/北 `118.574,31.831,118.76,31.9706`。此范围不是全国离线地图。
瓦片已包含 QGIS 渲染样式，前端直接显示原图，不再叠加模拟颜色滤镜。

## 最小链路

**2026-10-13 在线恢复纠正**：离线接入曾错误地将在线 AMap 也替换成普通高德 XYZ，造成远山黛样式丢失。
在线现恢复原 AMap JS API 2.0 与 `amap://styles/whitesmoke`，沿用原 VITE_GAODE_API_KEY/SECURITY_CODE，
不读取普通在线 XYZ 瓦片，不用滤镜模拟原样式。离线继续 Leaflet + MBTiles/XYZ，不加载 AMap SDK。
一次只运行当前激活的地图；切换保存中心和缩放，再销毁旧实例创建新实例，数据/会话/选择继续由 CoreProvider 唯一拥有。
新增最小 MapViewport 仅统一两个真实 SDK 的容器、投影和重画/点击事件；Canvas 绘制逻辑保持一份。
该接口不保存航迹或地图、不模拟 AMap API；若未来只剩一种地图 SDK，可删除这层边界。

### 顶层地图选择（2026-10-13）

界面提供在线/离线两个明确入口；离线下再选择 MBTiles 或 z/x/y，默认离线 MBTiles。
选择只属于地图组件视图状态，不改变采集、航迹数据、任务或控制。离线格式切换只替换图层；在线/离线切换按上述纠正规则更换实例，保留视图位置。
MBTiles 继续使用既有读取路由；z/x/y 使用 `/map/xyz/{z}/{x}/{y}`，目录为生产 `runtime/web/maps/xyz`、开发 `ui/maps/xyz`。
支持 `z/x/y.png`、`.jpg`、`.jpeg`、`.webp`，坐标按 XYZ 北向起算，不作 TMS 翻转；z 限制 0～22，单瓦片最多 4 MiB。
目录缺失/无当前瓦片仅提示，不自动切在线，不扫描整目录、不创建第二个地图服务。
此阶段不做文件选择器、下载程序联动、QGIS 导出自动化或地图包管理。
XYZ 不需要预扫描或额外元数据文件，不主动定位到其覆盖范围；切换保留当前中心和缩放，超出源允许层级时缩放被限制。
MBTiles 元数据异步返回只允许初次未交互时定位，不覆盖已拖动或切源的视图。

`MBTiles → 现有 Node Web 的 /map 路由 → Leaflet 栅格图层`。
雷达环、实时航迹、原历史结果图层、选中信息仍共用原 CoreProvider 与姿态计算，
只把投影和事件调用换成 Leaflet；不模拟 AMap API，不创建第二份航迹历史。
在线高德 SDK 加载器已恢复，Leaflet 与其 CSS/许可证随 UI 构建打包；两者只负责当前视图，各自不拥有业务状态。

默认离线，初次按地图包范围居中；用户选择在线时才加载原 AMap 远山黛，
不因离线缺瓦片自动联网。切换不重启采集或清空航迹；在线超时/失败可继续选择离线，不显示其他在线底图冒充原样式。

上述“图层按钮”为最初入口；现已改为地图左上方明确的在线/离线按钮和离线格式下拉框，原重复图层按钮已移除。
复位按钮以勾选设备为中心；没有勾选设备时回到地图包范围。
支持缩放、拖动和原有航迹点击选择；最大显示到 20 级，超出原始 16 级时放大已有瓦片，
不宣称得到更高地图精度。超出包范围没有底图，雷达和航迹覆盖层继续工作。

## HTTP 与生命周期

- `/map/metadata`：返回名称、经纬度范围、最小/最大级别和图片 MIME。
- `/map/tiles/{z}/{x}/{y}`：XYZ 请求，服务端转换 `tile_row = 2^z - 1 - y` 后按索引读取。
- 仅 GET/HEAD；非法坐标 400，缺瓦片 404，缺地图/格式不支持/读失败 503；不把错误返回成首页。
- 单个 Node 主事件循环拥有唯一只读 SQLite 连接，使用 Node 24+ 内置 SQLite；无额外 SQLite npm 原生依赖。
- 服务关闭释放连接，不改写地图、不创建业务数据库，不缓存完整地图或瓦片副本；一次读取最多 4 MiB。
- 生产沿用现有回环/同源访问限制；开发/预览通过 Vite 同一入口挂接地图路由。
- 地图加载失败仅显示提示，不能盖住或阻断其他业务组件；界面不输出旧高德 SDK 的密钥配置提示。

MBTiles 行号依照 [MBTiles 规范](https://github.com/mapbox/mbtiles-spec/blob/master/1.3/spec.md)。
读取使用 [Node SQLite](https://nodejs.org/docs/latest-v24.x/api/sqlite.html)，显示使用 [Leaflet](https://leafletjs.com/reference.html)。

## 部署与更新

开发/预览读取 `ui/maps/basemap.mbtiles`；生产固定读取 `dcmp/runtime/web/maps/basemap.mbtiles`。
`package_single_machine.ps1` 默认收集该源文件，也可用 `-MapFile` 指定其他兼容地图；
一并复制 offline-map.mjs，校验 Node 24+，地图与服务模块进入 SHA256 清单。
地图原文件已按本地大文件忽略，不自动纳入 Git；不要删除用户提供的文件。

完整打包时 `-XyzTileDirectory` 可指定 XYZ 根目录，目录中直接包含 z 子目录；
未指定时仅在 `ui/maps/xyz` 存在时收集。默认仍收集 MBTiles；仅打包 XYZ 可显式使用 `-MapFile ''`，
不允许两种离线输入都为空。打包创建 maps/xyz，并把已收集地图文件纳入 SHA256 清单。
开发 XYZ 目录按本地地图大文件忽略，不自动纳入 Git。

本次已将地图和新服务模块复制至 release-fixed；前端仍需用户构建并更新：

```powershell
Push-Location ui
npm run build
Pop-Location
Copy-Item ./ui/dist/* ./release-fixed/dcmp/runtime/web/dist/ -Recurse -Force
```

后续完整更新须同步服务和地图文件（先从托盘退出）：

```powershell
New-Item -ItemType Directory ./release-fixed/dcmp/runtime/web/maps -Force
Copy-Item ./ui/maps/basemap.mbtiles ./release-fixed/dcmp/runtime/web/maps/basemap.mbtiles -Force
Copy-Item ./ui/server/offline-map.mjs ./release-fixed/dcmp/runtime/web/server/offline-map.mjs -Force
Copy-Item ./ui/server/production.mjs ./release-fixed/dcmp/runtime/web/server/production.mjs -Force
```

用户构建后复制整个 dcmp 到现场，重新启动并 Ctrl+F5；不能只替换 Core EXE 更新地图 UI。
若替换了 MBTiles，应退出再启动以重开只读连接；不实现文件热替换或自动下载。
使用 XYZ 时把瓦片放成 `dcmp/runtime/web/maps/xyz/z/x/y.png`（或 jpg/jpeg/webp），
源文件缺失不影响 MBTiles；MBTiles 缺失也不影响 XYZ 路由。

## 用户验收

断网、无地图缓存浏览器下确认初始雨花台地图出现；拖动、缩放到 12～16 级，检查图像方向正确。
开始本地采集后验证航迹与表格对应、点击选择、停止/重开会话隔离、设备中心与覆盖环。
在线/离线切换不改变航迹状态；缺地图文件和拖出覆盖范围不影响本地 API、终端或航迹接收。
本次未新增坐标纠偏：用户后续按 GPS 真值校准；不据底图外观宣称地理对齐已通过。
代理仅静态检查和只读元数据查询，未编译、运行测试或启动业务服务，当前标记待用户验证。
