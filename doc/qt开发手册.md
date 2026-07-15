# NewAntiDrone VSCode 跨平台开发手册

> 适用于当前 NewAntiDrone 项目，以 VSCode 为主要开发环境。后端使用 C++17、Qt 5.14+ 和 CMake 3.16+；前端使用 React、TypeScript 和 Vite。

## 1. 支持状态

当前代码具备 Windows 和 Linux 的跨平台编译基础：网络、线程、文件、数据库、HTTP 和 WebSocket 均主要使用 Qt；Windows 控制台代码由 `Q_OS_WIN` 隔离。

需要区分以下两个结论：

- **具备跨平台基础**：代码和 CMake 没有发现明确的 Windows 独占编译障碍。
- **尚未完成 Linux 生产验证**：当前仓库没有可证明 Linux 持续构建、运行和部署均通过的记录。

已知跨平台可靠性事项：

1. Unix 信号处理函数目前直接调用 `QCoreApplication::quit()`，不满足严格的异步信号安全要求。
2. core 退出时可能使用 `QThread::terminate()` 终止标准输入线程，属于强制终止，需在后续改为可协作退出。
3. 数据存储当前以 SQLite 为实现基线；建表语句包含 SQLite 方言，不能据此宣称可直接切换 PostgreSQL 等数据库。
4. Web 地图依赖高德地图 JS API，其可用性还取决于浏览器、网络、Key 和安全域名配置，与操作系统无关。

在修复前两项并完成 Linux 构建、运行、退出和压力测试前，不应将项目标记为“Linux 生产就绪”。

## 2. 构建体系

项目的权威构建入口是根目录 `CMakeLists.txt`。仓库中的 `.pro` 文件和旧脚本仅作为历史遗留，不保证包含当前全部源文件和 Qt 模块，**不得作为正式构建与发布依据**。

默认行为：

- C++ 标准：C++17
- 最低 CMake：3.16
- 最低 Qt：5.14
- 单配置生成器未指定类型时：Debug
- `BUILD_TESTS`：默认开启
- 可执行文件输出目录：`build/bin`
- 构建时复制运行配置到：`build/bin/config`
- 构建时创建数据目录：`build/bin/data`

## 3. 开发环境

### 3.1 Windows

推荐安装：

- Qt 5.14.2 MinGW 64-bit（当前项目基线；也可评估兼容的更高 Qt 5 版本）
- 与该 Qt 套件 ABI 匹配的 MinGW
- CMake 3.16+
- Ninja 或 MinGW Makefiles
- Node.js 与 npm
- VSCode
- VSCode 扩展：C/C++、CMake Tools

Qt 和编译器安装路径由开发机决定，不应在文档或工程逻辑中依赖固定盘符。配置时通过 `CMAKE_PREFIX_PATH` 指向 Qt 套件目录。

### 3.2 Ubuntu / Debian

以下命令是 Qt 5 环境的参考安装方式，具体包名以发行版版本为准：

```bash
sudo apt update
sudo apt install \
  build-essential cmake ninja-build gdb \
  qtbase5-dev qtbase5-dev-tools libqt5websockets5-dev libqt5sql5-sqlite
```

前端还需安装受支持的 Node.js LTS 和 npm。`qt5-default` 在较新的 Ubuntu/Debian 中已经废弃，不应继续依赖。

验证环境：

```bash
cmake --version
c++ --version
node --version
npm --version
```

## 4. 当前目录职责

```text
newAnti_arch-main/
├── CMakeLists.txt            # C++ 权威构建入口
├── config/                   # 端点、处理器、数据库运行配置
├── core/
│   ├── business/             # 任务、设备、运行参数、航迹显示业务
│   ├── bus/                  # 系统总线及统一消息封装
│   ├── comm/                 # 通信端点与通信管理
│   ├── data/                 # 配置和 SQLite 数据访问
│   ├── log/                  # 日志基础设施
│   ├── processor/            # 协议处理、路由和周期发布
│   ├── protocol/             # 设备协议结构
│   └── web/                  # HTTP、WebSocket 与 API 路由
├── test_comm/                # 通信与协议测试程序
├── doc/test/src/             # CTest/Qt Test 单元测试
├── ui/                       # React + TypeScript + Vite 前端
└── doc/                      # 架构、业务、测试和开发文档
```

## 5. C++ 构建

### 5.1 Windows MinGW

在已配置 Qt 和 MinGW 环境的 PowerShell 中执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/mingw73_64" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTS=ON
cmake --build build --parallel
```

使用 Ninja 时将生成器替换为 `Ninja`。Release 构建使用 `-DCMAKE_BUILD_TYPE=Release`。

### 5.2 Linux GCC

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTS=ON
cmake --build build --parallel
```

如果 Qt 不在系统默认搜索路径，额外传入：

```bash
-DCMAKE_PREFIX_PATH=/path/to/Qt/5.14.x/gcc_64
```

### 5.3 构建产物

| 程序   | Windows                           | Linux                         |
| ---- | --------------------------------- | ----------------------------- |
| core | `build/bin/NewAntiDrone_core.exe` | `build/bin/NewAntiDrone_core` |
| 通信测试 | `build/bin/test_comm.exe`         | `build/bin/test_comm`         |

多配置生成器（例如 Visual Studio）可能在 `Debug` 或 `Release` 子目录输出，实际位置以 CMake 构建日志为准。

## 6. 前端构建与开发

```bash
cd ui
npm install
npm run build
```

生产前端产物位于 `ui/dist`。core 会从该目录提供静态页面，因此正式运行或联调生产页面前应先完成前端构建。

前端开发模式：

```bash
cd ui
npm run dev
```

- Vite 默认监听 `5173`；端口占用时会自动选择其他端口。
- `/api` 代理到 `http://localhost:8088`。
- `/ws` 代理到 `ws://localhost:8089`。
- core 未启动或正在重启时，代理可能暂时出现 `ECONNREFUSED` 或 `ECONNRESET`；前端应允许后端恢复后重新连接。

## 7. 配置与数据路径

core 查找 `config` 目录的顺序为：

1. 当前工作目录下的 `config`
2. 可执行文件同级目录下的 `config`
3. 可执行文件上一级目录下的 `config`

CMake 会将以下文件复制到 `build/bin/config`：

- `endpoints.json`
- `processor.json`
- `database.json`

数据库中的相对路径以最终选中的配置目录的父目录为基准解析。日志写入可执行文件相邻数据目录中的 `data/logs`。部署或调试时不要仅复制可执行文件而遗漏配置、数据库和 UI 产物。

## 8. 运行

Windows：

```powershell
.\build\bin\NewAntiDrone_core.exe
```

Linux：

```bash
./build/bin/NewAntiDrone_core
```

通信测试程序在另一个终端运行：

```powershell
.\build\bin\test_comm.exe
```

或：

```bash
./build/bin/test_comm
```

core 的 HTTP 服务默认端口为 `8088`，WebSocket 服务默认端口为 `8089`。端口、UDP 地址以及设备协议参数应以当前配置文件和代码定义为准。

## 9. 自动测试

在开启 `BUILD_TESTS` 后执行：

```bash
ctest --test-dir build --output-on-failure
```

多配置生成器需要指定配置，例如：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

测试通过只能证明当前平台和当前配置；跨平台发布前应至少在 Windows 与目标 Linux 发行版分别执行构建、测试和运行验证。

## 10. VSCode 开发流程

VSCode 是当前项目的主要开发环境。仓库当前不依赖已提交的 `.vscode/tasks.json` 或 `.vscode/launch.json`；开发者应通过 CMake Tools 配置本机 Qt 和编译器，避免把个人 Qt 路径或盘符写入共享配置。

### 10.1 推荐扩展

- **C/C++**：代码补全、导航和 GDB 调试。
- **CMake Tools**：配置、构建、测试及选择构建目标。
- **ESLint**：前端规则检查；项目引入 ESLint 配置后启用。

### 10.2 首次配置

1. 用 VSCode 打开项目根目录，不要只打开 `core` 或 `ui` 子目录。
2. 执行 `CMake: Select a Kit`，选择与 Qt 套件 ABI 匹配的编译器。
3. 执行 `CMake: Configure`。
4. 如果 Qt 不在默认搜索路径，在本机 CMake 配置中设置 `CMAKE_PREFIX_PATH`。
5. 状态栏选择 `Debug` 或 `Release`，再执行 `CMake: Build`。

Windows 使用 MinGW Qt 时必须选择对应版本的 MinGW Kit，不能选择 MSVC。Linux 通常选择系统 GCC 或 Clang，并确保安装的 Qt 开发包与编译器兼容。

可在用户级或本地工作区配置中加入：

```jsonc
{
  "cmake.sourceDirectory": "${workspaceFolder}",
  "cmake.buildDirectory": "${workspaceFolder}/build",
  "cmake.configureSettings": {
    "BUILD_TESTS": true
  }
}
```

Windows 的 Qt 路径属于本机配置，例如：

```jsonc
{
  "cmake.configureSettings": {
    "CMAKE_PREFIX_PATH": "C:/Qt/Qt5.14.2/5.14.2/mingw73_64",
    "BUILD_TESTS": true
  }
}
```

这类带绝对路径的设置不应直接提交为团队公共配置。

### 10.3 构建与测试

常用命令面板操作：

| VSCode 命令 | 用途 |
| --- | --- |
| `CMake: Configure` | 生成或刷新构建系统 |
| `CMake: Build` | 构建当前项目 |
| `CMake: Set Build Target` | 选择 core、test_comm 或其他目标 |
| `CMake: Run Tests` | 执行 CTest 测试 |
| `CMake: Delete Cache and Reconfigure` | Kit、Qt 路径或生成器改变后重新配置 |

修改 CMake 文件、切换编译器或更换 Qt 套件后，应重新执行 Configure；普通 C++ 修改只需 Build。

### 10.4 调试 core

CMake Tools 配置成功后：

1. 执行 `CMake: Set Debug Target`。
2. 选择 `NewAntiDrone_core`。
3. 执行 `CMake: Debug`，或使用状态栏调试按钮。

程序路径通常为：

- Windows：`${workspaceFolder}/build/bin/NewAntiDrone_core.exe`
- Linux：`${workspaceFolder}/build/bin/NewAntiDrone_core`

多配置生成器可能增加 `Debug` 或 `Release` 子目录，以 CMake Tools 识别的实际目标为准。启动时应确保工作目录能匹配第 7 节的配置查找规则；推荐将工作目录设为项目根目录或 `build/bin`。

### 10.5 前端开发

在 VSCode 集成终端中执行：

```powershell
cd ui
npm install
npm run dev
```

前端与 core 可分别运行在两个集成终端中。调试 React 页面时可使用浏览器开发者工具；core 断点仍由 CMake Tools/C++ 调试器负责。提交前至少执行：

```powershell
npm run build
```

### 10.6 可选 launch 配置

通常优先使用 CMake Tools 直接调试目标。确需固定启动参数、环境变量或工作目录时，再创建 `.vscode/launch.json`；其中 `program` 应指向 CMake 实际产物，不要引用旧的 `core/debug` 或 qmake 输出目录。

## 11. 编码规则

- 源码、Markdown、CMake 和 JSON 统一使用 UTF-8。
- UTF-8 BOM 不是 C++ 中文字符串可移植性的必要条件，不强制添加。
- 编译器、终端和源文件编码应保持一致；Windows 控制台输出由项目日志实现处理。
- `QStringLiteral()` 适用于编译期 Qt 字符串及减少运行时转换，但不是所有中文字符串都必须使用的跨平台规则。
- JSON 必须保持合法格式，不写注释，不依赖 BOM。
- 代码注释应解释边界、约束和非显然逻辑，不要求为每个声明添加冗余注释。

## 12. 部署

### 12.1 Windows

`core/CMakeLists.txt` 会在找到 `windeployqt` 时自动部署 Qt DLL。若未找到，可在 Release 构建后手动执行：

```powershell
windeployqt .\build\bin\NewAntiDrone_core.exe
```

部署包还应包含运行配置、数据目录需求以及 `ui/dist`。不要依赖开发机 PATH 作为发布方案。

### 12.2 Linux

Linux 可选择安装系统 Qt 运行库、制作发行版软件包，或使用 AppImage 等方式打包。发布前需在目标发行版检查：

- Qt Core、Gui、Network、Sql、WebSockets 插件和动态库
- SQLite Qt 驱动是否可加载
- 运行用户对配置、数据库和日志目录是否有权限
- HTTP、WebSocket 和 UDP 端口是否可用
- 无交互终端、服务管理器停止和异常信号下能否正确退出

## 13. 常见问题

### CMake 找不到 Qt5

确认 Qt 套件与编译器 ABI 匹配，并设置 `CMAKE_PREFIX_PATH`。MinGW Qt 不能与 MSVC 编译器混用。

### Windows 运行时缺少 Qt DLL

运行 `windeployqt`，或确认构建后的自动部署步骤成功。仅把 Qt `bin` 加入 PATH 适合开发，不适合作为正式部署。

### 找不到配置或数据库位置异常

检查启动时的工作目录、可执行文件相对目录及 `build/bin/config`。优先查看日志中最终选中的配置路径。

### UI 无法访问 API 或没有航迹

确认 core 正在运行且 `8088/8089` 可访问；开发模式确认浏览器打开的是 Vite 实际输出的端口；随后检查任务运行上下文、设备端点和 UDP 配置。

### Linux 编译成功但退出异常

这是当前已知风险。应优先修复 Unix 信号转发和标准输入线程的协作退出，再进行服务化部署验证。
