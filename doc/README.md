# DCMP — 反无人机雷达指控系统

> **D**rone **C**ountermeasure **M**onitoring **P**latform

基于 **Qt 5.14.2 (C++17)** 内核 + **React 19 + Vite** 前端的雷达上位机系统，集成 **高德地图 JSAPI v2** 实现雷达站定位、目标标绘和实时监控。

---

## 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        Browser (React UI)                       │
│  ┌─ Header (glass-panel) ───────────────────────────────────┐  │
│  │  Live Transmission · 开始采集 · 设置                      │  │
│  ├──────────────────────────────────────────────────────────┤  │
│  │ ┌─ Sidebar ─┐ ┌─ Map Area ─────────────────────────────┐ │  │
│  │ │ 资源管理器  │ │                                       │ │  │
│  │ │ 项目文件    │ │   高德地图 Amap JSAPI v2              │ │  │
│  │ │ 设备列表    │ │   · 雷达站定位                        │ │  │
│  │ │ 数据分析    │ │   · 目标标绘                          │ │  │
│  │ │ 历史归档    │ │   · 浮动地图控件                      │ │  │
│  │ │            │ │                                       │ │  │
│  │ │ Structure  │ │                                       │ │  │
│  │ │  ├Globals  │ │                                       │ │  │
│  │ │  └Node_01  │ │                                       │ │  │
│  │ └────────────┘ └───────────────────────────────────────┘ │  │
│  ├──────────────────────────────────────────────────────────┤  │
│  │ Activity Feed (320px)                                    │  │
│  │ Timestamp | System ID | Event Log | Integrity            │  │
│  └──────────────────────────────────────────────────────────┘  │
│                         HTTP REST / WebSocket                   │
│                             8088 / 8089                         │
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

- **高德地图集成** — 基于 Amap JSAPI v2，GCJ-02 坐标系，雷达站标定与目标标绘
- **DCMP 风格 UI** — 玻璃拟态面板 (glass-panel)、墨绿主色调 (#003527)、Material Symbols 图标
- **实时 Activity Feed** — 带过滤器的日志表格，Nominal/Caution/Breach 完整性状态
- **浮动地图控件** — 缩放 / 定位 / 图层控制
- **C++ HTTP+WebSocket 双协议服务** — REST API + 实时推送
- **一键启动** — 双击 `core.exe` 自动启动 HTTP/WS 服务并加载前端页面

---

## 技术栈

| 层 | 技术 | 说明 |
| --- | --- | --- |
| 前端 UI | **React 19** + **TypeScript 5.8** + **Vite 6** | SPA with HMR |
| 样式 | **Tailwind CSS 3** + CSS Variables | DCMP 墨绿主题·玻璃面板 |
| 地图 | **高德地图 JSAPI v2.0** | GCJ-02 坐标系·点标记·缩放控件 |
| 图标 | **Material Symbols Outlined** | Google Fonts CDN |
| 通信 | **REST** (QTcpServer) + **WebSocket** (QWebSocketServer) | 状态获取 / 实时广播 |
| 后端 | **Qt 5.14.2** / C++17 / CMake 3.16 | 跨平台 |

---

## 目录结构

```
newAnti_arch/
├── CMakeLists.txt                 # 顶层 CMake
├── config/                        # 运行时配置
│   ├── endpoints.json             # UDP 端点配置
│   ├── processor.json             # 处理器参数
│   └── database.json              # 数据库配置
├── core/                          # ★ C++ 后端核心
│   ├── main.cpp                   # 入口
│   ├── HttpServer.{h,cpp}         # HTTP REST + WebSocket
│   ├── AppController.{h,cpp}      # 装配 & 生命周期
│   ├── comm/                      # 通信层 (UDP Endpoint)
│   ├── protocol/                  # 协议层 (雷达协议结构体)
│   ├── bus/                       # 系统总线
│   ├── processor/                 # 业务处理
│   └── data/                      # 数据层 (SQLite)
├── ui/                            # ★ React 前端
│   ├── src/
│   │   ├── App.tsx                # 根组件 (DCMP 布局)
│   │   ├── main.tsx               # 入口
│   │   ├── components/
│   │   │   ├── layout/            # 布局组件
│   │   │   │   ├── TitleBar.tsx   # 顶部状态栏
│   │   │   │   ├── Sidebar.tsx    # 资源管理器侧边栏
│   │   │   │   └── StatusBar.tsx  # 底部状态栏
│   │   │   ├── map/
│   │   │   │   └── MapRadarView.tsx  # 高德地图 + 浮动控件
│   │   │   └── panels/
│   │   │       └── BottomPanel.tsx   # Activity Feed 日志面板
│   │   ├── hooks/
│   │   │   └── useCoreApi.tsx     # 后端通信 Context
│   │   ├── types/
│   │   │   └── global.d.ts        # Amap 类型声明
│   │   └── styles/
│   │       └── globals.css        # DCMP 主题 + 全局样式
│   ├── .env                       # 高德地图 Key 配置 (gitignored)
│   ├── package.json
│   ├── vite.config.ts
│   └── tailwind.config.js
├── test_comm/                     # 协议联调测试
├── .vscode/                       # VS Code 编译/调试配置
└── ide_design.html                # UI 设计原型
```

---

## 快速开始

### 1. 构建 C++ 后端

#### Windows (MinGW 7.3.0, Qt 5.14.2)

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

```powershell
# Windows: 双击 build/bin/NewAntiDrone_core.exe
# 或命令行：
./build/bin/NewAntiDrone_core
```

后端启动后自动打开浏览器访问 **http://localhost:8088**

#### 开发模式 (前后端分离)

```bash
# 终端 1: 前端开发服务器 (HMR)
cd ui && npm run dev

# 终端 2: C++ 后端 (HTTP 8088 / WS 8089)
./build/bin/NewAntiDrone_core
```

前端开发服务器会代理 `/api` 到 8088，`/ws` 到 8089。

---

## API 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/` | 前端静态页面 |
| `GET` | `/api/status` | 系统状态 (采集/连接/运行时间) |
| `POST` | `/api/start` | 开始采集 |
| `POST` | `/api/stop` | 停止采集 |
| `WS` | `ws://localhost:8089` | 实时状态推送 |

---

## 高德地图配置

在 `ui/.env` 中配置：

```env
VITE_GAODE_API_KEY=你的Key
VITE_GAODE_SECURITY_CODE=你的安全密钥
```

> Key 需在[高德开放平台](https://console.amap.com/)申请，服务平台选择 **Web端(JS API)**，并配置 `localhost` 白名单。

---

## 环境要求

| 组件 | 版本 / 说明 |
| --- | --- |
| Qt | 5.14.2 (`core` / `gui` / `network` / `websockets` / `sql` 模块) |
| 编译器 (Windows) | MinGW 7.3.0 64-bit |
| 编译器 (Linux) | GCC 7+ / Clang 6+ |
| 构建工具 | CMake 3.16+, mingw32-make / make |
| Node.js | 18+ (仅构建前端) |

---

## 许可证

仅供学习和研究使用。
