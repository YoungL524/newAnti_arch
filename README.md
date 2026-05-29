# DCMP — 反无人机雷达指控系统

> **D**rone **C**ountermeasure **M**onitoring **P**latform

基于 **Qt 5.14.2 (C++17)** 内核 + **React 19 + Vite** 前端的雷达上位机系统，集成 **高德地图 JSAPI v2** 实现雷达站定位、PPI 多面阵扫描显示、目标标绘和实时监控。

---

## 核心特性

- **PPI 多面阵渲染** — 支持 1~4 面阵雷达，每面阵独立朝向/扫描角/开关，Canvas 实时扫线 + 尾迹
- **品牌协议索引** — 中电38所 / 中电14所 / 雷鹰，品牌字段映射后端数据协议
- **高德地图集成** — Amap JSAPI v2，GCJ-02 坐标系，雷达站标定与目标标绘
- **设备注册表** — 雷达/光学/水平仪/GPS 统一管理，结构树阵面开关控制
- **DCMP 风格 UI** — 玻璃拟态面板 (glass-panel)、墨绿主色调、Material Symbols 图标
- **C++ HTTP + WebSocket 双协议** — REST API 状态获取 + 实时推送
- **一键启动** — 双击 `core.exe` 自动启动服务并加载前端页面

---

## 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        Browser (React UI)                       │
│  ┌─ TitleBar ────────────────────────────────────────────────┐  │
│  │  连接状态 · 开始采集                                        │  │
│  ├────────────────────────────────────────────────────────────┤  │
│  │ ┌─ Sidebar ─┐ ┌─ Map + PPI ─────────────────────────────┐ │  │
│  │ │ 设备列表    │ │  高德地图 Amap JSAPI v2                  │ │  │
│  │ │ 资源管理    │ │  └─ PPI Canvas Overlay                 │ │  │
│  │ │ 数据回放    │ │      · 同心环 + 十字线                   │ │  │
│  │ │ 校准分析    │ │      · 多面阵扇区边界                    │ │  │
│  │ │ 日志归档    │ │      · 同步扫线 + 旋转明暗               │ │  │
│  │ │            │ │      · 尾迹衰减                           │ │  │
│  │ │ Structure  │ │                                         │ │  │
│  │ │  ├全局集群  │ │                                         │ │  │
│  │ │  └NODE_01   │ │                                         │ │  │
│  │ │    ├阵面A ◉ │ │                                         │ │  │
│  │ │    ├阵面B ○ │ │                                         │ │  │
│  │ │    ├阵面C ◉ │ │                                         │ │  │
│  │ │    └阵面D ◉ │ │                                         │ │  │
│  │ └────────────┘ └─────────────────────────────────────────┘ │  │
│  ├────────────────────────────────────────────────────────────┤  │
│  │ BottomPanel — Activity Feed / Device Table                  │  │
│  └────────────────────────────────────────────────────────────┘  │
│                        HTTP REST / WebSocket                     │
│                            8088 / 8089                            │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────────┐
│                    C++ Core (Qt Console)                         │
│                                                                  │
│  ┌─ HttpServer ──────────────────────────────────────────────┐  │
│  │  QTcpServer → HTTP REST API + 静态文件服务                  │  │
│  │  QWebSocketServer → 实时状态广播                            │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌─ AppController ───────────────────────────────────────────┐  │
│  │  雷达业务逻辑 / 工作模式控制 / 通信管理 / 协议编解码         │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌─ Communication ───────────────────────────────────────────┐  │
│  │  UDP Endpoint · DPH 心跳 50ms · 协议 pack/unpack           │  │
│  └───────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 技术栈

| 层 | 技术 | 说明 |
| --- | --- | --- |
| 前端 UI | **React 19** + **TypeScript 5.8** + **Vite 6** | SPA with HMR |
| 样式 | **Tailwind CSS 3** + CSS Variables | DCMP 墨绿主题·玻璃面板 |
| 地图 | **高德地图 JSAPI v2.0** | GCJ-02 坐标系 |
| 图标 | **Material Symbols Outlined** | Google Fonts CDN |
| PPI 渲染 | **Canvas 2D** + requestAnimationFrame | 多面阵扫线·尾迹·扇区 |
| 通信 | **REST** (QTcpServer) + **WebSocket** (QWebSocketServer) | 状态获取 / 实时广播 |
| 后端 | **Qt 5.14.2** / C++17 / CMake 3.16 | 跨平台 |

---

## 数据模型

### RadarDevice

```typescript
interface RadarFace {
  label: string               // 阵面标识: "A" / "B" / "C" / "D"
  boresightAzimuth: number    // 阵面朝向 (°), 0=North, CW
  scanAngle: number           // 扫描半角 (°), e.g. 45 for ±45°
  active: boolean             // 是否开启扫描
}

type RadarBrand = 'cetc38' | 'cetc14' | 'leiying'

interface RadarDevice {
  id: string
  name: string
  brand: RadarBrand
  lng: number; lat: number; altitude: number
  maxRange: number            // km
  faces: RadarFace[]          // 阵面列表
}
```

### SubDevice

```typescript
interface SubDevice {
  id: string
  name: string
  kind: 'opto' | 'level' | 'gps'
  brand: RadarBrand
  linkedRadarId?: string
  mountOffsetAzimuth?: number
  coordinateSystem?: string
}
```

---

## 目录结构

```
newAnti_arch/
├── CMakeLists.txt                 # 顶层 CMake
├── CLAUDE.md                      # AI 辅助开发指引
├── config/                        # 运行时配置
│   ├── endpoints.json
│   ├── processor.json
│   └── database.json
├── core/                          # C++ 后端核心
│   ├── main.cpp
│   ├── HttpServer.{h,cpp}
│   ├── AppController.{h,cpp}
│   ├── comm/                      # 通信层 (UDP)
│   ├── protocol/                  # 协议层
│   ├── bus/                       # 系统总线
│   ├── processor/                 # 业务处理
│   └── data/                      # 数据层 (SQLite)
├── ui/                            # React 前端
│   ├── src/
│   │   ├── App.tsx
│   │   ├── main.tsx
│   │   ├── components/
│   │   │   ├── layout/            # TitleBar, Sidebar, StatusBar
│   │   │   ├── map/               # MapRadarView, PPIOverlay, types
│   │   │   ├── panels/            # BottomPanel, DataTable
│   │   │   └── dialogs/           # DeviceEditDialog, RadarEditForm, SubDeviceEditForm
│   │   ├── hooks/                 # useCoreApi, useTrackData
│   │   ├── lib/                   # protocol, columns
│   │   └── styles/                # globals.css
│   ├── package.json
│   ├── vite.config.ts
│   └── tailwind.config.js
├── test_comm/                     # 协议联调测试
├── doc/                           # 设计文档
└── .vscode/                       # VS Code 配置
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

```powershell
./build/bin/NewAntiDrone_core
```

后端启动后自动打开浏览器访问 **http://localhost:8088**

开发模式：

```bash
# 终端 1: 前端 HMR
cd ui && npm run dev

# 终端 2: C++ 后端
./build/bin/NewAntiDrone_core
```

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
| Qt | 5.14.2 (core / gui / network / websockets / sql) |
| 编译器 (Windows) | MinGW 7.3.0 64-bit |
| 编译器 (Linux) | GCC 7+ / Clang 6+ |
| 构建工具 | CMake 3.16+ |
| Node.js | 18+ (仅构建前端) |

---

## 许可证

仅供学习和研究使用。
