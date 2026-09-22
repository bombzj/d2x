# Linux 构建与最小源码分发

## Linux 支持状态

核心代码使用 C++20、标准文件系统、raylib 和 StormLib。存档替换单独区分 Windows 的 MoveFileExW 与其他系统的 rename；玩法、数据和存档编码不依赖 Win32。两个依赖都有 Linux 构建支持，MPQ 内容不区分系统，可复用同一个 `d2x-act1.mpq`。CMake 只在 Windows 定义相关宏，只对 MinGW 使用 Windows 静态链接选项。

目前 Windows 已实际编译和运行。Linux 已完成源码及依赖配置检查，但开发机器没有可用的 Linux/WSL 环境，**尚未在 Linux 实际编译或运行**。下面使用 X11 后端；Wayland 桌面需要 XWayland。运行游戏需要图形桌面和支持 OpenGL 3.3 的驱动。

## Ubuntu / Debian 构建

建议使用较新的桌面发行版，例如 Ubuntu 24.04；要求 CMake 3.25+ 和支持 C++20 的编译器，建议 GCC 12+。

安装编译工具和窗口、音频、OpenGL 开发依赖：

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git \
  libasound2-dev libx11-dev libxrandr-dev libxi-dev \
  libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev
```

进入解压后的 `d2x` 根目录，执行：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF
cmake --build build --parallel
./build/bin/d2x --mpq assets/mpq2
```

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib 和 StormLib，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 不需要 Wine，也不使用 Windows EXE。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。

游戏内由营地步行进入连续野外，点击洞口／楼梯旅行；`F2` 是开发用区域目录，独立区域也可用 `--level 38` 等参数查看。`F5–F10` 选择技能，右键施放，`1–4` 饮药，`B` 展开腰带。原表人口计划按附近房间创建敌人；未实现类型使用保留真实身份的沉沦魔替身。按 `I` 整理，F11 保存、Ctrl+F11 读取。营地内 F4 走近原版私人储物箱。

## 最少发哪些文件

若对方可以联网获取编译依赖：

```text
d2x/
  CMakeLists.txt
  cmake/                       # Dependencies.cmake
  src/                         # 完整模块目录，含 resources/presets.hpp
  LICENSE
  docs/
    THIRD_PARTY.md
    licenses/                  # 保留代码来源和许可
    BUILD_AND_SHARE.md         # 本说明
  assets/mpq2/                 # 编译不需要；运行本轮地图使用完整源 MPQ
```

`CMakeLists.txt`、完整 `cmake` 和 `src` 是编译输入。`LICENSE`、`docs/THIRD_PARTY.md`、`docs/licenses` 保留源码的许可及来源。MPQ 不参与编译；当前运行使用完整源文件，或待后续重新收集的精简包。已有精简包缺少本轮全部生成模板。

协作交接另附 `README.md`、`BASELINE.md`、`AGENTS.md` 和 `docs/baseline`。Windows 可附带 `Play.cmd` 和 `scripts`，方便构建、启动；Linux 按上述命令执行。

无需发送 `build`、`.git`、`reference`、`downloads`、`artifacts`、`saves`、`tests`、日志、已有 EXE 或工作目录里的旧压缩包。`external` 在联网编译时也可省略。

如果编译机器不能访问 GitHub，再增加当前固定版本的 `external/raylib` 和 `external/stormlib` 源码目录，保留其构建文件、源码、附带库和许可；可以不含其中的 `.git` 与构建缓存。CMake 会优先使用这两个目录。系统编译器、CMake 和上述开发库仍需提前安装。

项目提供的精简分发包是源码、说明、Windows 便捷脚本与现有 MPQ，不含编译产物或测试脚本。MPQ 素材的权利说明见 [第三方说明](THIRD_PARTY.md)，与 GPL-3.0 源码许可分别保留。

本次按用户要求只提交源码和文档，不生成新分发包或精简 MPQ。`dist/d2x-source-classic-hud-20260922.zip` 及 13.19 MiB／397 个资源的 `d2x-act1.mpq` 均为历史 HUD 产物，不代表本次基线。完整源文件清单见 [MPQ 资源](MPQ_RESOURCES.md)。当前存档格式为版本 3，不兼容旧版 1／2；存档不属于编译输入。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
