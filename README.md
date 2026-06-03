# DCMP — 反无人机雷达指控系统

> **D**rone **C**ountermeasure **M**onitoring **P**latform

基于 **Qt 5.14.2 (C++17)** 内核 + **React 19 + Vite** 前端的雷达上位机系统。PPI 多面阵扫描、实时航迹标绘、设备注册管理。

---

## 核心特性

- **设备级独立控制** — 多选框勾选雷达，每设备独立状态机，广播命令 + 单机反馈
- **PPI 多面阵渲染** — 1~4 面阵雷达，独立朝向/扫描角/开关，Canvas 同步扫线 + 旋转明暗
- **实时航迹显示** — Canvas TrackLayer，极简拖尾 + 渐变圆点，50ms→1s 节流渲染
- **端到端数据管道** — `test_comm → UDP → core → SystemBus → WebSocket → 前端`
- **状态监控** — 设备列表彩色状态灯 (灰=待机/绿=工作/红=故障)，PPI 中心点跟随设备状态
- **品牌协议索引** — 中电38所 / 中电14所 / 雷鹰
- **任务系统** — 新建任务/设备筛选/日志归档，任务生命周期管理
- **DCMP 风格 UI** — 玻璃拟态面板、墨绿主色调、Material Symbols 图标

---

## 数据流架构

```
┌── 前端 React ──────────────────────────────────────────────────┐
│  TitleBar · Sidebar · MapRadarView(Amap+PPI+TrackLayer) · StatusBar  │
│       │  POST /api/start|stop          WebSocket :8089  ▲      │
└───────┼────────────────────────────────────────────────┼──────┘
        │                                                │
┌───────▼────────── C++ Core ────────────────────────────┼──────┐
│  HttpServer (HTTP :8088 / WS :8089)                    │      │
│       │  subscribeToBus()                              │      │
│  ┌────▼────────────────────────────────────────────┐   │      │
│  │              SystemBus (DDS pub/sub)              │   │      │
│  │  RadarDataProcessor ──→ BusToCommRouter           │   │      │
│  │       ↑                        ↓                 │   │      │
│  │  CommManager (UDP :7788)      UDP :1234          │   │      │
│  └───────┼────────────────────────┼─────────────────┘   │      │
│          │                        │                     │      │
│  RadarWorkModeController    DphPublisher (50ms)         │      │
│  AppController · DataStore(SQLite) · Logger             │      │
└──────────┼────────────────────────┼─────────────────────┘      │
           │ track data             │ workStandby cmd            │
┌──────────▼────────────────────────┼────────────────────────────┐
│  test_comm (雷达模拟器)             │                            │
│  · 接收 DPH (:3456)               │                            │
│  · 接收 workStandby (:1234) ──────┘                            │
│  · CSV 回放 → UDP → core :7788                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 技术栈

| 层 | 技术 | 说明 |
| --- | --- | --- |
| 前端 UI | React 19 + TypeScript 5.8 + Vite 6 | SPA with HMR |
| 样式 | Tailwind CSS 3 + CSS Variables | DCMP 墨绿主题·玻璃面板 |
| 地图 | 高德地图 JSAPI v2.0 | GCJ-02 坐标系 |
| PPI | Canvas 2D + requestAnimationFrame | 多面阵扫线·尾迹·扇区 |
| 航迹 | Canvas 2D TrackLayer | 彗星拖尾·1s 节流 |
| 通信 | REST (QTcpServer) + WebSocket | :8088 / :8089 |
| 后端 | Qt 5.14.2 / C++17 / CMake 3.16 | 跨平台 |

---

## 数据模型

```typescript
interface RadarFace {
  label: string               // "A" / "B" / "C" / "D"
  boresightAzimuth: number    // 阵面朝向 (°)
  scanAngle: number           // 扫描半角 (°)
  active: boolean             // 是否开启扫描
}

type RadarBrand = 'cetc38' | 'cetc14' | 'leiying'

interface RadarDevice {
  id: string; name: string; brand: RadarBrand
  lng: number; lat: number; altitude: number; maxRange: number
  faces: RadarFace[]
}
```

---

## 目录结构

```
newAnti_arch/
├── CMakeLists.txt
├── CLAUDE.md
├── config/                          # 运行时配置
│   ├── endpoints.json               # UDP 端点 (core :7788)
│   ├── processor.json               # 处理器流水线
│   └── database.json                # SQLite 配置
├── core/                            # C++ 后端
│   ├── main.cpp                     # 入口: HttpServer + StdinReader
│   ├── HttpServer.{h,cpp}           # HTTP/WS + subscribeToBus
│   ├── AppController.{h,cpp}        # 装配 & 生命周期
│   ├── RadarWorkModeController      # 工作/待机状态机
│   ├── comm/                        # UDP Endpoint + CommManager
│   ├── protocol/                    # 雷达协议结构体 (Send/Receive)
│   ├── bus/                         # SystemBus (DDS pub/sub)
│   ├── processor/                   # 数据流管道 (+ LatencyStats)
│   ├── data/                        # DataStore (SQLite) + ConfigManager
│   ├── business/                    # 工作模式控制
│   └── log/                         # Logger (日滚动 + 7天清理)
├── ui/                              # React 前端
│   └── src/
│       ├── App.tsx                  # 根组件 + 任务/设备状态管理
│       ├── components/
│       │   ├── layout/              # TitleBar, Sidebar, StatusBar
│       │   ├── map/                 # MapRadarView, PPIOverlay, TrackLayer, types
│       │   ├── panels/              # BottomPanel, DataTable
│       │   └── dialogs/             # DeviceEditDialog, TaskCreateDialog, ...
│       ├── hooks/                   # useCoreApi (WS+节流), useTrackData
│       └── lib/                     # protocol, taskTypes, columns
├── test_comm/                       # 雷达模拟器 + CSV 回放
│   ├── target_data-4.csv            # 真实航迹数据
│   ├── TestComm.{h,cpp}             # CSV 加载 + UDP 回放
│   └── main.cpp                     # 命令行入口
├── doc/                             # 设计文档
└── .vscode/
```

---

## 快速开始

### 构建

```powershell
# C++ 后端 + test_comm
cmake -S . -B build -G "MinGW Makefiles" `
    -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/mingw73_64" `
    -DCMAKE_MAKE_PROGRAM="C:/Qt/Qt5.14.2/Tools/mingw730_64/bin/mingw32-make.exe"
cmake --build build -j 8

# 前端
cd ui && npm install && npm run build
```

### 运行（完整链路测试）

```powershell
# 终端 1: core
.\build\bin\NewAntiDrone_core.exe

# 终端 2: test_comm (指定 CSV 路径)
.\build\bin\test_comm.exe 127.0.0.1 7788 1234 3456 ..\..\test_comm\target_data-4.csv

# 浏览器: http://localhost:8088
# 前端 新建任务 → 添加雷达 → 开始采集 → 查看航迹
```

### 开发模式

```bash
cd ui && npm run dev      # 终端 1: 前端 HMR (:5173)
./build/bin/NewAntiDrone_core   # 终端 2: 后端 (:8088)
```

---

## API

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/` | 前端静态页面 |
| `GET` | `/api/status` | `{ acquisition, state, connected }` |
| `POST` | `/api/start` | 开始采集 (Work_Mode=1) |
| `POST` | `/api/stop` | 停止采集 (Work_Mode=0) |
| `WS` | `:8089` | `type: status` (1s) + `type: tracks` (实时) |

### WebSocket 消息格式

```typescript
// 状态推送
{ type: "status", acquisition: boolean, state: string, connected: boolean }

// 航迹推送
{ type: "tracks", deviceType: number,
  track: { targetId, range, azimuth, elevation, x, y, z, ... } }
```

---

## 环境要求

| 组件 | 版本 |
| --- | --- |
| Qt | 5.14.2 (core / gui / network / websockets / sql) |
| MinGW | 7.3.0 64-bit (Windows) |
| CMake | 3.16+ |
| Node.js | 18+ |

## 许可证

仅供学习和研究使用。
