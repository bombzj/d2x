# 构建与运行

## Linux 支持状态

核心代码使用 C++20、标准文件系统、raylib 和 StormLib。存档替换单独区分 Windows 的 MoveFileExW 与其他系统的 rename；玩法、数据和存档编码不依赖 Win32。两个依赖都有 Linux 构建支持，运行时读取 `assets/mpq2` 的原始 MPQ。CMake 只在 Windows 定义相关宏，只对 MinGW 使用 Windows 静态链接选项。

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

进入项目根目录，执行：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF
cmake --build build --parallel
./build/bin/d2x
```

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib 和 StormLib，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 不需要 Wine，也不使用 Windows EXE。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。

游戏内由营地步行进入连续野外，点击洞口／楼梯旅行；`Ctrl+F2` 是开发用区域目录，独立区域也可用 `--level 38` 等参数查看。打开鼠标技能菜单，悬停技能图标按 `F1–F8` 绑定，之后单按该键切换对应鼠标技能；`1–4` 饮药，`B` 展开腰带。原表人口计划按附近房间创建敌人；未实现类型使用保留真实身份的沉沦魔替身。按 `I` 整理，F11 保存、Ctrl+F11 读取。营地内 `Ctrl+F4` 走近原版私人储物箱。

## Windows 运行

在项目根目录使用 `Play.cmd`，或运行 `build/bin/d2x.exe`。程序默认读取 `assets/mpq2` 中的原始 MPQ；其他位置可用 `--mpq <目录>` 指定。MPQ 不参与编译，但运行时必须可用。源码入口为 `CMakeLists.txt`、`cmake/` 和 `src/`；许可和素材来源见 `LICENSE`、[第三方说明](THIRD_PARTY.md) 及 `docs/licenses/`。

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式 v27、规则 v54，旧档不迁移。分发目录保留完整 `assets/mpq2`，EXE 直接读取原始 MPQ，不生成精简资源包或 ZIP。原 MPQ 文件清单见 [MPQ 资源](MPQ_RESOURCES.md)。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
