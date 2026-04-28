# VSCode + Qt 开发手册

> 本手册适用于 NewAntiDrone-newArch 项目，基于 Qt 5.14.2 + qmake 构建体系。
> 覆盖 Windows (MinGW) 和 Linux (GCC) 两个平台。

---

## 1. 环境准备

### 1.1 Windows

**必需软件：**

| 软件 | 版本 | 用途 |
|------|------|------|
| Qt | 5.14.2 MinGW 73_64 | Qt 库 + qmake |
| MinGW | 7.3.0 64-bit | C++ 编译器 |
| VSCode / Windsurf | 最新 | 代码编辑器 |

**推荐 Qt 安装路径：** `D:\qt`（安装时勾选 MinGW 7.3.0 64-bit 和对应的 Qt 5.14.2 套件）

**VSCode 必装扩展：**
- **C/C++** (Microsoft) — IntelliSense + 调试
- **C/C++ Extension Pack** — 附带 CMake 等工具（可选）

**环境变量（可选）：** 如果希望在任意终端使用 qmake/make，将以下路径加入系统 PATH：
```
D:\qt\5.14.2\mingw73_64\bin
D:\qt\Tools\mingw730_64\bin
```

> 项目的 `.vscode/tasks.json` 已内置完整 PATH，不加系统变量也能在 VSCode 内正常编译。

### 1.2 Linux (Ubuntu / Lubuntu)

```bash
# 安装 Qt 开发包和构建工具
sudo apt install qt5-default qtbase5-dev build-essential gdb

# 验证
qmake --version
g++ --version
```

**VSCode 扩展：** 同 Windows，安装 C/C++ 扩展即可。

---

## 2. 项目结构

```
NewAntiDrone-newArch/
├── .vscode/                  # VSCode 配置（tasks, launch, settings）
├── config/
│   └── endpoints.json        # 端点配置（运行时读取）
├── core/                     # 核心程序（NewAntiDrone_core.exe）
│   ├── core.pro
│   ├── main.cpp
│   ├── AppController.h/cpp   # 总控制器
│   ├── comm/                 # 通信层（UDP 端点、通信管理器）
│   ├── protocol/             # 协议结构体（发送/接收）
│   ├── bus/                  # 系统总线（线程安全队列）
│   └── processor/            # 数据处理线程
├── test_comm/                # 测试工具（test_comm.exe）
│   ├── test_comm.pro
│   ├── main.cpp
│   ├── TestComm.h/cpp        # 测试业务逻辑
│   └── StdinReader.h         # stdin 异步读取线程
├── doc/                      # 设计文档
├── vscode/                   # 本手册等参考资料
├── NewAntiDrone.pro          # 顶层 subdirs 项目文件
└── build.bat                 # Windows 一键编译脚本
```

---

## 3. 编译

### 3.1 VSCode 内编译（推荐）

按 **Ctrl+Shift+B** 触发默认构建任务 `build-all`，自动执行：
1. `qmake NewAntiDrone.pro CONFIG+=debug CONFIG+=console`（生成 Makefile）
2. `mingw32-make -j8`（并行编译 core + test_comm）

**可选构建任务（Ctrl+Shift+B 后选择）：**

| 任务名 | 说明 |
|--------|------|
| `build-all` | 编译全部（默认） |
| `build-core` | 仅编译 core |
| `build-test_comm` | 仅编译 test_comm |
| `clean-all` | 清理全部编译产物 |
| `rebuild-all` | clean + build |

### 3.2 终端手动编译

**Windows (cmd):**
```cmd
cd /d E:\AFSIM\NewAntiDrone-newArch
D:\qt\5.14.2\mingw73_64\bin\qmake.exe NewAntiDrone.pro CONFIG+=debug CONFIG+=console
D:\qt\Tools\mingw730_64\bin\mingw32-make.exe -j8
```

**Linux:**
```bash
cd ~/NewAntiDrone-newArch
qmake NewAntiDrone.pro CONFIG+=debug
make -j$(nproc)
```

### 3.3 编译产物位置

| 平台 | core | test_comm |
|------|------|-----------|
| Windows Debug | `core/debug/NewAntiDrone_core.exe` | `test_comm/debug/test_comm.exe` |
| Windows Release | `core/release/NewAntiDrone_core.exe` | `test_comm/release/test_comm.exe` |
| Linux | `core/NewAntiDrone_core` | `test_comm/test_comm` |

---

## 4. 运行与调试

### 4.1 VSCode 运行配置

点击左侧 **Run and Debug**（Ctrl+Shift+D），选择配置：

| 配置名 | 说明 |
|--------|------|
| **Debug Core** | 调试 core（可设断点） |
| **Run Core (no debug)** | 直接运行 core |
| **Debug TestComm** | 调试 test_comm |
| **Run TestComm (no debug)** | 直接运行 test_comm |

> 所有配置均设置为 `externalConsole: true`，会弹出独立 cmd 窗口运行。
> 这样中文显示正常，且两个程序可以**同时运行**互不干扰。

### 4.2 同时运行两个程序

1. 选择 **Debug Core** 或 **Run Core**，按 F5 启动
2. 不要停止，切换到 **Debug TestComm** 或 **Run TestComm**，再按 F5
3. 两个程序各自在独立窗口运行，互相通信

### 4.3 运行时配置文件

程序启动时会读取 `config/endpoints.json`，**工作目录**（cwd）设为项目根目录，所以路径 `config/endpoints.json` 能被正确找到。

如果提示找不到配置文件，检查 launch.json 中的 `cwd` 是否设为 `${workspaceFolder}`。

### 4.4 Linux 运行

```bash
cd ~/NewAntiDrone-newArch

# 运行 core
./core/NewAntiDrone_core

# 另开终端运行 test_comm
./test_comm/test_comm
```

---

## 5. Linux 下的 VSCode 配置

项目自带的 `.vscode/` 配置是 Windows 专用路径。Linux 下需要调整：

### 5.1 settings.json 差异

```jsonc
{
  // Linux 不需要 utf8bom，系统默认就是 UTF-8
  "files.encoding": "utf8",

  "C_Cpp.default.compilerPath": "/usr/bin/g++",
  "C_Cpp.default.intelliSenseMode": "gcc-x64",
  "C_Cpp.default.cppStandard": "c++17",
  "C_Cpp.default.includePath": [
    "/usr/include/x86_64-linux-gnu/qt5",
    "/usr/include/x86_64-linux-gnu/qt5/QtCore",
    "/usr/include/x86_64-linux-gnu/qt5/QtNetwork",
    "${workspaceFolder}/core",
    "${workspaceFolder}/core/comm",
    "${workspaceFolder}/core/bus",
    "${workspaceFolder}/core/processor",
    "${workspaceFolder}/core/protocol"
  ]
}
```

### 5.2 tasks.json 差异

```jsonc
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "qmake-all",
      "type": "shell",
      "command": "qmake",
      "args": ["NewAntiDrone.pro", "CONFIG+=debug"],
      "options": { "cwd": "${workspaceFolder}" },
      "problemMatcher": []
    },
    {
      "label": "build-all",
      "type": "shell",
      "command": "make",
      "args": ["-j$(nproc)"],
      "options": { "cwd": "${workspaceFolder}" },
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": ["$gcc"],
      "dependsOn": "qmake-all"
    }
  ]
}
```

### 5.3 launch.json 差异

```jsonc
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug Core",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/core/NewAntiDrone_core",
      "args": [],
      "cwd": "${workspaceFolder}",
      "externalConsole": true,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "preLaunchTask": "build-all"
    },
    {
      "name": "Debug TestComm",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/test_comm/test_comm",
      "args": [],
      "cwd": "${workspaceFolder}",
      "externalConsole": true,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "preLaunchTask": "build-all"
    }
  ]
}
```

> **提示：** 可以将 Windows 和 Linux 的 launch 配置都放在同一个 launch.json 里，
> 分别命名为 "Debug Core (Win)" 和 "Debug Core (Linux)"，按需选择即可。

---

## 6. 编码规范与注意事项

### 6.1 文件编码（极其重要）

| 文件类型 | 编码要求 | 原因 |
|----------|----------|------|
| `.h`, `.cpp`, `.md` | **UTF-8 with BOM** | MinGW 需要 BOM 才能正确识别 UTF-8 中文字符串 |
| `.pro`, `.json` | **UTF-8 without BOM** | qmake 不支持 BOM，JSON 规范不推荐 BOM |

**VSCode 设置已配置 `"files.encoding": "utf8bom"`**，新建/编辑 `.h`/`.cpp` 文件时会自动带 BOM。

**注意：** 编辑 `.pro` 文件后，需检查是否被自动加了 BOM。如果 qmake 报 `Unexpected UTF-8 BOM`，说明 `.pro` 文件被加了 BOM，需手动移除。

**批量检查 BOM 状态的 PowerShell 脚本：**
```powershell
Get-ChildItem -Recurse -File -Include *.h,*.cpp,*.pro,*.json |
  Where-Object { $_.FullName -notmatch 'debug|\.git' } |
  ForEach-Object {
    $b = [IO.File]::ReadAllBytes($_.FullName)
    $bom = ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)
    "$([int]$bom) $($_.Name)"
  }
```

### 6.2 中文字符串

所有中文字符串字面量**必须使用 `QStringLiteral()` 包装**：

```cpp
// 正确
qDebug() << QStringLiteral("系统启动完成");

// 错误 - 可能乱码
qDebug() << "系统启动完成";
```

### 6.3 控制台中文输出

项目已在 `main.cpp` 中实现了 `consoleMessageHandler`：
- **Windows cmd / PowerShell / VSCode Terminal：** 使用 `WriteConsoleW` 直接输出 Unicode
- **Linux 终端：** 使用 `fwrite` + UTF-8（Linux 终端默认 UTF-8，无需特殊处理）
- **Qt Creator 应用程序输出面板：** 走 `fwrite` + UTF-8 回退路径（需设置 Run in Terminal）

### 6.4 命名规范

- **所有 .exe / TARGET 名：** 有后缀统一使用**下划线 `_`**，禁止使用连字符 `-`
  - `NewAntiDrone_core`（不是 `NewAntiDrone-core`）
  - `test_comm`（不是 `test-comm`）
- **源文件目录名：** 使用下划线（如 `test_comm/`）
- **C++ 类名：** 大驼峰（如 `AppController`, `TestComm`）
- **代码注释：** 所有代码必须有详细的中文 Doxygen 注释

---

## 7. 常见问题

### Q: Ctrl+Shift+B 编译报 "参数格式不正确 - /d"
**A:** 终端配置冲突。检查 settings.json 中是否有 `cmd.exe /k chcp 65001` 的终端配置，改用 PowerShell 作为默认终端。

### Q: qmake 报 "Unexpected UTF-8 BOM"
**A:** `.pro` 文件被加了 BOM。用 PowerShell 移除前 3 字节：
```powershell
$f='path/to/file.pro'; $b=[IO.File]::ReadAllBytes($f); if($b[0]-eq 0xEF){[IO.File]::WriteAllBytes($f,$b[3..($b.Length-1)])}
```

### Q: 编译成功但运行报 "找不到 Qt5Core.dll"
**A:** Qt DLL 不在 PATH 中。launch.json 的 environment 已配置 Qt bin 路径。如果直接双击 exe 运行，需将 `D:\qt\5.14.2\mingw73_64\bin` 加入系统 PATH，或将 DLL 复制到 exe 同级目录。

### Q: 断点不生效
**A:** 确认编译时使用了 `CONFIG+=debug`（tasks.json 中已配置）。Release 模式下断点不可用。

### Q: Linux 下 qmake 找不到
**A:** 运行 `sudo apt install qt5-default`。如果系统有多个 Qt 版本，用 `qtchooser` 选择。

### Q: VSCode IntelliSense 报红但编译正常
**A:** IntelliSense 的 includePath 可能缺少某些路径。检查 settings.json 中的 `C_Cpp.default.includePath` 是否包含所有需要的 Qt 头文件路径。

---

## 8. 快捷操作速查

| 操作 | 快捷键 | 说明 |
|------|--------|------|
| 编译全部 | `Ctrl+Shift+B` | qmake + make |
| 运行/调试 | `F5` | 使用当前选中的 launch 配置 |
| 切换 launch 配置 | 左下角下拉菜单 | Debug Core / TestComm 等 |
| 停止运行 | `Shift+F5` | 终止当前调试会话 |
| 打开终端 | `` Ctrl+` `` | 在 VSCode 内打开终端 |
| 跳转到定义 | `F12` | 跳转到函数/类定义 |
| 查找引用 | `Shift+F12` | 查找所有引用 |
| 全局搜索 | `Ctrl+Shift+F` | 在项目中搜索文本 |
