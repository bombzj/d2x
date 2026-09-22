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
./build/bin/d2x --mpq assets/mpq2/d2x-act1.mpq
```

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib 和 StormLib，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 不需要 Wine，也不使用 Windows EXE。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。

游戏内按 `F2` 分页选择地图，或用 `--level 38` 进入崔斯特瑞姆。`F5–F10` 施放技能，`1–4` 饮药，`B` 展开腰带。原表刷怪已接通，未实现类型使用保留真实身份的沉沦魔替身；`--difficulty normal|nightmare|hell` 和 `--population-seed 210` 可改变刷怪设置。原版掉落执行仍待完成。按 `I` 整理，F11 保存、Ctrl+F11 读取。营地内 F4 走近原版私人储物箱。

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
  assets/mpq2/d2x-act1.mpq      # 编译不需要；运行当前地图需要
```

`CMakeLists.txt`、完整 `cmake` 和 `src` 是编译输入。`LICENSE`、`docs/THIRD_PARTY.md`、`docs/licenses` 保留源码的许可及来源。运行当前地图只需 `d2x-act1.mpq`，无需同时附带五个原始大 MPQ。

建议另外保留 `README.md`，体积很小。若接收者使用 Windows，也可附带 `Play.cmd` 和 `scripts`，方便构建、启动或重新下载资源；Linux 按上面的命令即可。

无需发送 `build`、`.git`、`reference`、`downloads`、`artifacts`、`saves`、`tests`、日志、已有 EXE 或工作目录里的旧压缩包。`external` 在联网编译时也可省略。

如果编译机器不能访问 GitHub，再增加当前固定版本的 `external/raylib` 和 `external/stormlib` 源码目录，保留其构建文件、源码、附带库和许可；可以不含其中的 `.git` 与构建缓存。CMake 会优先使用这两个目录。系统编译器、CMake 和上述开发库仍需提前安装。

项目提供的精简分发包是源码、说明、Windows 便捷脚本与现有 MPQ，不含编译产物或测试脚本。MPQ 素材的权利说明见 [第三方说明](THIRD_PARTY.md)，与 GPL-3.0 源码许可分别保留。

本轮分发包为 `dist/d2x-source-population-20260922.zip`。`assets/mpq2/d2x-act1.mpq` 约 13.06 MiB，含 391 个资源，覆盖 13 个完整预设地形及变体、三个模板和当前所需图形／UI／音效，并新增五张怪物生成原表。源码和 MPQ 需一起更新，旧精简包缺少新表会关闭怪物生成并提示。当前加载 34 种物品美术，659 条物品数据不表示全部功能已实现。原始 `assets/mpq2` 无需全发；存档不属于编译输入，另行按需复制。本轮存档格式为版本 2，不兼容旧版 1。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
