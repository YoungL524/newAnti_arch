# NewAntiDrone (newArch)

反无人机雷达指控系统 —— 新架构版本。基于 **Qt 5.14.2 (C++17)** 的控制台应用，按"通信 / 协议 / 总线 / 处理 / 数据"五层解耦设计，便于在嵌入式与桌面侧统一维护。

> 上位/指控端（本工程）↔ 雷达 CPU 端，通过 UDP 收发雷达指控协议（详见 `ZL-反无雷达指控协议-260205.docx`）。

---

## 目录结构

```
NewAntiDrone-newArch/
├── CMakeLists.txt            # 顶层 CMake（推荐）
├── NewAntiDrone.pro          # 顶层 qmake 工程（兼容保留）
├── build.bat                 # Windows 一键 qmake 构建并运行 core
├── config/                   # 运行时配置（被 core 加载）
│   ├── endpoints.json        # 通信端点：本地端口 / 对端目标
│   ├── processor.json        # 路由 / DPH 周期发布 / 默认下行
│   └── database.json         # SQLite 配置
├── core/                     # ★ 主进程：NewAntiDrone_core
│   ├── core.pro
│   ├── main.cpp              # 入口 + 控制台命令(work/standby/help/quit)
│   ├── AppController.{h,cpp} # 装配 & 生命周期
│   ├── StdinReader.h         # 独立线程读 stdin，不阻塞 Qt 事件循环
│   ├── comm/                 # 通信层：Endpoint 抽象 + UdpEndpoint + 管理器
│   ├── protocol/             # 协议层：StructureSend / StructureReceive
│   ├── bus/                  # 系统总线：ISystemBus + ThreadSafeQueue + GenericPackets
│   ├── processor/            # 业务处理：RadarDataProcessor / BusToCommRouter / DphPublisher
│   └── data/                 # 数据层：ConfigManager / DataStore (SQLite)
├── test_comm/                # 协议联调客户端（模拟雷达端）
│   ├── test_comm.pro
│   ├── main.cpp
│   ├── TestComm.{h,cpp}
│   └── StdinReader.h
├── data/                     # 运行期数据（SQLite 等，已 .gitignore）
├── doc/                      # 设计文档
│   ├── 架构设计-做减法版.md
│   └── core-数据流图.md
├── 系统架构.png
└── ZL-反无雷达指控协议-260205.docx
```

---

## 架构概览

```
                ┌──────────── AppController（装配 / 生命周期） ────────────┐
                │                                                          │
   data 层      │  ConfigManager  ◄── JSON ──►  endpoints/processor/db    │
                │  DataStore      ◄── SQLite ─►  data/antidrone.db        │
                │                                                          │
   bus 层       │  ISystemBus / SystemBus  +  ThreadSafeQueue              │
                │  GenericPackets（统一业务包格式，发布订阅）              │
                │                                                          │
  processor 层  │  RadarDataProcessor   BusToCommRouter   DphPublisher     │
                │  （报文 ↔ 业务包翻译；上行入总线，下行出网络）           │
                │                                                          │
   comm 层      │  CommunicationManager → Endpoint(抽象) → UdpEndpoint     │
                │  EndpointConfigLoader（按 JSON 注册端点 / 目标）         │
                │                                                          │
  protocol 层   │  StructureSend / StructureReceive（雷达协议结构体）      │
                └──────────────────────────────────────────────────────────┘
```

数据流（典型）：

- **上行**：`UdpEndpoint` 收包 → `RadarDataProcessor` 解析 → `SystemBus` 发布 → 订阅者（路由 / 持久化等）。
- **下行**：业务模块向 `SystemBus` 投递 → `BusToCommRouter` 按 `processor.json` 选择端点/目标 → `UdpEndpoint` 发送。
- **周期**：`DphPublisher` 每 50ms 向雷达端发送 DPH 心跳（见 `processor.json` → `dphPublisher`）。

---

## 环境要求

| 组件 | 版本 / 说明 |
| --- | --- |
| Qt | 5.14.2（`core`/`network`/`sql` 模块） |
| 编译器（Windows） | MinGW 7.3.0 64-bit（Qt 自带的 `mingw73_64`） |
| 编译器（Linux） | GCC 7+ / Clang 6+ |
| 构建工具 | qmake + mingw32-make / make |

> Linux：`sudo apt install qtbase5-dev libqt5sql5-sqlite qt5-qmake build-essential` 即可。

---

## 构建与运行

### CMake（推荐，Windows / Linux 通用）

#### Windows（MinGW，Qt 5.14.2 自带工具链）

```powershell
# 配置（首次）
cmake -S . -B build -G "MinGW Makefiles" `
    -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/mingw73_64" `
    -DCMAKE_MAKE_PROGRAM="C:/Qt/Qt5.14.2/Tools/mingw730_64/bin/mingw32-make.exe"

# 编译
cmake --build build -j 8

# 运行（产物与 config 已自动 copy 到 build/bin/）
.\build\bin\NewAntiDrone_core.exe
.\build\bin\test_comm.exe
```

如果用 MSVC 工具链，把 `-G "MinGW Makefiles"` 换成 `-G "Visual Studio 17 2022" -A x64`，并把 `CMAKE_PREFIX_PATH` 指向 `C:/Qt/Qt5.14.2/5.14.2/msvc2017_64`（视你的安装而定）。

#### Linux（Ubuntu）

```bash
sudo apt install qtbase5-dev libqt5sql5-sqlite cmake build-essential

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

./build/bin/NewAntiDrone_core
./build/bin/test_comm
```

CMake 会把 `config/*.json` 拷贝到 `build/bin/config/`，并预建 `build/bin/data/`（SQLite 落地目录），产物自包含可直接运行。

### VSCode 推荐工作流

仓库自带 `.vscode/` 共享配置（`settings.json` / `tasks.json` / `launch.json` / `extensions.json`）：

- 首次打开会提示安装推荐扩展（`ms-vscode.cpptools` / `ms-vscode.cmake-tools` / `tonka3000.qtvsctools` 等）。
- CMake Tools 会自动 configure 并接管 IntelliSense（无需手写 `c_cpp_properties.json`）。
- **F7** 默认编译 `build-all`；**F5** 调试 `Debug Core (gdb)`；侧边栏可切到 `Debug TestComm`。
- Tasks：`cmake-configure` / `build-all` / `build-core` / `build-test_comm` / `clean` / `wipe-build`。

### qmake（兼容保留）

#### Windows 一键脚本

1. 把 Qt 的 mingw 工具链加进 PATH，或在 `build.bat` 顶部追加：
   ```bat
   set PATH=C:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin;C:\Qt\Qt5.14.2\Tools\mingw730_64\bin;%PATH%
   ```
2. 双击或在 PowerShell / CMD 中执行 `build.bat`：进入 `core/`，必要时 `qmake core.pro`，然后 `mingw32-make -j8` 构建并启动 `debug\NewAntiDrone_core.exe`。

#### 手动 qmake

```bash
qmake NewAntiDrone.pro
mingw32-make -j8     # Windows
# 或
make -j8             # Linux

cd core/debug && ./NewAntiDrone_core
cd test_comm/debug && ./test_comm
```

### 运行控制台命令

启动 `NewAntiDrone_core` 后输入：

| 命令 | 作用 |
| --- | --- |
| `1` 或 `work` | 发送工作模式（`workStandby_Mode = 1`），雷达开始上报航迹 |
| `2` 或 `standby` | 发送待机模式（`workStandby_Mode = 0`），雷达停止上报 |
| `h` 或 `help` | 显示帮助 |
| `q` 或 `quit` | 退出程序 |

`core` 启动后会持续每 50 ms 向雷达端发送 DPH（见 `processor.json`）。

---

## 配置文件

`ConfigManager` 按以下顺序定位 `config/` 子目录（首个命中即用）：

1. `<工作目录>/config/`
2. `<exe 所在目录>/config/`
3. `<exe 所在目录>/../config/`

CMake 构建后 `build/bin/config/` 已就位；从工程根目录直接跑也能命中根 `config/`。

### `config/endpoints.json`

通信端点定义。`id` 在 `processor.json` 中被引用。

```json
{
  "endpoints": [
    {
      "id": "radar_cpu",
      "protocol": "udp",
      "localAddress": "127.0.0.1",
      "localPort": 7788,
      "targets": {
        "dph":      { "address": "127.0.0.1", "port": 3456 },
        "workmode": { "address": "127.0.0.1", "port": 1234 }
      },
      "description": "雷达CPU通信"
    }
  ]
}
```

### `config/processor.json`

业务处理器配置：上行源、下行路由、DPH 周期发布。

```json
{
  "endpointSources":   [ { "endpointId": "radar_cpu", "sourceType": 1 } ],
  "reverseTargetDevice": 1,
  "defaultRoute":      { "endpointId": "radar_cpu", "targetName": "workmode" },
  "dphPublisher":      { "enabled": true, "intervalMs": 50,
                         "endpointId": "radar_cpu", "targetName": "dph" },
  "routes": [
    { "payloadType": 1, "endpointId": "radar_cpu", "targetName": "workmode" },
    { "payloadType": 2, "endpointId": "radar_cpu", "targetName": "workmode" },
    { "payloadType": 3, "endpointId": "radar_cpu", "targetName": "workmode" }
  ]
}
```

### `config/database.json`

```json
{ "driver": "QSQLITE", "database": "data/antidrone.db" }
```

> 数据库文件本身不入仓库（已加入 `.gitignore`）。建表脚本见仓库根的 `-- SQLite.sql`。

---

## 联调（test_comm ↔ core）

`test_comm` 复用 `core/protocol` 的报文头，作为模拟雷达端跑在同一台机器上：

1. 修改双方 `endpoints.json` 中的端口/地址使其互通（默认 `127.0.0.1` 即可）。
2. 先启动 `test_comm`（监听上行），再启动 `core`。
3. 在 `core` 控制台输入 `1`（work）后，`test_comm` 应开始收到 DPH 与工作模式指令；`test_comm` 也可推送模拟航迹给 `core` 验证总线发布链路。

---

## 路线图

- [x] 迁移 qmake → **CMake**（统一 Windows / Ubuntu，去除 Qt 路径硬编码）。
- [ ] 协议结构体 pack/unpack 的单元测试（端到端字节级对照）。
- [ ] `SystemBus` 容量上限与背压策略，防止慢消费者撑大队列。
- [ ] 日志门面（等级 / 文件落盘 / 现场排错）。
- [ ] 处理 Windows-only 路径（`StdinReader` / 控制台编码），与 Linux 对齐。

---

## 仓库约定

- `Makefile` / `Makefile.*` / `*.pro.user*` / `*.qmake.stash` / `build*/` / `debug/` / `release/` / `*.db` 等构建与本地产物均不入库（见 `.gitignore`）。
- `.vscode/` 作为团队共享配置入库；个人 `*.code-workspace` 不入库。
- 首次拉取后：CMake 用户 `cmake -S . -B build ...`；qmake 用户自行 `qmake` 生成本地 Makefile，**切勿提交生成的 Makefile**。