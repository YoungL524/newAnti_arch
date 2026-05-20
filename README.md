# DCMP — 反无人机雷达指控系统 (新架构)

> **D**rone **C**ountermeasure **M**onitoring **P**latform

基于 **Qt 5.14.2 (C++17)** 内核 + **React 19 + Vite** 前端的雷达上位机系统，支持实时 PPI 雷达显示、目标跟踪、数据采集和日志监控。

![UI 预览](ide_design.png)

---

## 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        Browser (React UI)                       │
│  ┌─ TitleBar ─ ActivityBar ─ Sidebar ─────────────────────────┐ │
│  │                                                             │ │
│  │               PPI Radar Display (全屏 Canvas)               │ │
│  │    · 距离环 / 角度标签 / 扫描线 / 数据点 / 图例             │ │
│  │                                                             │ │
│  │  ┌─────── 底部面板 (目标列表 | 日志 | 告警 | 事件) ───────┐ │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                         HTTP REST / WebSocket                      │
│                             8088 / 8089                            │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────────┐
│                    C++ Core (Qt Console)                         │
│                                                                  │
│  ┌─ HttpServer ──────────────────────────────────────────────┐  │
│  │  · QTcpServer  → 提供 HTTP REST API + 静态文件服务          │  │
│  │  · QWebSocketServer → 实时推送状态广播                       │  │
│  └─────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌─ AppController ────────────────────────────────────────────┐  │
│  │  · 雷达业务逻辑 / 工作模式控制                               │  │
│  │  · 通信管理 / 协议编解码 / 数据持久化                       │  │
│  │  · 系统健康监测 / 延时统计                                  │  │
│  └─────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌─ Communication ────────────────────────────────────────────┐  │
│  │  · UDP Endpoint (与雷达 CPU 通信)                           │  │
│  │  · 协议结构体 pack / unpack                                │  │
│  │  · DPH 心跳 50ms 周期                                     │  │
│  └─────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### 核心特性

- **全屏 PPI 雷达显示** — Canvas 实时渲染，距离环 / 十字线 / 角度标签 / 橙色扫描线 / 目标数据点
- **Material Design 3 浅色主题** — 暖白色 Canvas 色系 (#f7f7f4)，橙色强调 (#f54e00)
- **模块化布局** — 标题栏 / 活动栏 / 侧边栏 / 全屏雷达 / 底部多标签面板
- **C++ HTTP+WebSocket 双协议服务** — REST API + 实时推送
- **一键启动** — 双击 `core.exe` 自动启动 HTTP/WS 服务并加载前端页面

---

## 技术栈

| 层 | 技术 | 说明 |
| --- | --- | --- |
| 前端 UI | **React 19** + **TypeScript 5.8** + **Vite 6** | 现代工程化 SPA |
| 样式 | **Tailwind CSS 3** + CSS Variables | Material Design 3 浅色主题 |
| 可视化 | **HTML Canvas** API + requestAnimationFrame | PPI 雷达渲染 (60fps) |
| 通信 | **REST** (QTcpServer) + **WebSocket** (QWebSocketServer) | 状态获取 / 实时广播 |
| 后端 | **Qt 5.14.2** / C++17 / CMake 3.16 | 跨平台 (Windows / Ubuntu) |

---

## 目录结构

```
NewAntiDrone-newArch/
├── CMakeLists.txt             # 顶层 CMake (C++ core 构建)
├── config/                    # 运行时配置 (endpoints / processor / database)
│   ├── endpoints.json
│   ├── processor.json
│   └── database.json
├── core/                      # ★ C++ 后端核心
│   ├── main.cpp               # 入口 + 控制台命令
│   ├── HttpServer.{h,cpp}     # HTTP REST + WebSocket 服务
│   ├── AppController.{h,cpp}  # 装配 & 生命周期
│   ├── comm/                  # 通信层 (UDP Endpoint)
│   ├── protocol/              # 协议层 (雷达协议结构体)
│   ├── bus/                   # 系统总线
│   ├── processor/             # 业务处理
│   └── data/                  # 数据层 (SQLite)
├── ui/                        # ★ React 前端
│   ├── src/
│   │   ├── App.tsx            # 根组件
│   │   ├── main.tsx           # 入口
│   │   ├── components/
│   │   │   ├── layout/        # 布局组件 (TitleBar / RadarView / StatusBar 等)
│   │   │   └── panels/        # 面板组件 (BottomPanel)
│   │   ├── hooks/             # React Hooks (useCoreApi)
│   │   ├── lib/               # 类型定义
│   │   └── styles/            # 全局样式 (CSS Variables)
│   ├── package.json
│   ├── vite.config.ts
│   └── tailwind.config.js
├── test_comm/                 # 协议联调测试客户端
├── doc/                       # 设计文档
│   └── ZL-反无雷达指控协议-260205.docx
└── ide_design.html            # UI 设计原型参考
```

---

## 快速开始

### 1. 构建 C++ 后端

#### Windows (MinGW, Qt 5.14.2)

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
    -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/mingw73_64" `
    -DCMAKE_MAKE_PROGRAM="C:/Qt/Qt5.14.2/Tools/mingw730_64/bin/mingw32-make.exe"

cmake --build build -j 8
```

#### Linux (Ubuntu)

```bash
sudo apt install qtbase5-dev libqt5sql5-sqlite libqt5websockets5-dev cmake build-essential
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### 2. 构建前端

```bash
cd ui
npm install
npm run build
```

### 3. 运行

#### 一键启动

```bash
# Windows: 双击 build/bin/NewAntiDrone_core.exe
# 或命令行：
./build/bin/NewAntiDrone_core
```

后端启动后，在浏览器访问 **http://localhost:8088**

#### 开发模式 (前后端分离)

```bash
# 终端 1: 前端开发服务器 (热更新)
cd ui && npm run dev

# 终端 2: C++ 后端 (HTTP 8088 / WS 8089)
./build/bin/NewAntiDrone_core
```

---

## API 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/` | 前端静态页面 (index.html) |
| `GET` | `/api/status` | 系统状态 (采集/连接/运行时间) |
| `POST` | `/api/start` | 开始采集 (发送工作模式) |
| `POST` | `/api/stop` | 停止采集 (发送待机模式) |
| `WS` | `ws://localhost:8089` | 实时状态推送 (每秒) |

---

## 前端布局

```
┌─ TitleBar (44px, 暖白) ─ [⬡ DCMP] ─── [⚙] [🔔] [●已连接] [开始采集] ─┐
├─ Main (flex-1) ─────────────────────────────────────────────────────────┤
│ ┌Activity┐ ┌Sidebar(196px,悬浮)──┐                                     │
│ │ radar  │ │ 资源管理器          │  ┌─ PPI Canvas (全屏, 60fps) ──────┐ │
│ │ table  │ │ 工程项目            │  │  距离环 / 角度 / 扫描线 / 数据点  │ │
│ │ folder │ │ 设备列表            │  │  雷达信息浮层 / 图例 / 快捷工具   │ │
│ └────────┘ └─────────────────────┘  └───────────────────────────────────┘ │
│ ┌─ BottomPanel (200px) ────────────────────────────────────────────────┐ │
│ │ 目标列表(7) | 日志(45) | 告警(3) | 事件(12)       [清空] [导出] [▼] │ │
│ └───────────────────────────────────────────────────────────────────────┘ │
├─ StatusBar (24px) ─ ●就绪 | DCMP ─── Dist:2.3km Az:… ─── 20:08:18 ─────┤
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 环境要求

| 组件 | 版本 / 说明 |
| --- | --- |
| Qt | 5.14.2 (`core` / `network` / `websockets` / `sql` 模块) |
| 编译器 (Windows) | MinGW 7.3.0 64-bit |
| 编译器 (Linux) | GCC 7+ / Clang 6+ |
| 构建工具 | CMake 3.16+, mingw32-make / make |
| Node.js | 18+ (仅构建前端) |

---

## 许可证

本项目仅供学习和研究使用。
