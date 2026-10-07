# 构建、运行与打包

操作说明不记录各功能的重复构建流水；当前源码与运行包差异见 [项目基线](../../BASELINE.md)。命令需要当轮用户授权时才执行，默认不构建、运行或打包。

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

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib、StormLib、Asio 和 BNCSutil，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 图形应用不需要 Wine；联网认证会只读配置中的原版客户端文件。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。当前源码／已交付包差异见基线，Linux未验证。

游戏内由原服提供角色、单位和区域状态，客户端重建 MPQ 地形。技能菜单悬停后用 `F1–F8` 绑定，`1–4` 饮药，`B` 展开腰带，`I` 整理库存；菜单不暂停联机会话。本地读写档与直进区域参数已退出产品入口。

## Windows 运行

构建已有EXE后，在项目根目录使用 `Play.cmd`，或运行 `build/bin/d2x.exe`。显式 `--mpq <目录或文件>` 优先；未指定时，依次搜索工作目录本身、EXE 所在目录本身、工作目录的 `assets/mpq2`，再搜索 EXE 所在目录及最多四级父目录中的 `assets/mpq2`。自动定位以 `d2data.mpq`／`D2Data.mpq` 为入口，找到首个目录即停止，挂载该目录全部 MPQ，不递归搜索子目录。工作目录与 EXE 所在目录可以不同。MPQ 不参与编译，但运行时必须可用。源码入口为 `CMakeLists.txt`、`cmake/` 和 `src/`；许可和素材来源见 `LICENSE`、[第三方说明](../resources/THIRD_PARTY.md) 及 `docs/licenses/`。

当前源码普通启动及 `Play.cmd` 进入原图主菜单，由 Battle.net 登录／注册→Realm→服务器角色→创建或加入房间。Single Player、本地选角／D2S 与直进场景产品入口已移除；原用户文件继续保留。当前运行包及有限原服证据见项目基线。

Windows EXE 需要同目录 `d2x_bncs_legacy.dll`。`--online-config <路径>` 指定配置，原版客户端文件只读参与认证；PvPGN 模式无需 CD-key。网络 worker 独立于菜单、资源加载和绘制持续推进，`--hidden` 也不会进入本地玩法。`--class/--load/--save/--seed/--map-seed/--population-seed/--level/--region/--difficulty` 等旧参数明确报错。

先通过 UI 记忆一次账号，可简化重复入局：

```powershell
.\Play.cmd -OnlineCharacter <角色名> -OnlineCreateGame <新房间>
.\Play.cmd -OnlineCharacter <角色名> -OnlineJoinGame <已有房间>
# EXE 对应参数：
.\build\bin\d2x.exe --online-character <角色名> --online-create-game <新房间> --debug-pipe d2x-debug
```

快捷流程只执行一次正常认证、选角、建房／加入；不绕过原服票据、不重复注册或建房。仅指定角色则进入大厅。失败或人工操作结束自动步骤，继续使用页面或 command；有密码或其他房间设置使用这些正式入口。记忆按配置文件路径隔离，复制到包后需在包目录登录一次。

ESC 优先关闭面板，无面板时打开游戏菜单；Save and Exit 请求原服退局保存，成功离局返回服务器选角。关闭窗口／quit 仍持续服务退局交换至响应或期限后退出。测试 `pause/resume` 仅在显式调试管道启用，冻结客户端画面／界面输入，服务器与网络持续运行；不提供原服单步，恢复使用最新副本。详见[调试入口](DEBUG_PIPE.md)。

启动脚本不隐式构建：缺EXE时要求显式执行scripts/build.ps1。

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式见[存档说明](../modules/SAVES.md)，旧档不迁移。原资源只维护项目 `assets/mpq2` 一份，EXE 通过 `--mpq` 读取，不为本地打包抽取或复制 MPQ，也不生成精简资源包。原 MPQ 文件清单见 [MPQ 资源](../resources/MPQ.md)。

## 当前本地分发目录

当前运行目录固定为 `dist/current/`，入口 `Play.cmd`，根目录放置 `d2x.exe`。在仓库根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\package.ps1
.\dist\current\Play.cmd -Mpq assets/mpq2
```

`package.ps1` 只复制已构建程序、两份运行脚本及说明／许可，覆盖更新同一目录，不复制 MPQ、不删除该目录的存档或截图、不运行程序。同步文档时移除源目录已不存在的说明文件，避免目录调整后留下过期文档。未指定 `-Mpq` 时启动脚本先查运行目录本身和 EXE 同目录，再从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处可把完整原 MPQ 放在 `d2x.exe` 旁边，或传实际原资源路径。`-Mpq/-OnlineConfig` 按调用时工作目录解析为绝对路径。程序工作目录为包根，客户端偏好与截图归包，角色持久化由原服处理。整个 `dist/` 忽略Git。示例路径按仓库根作为调用目录；例如从仓库根启动 `dist/current/Play.cmd -Mpq assets/mpq2`，不要把包目录当原资源相对根。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
