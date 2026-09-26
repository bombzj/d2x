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

普通图形启动及默认 `Play.cmd` 先显示资料片角色列表；可新建七职业普通角色、输入角色名、选择已有角色进入游戏，或确认删除角色。角色以原版 D2S v96 存放在运行目录的 `saves/`；按 F11 保存，正常退出自动保存。仅指定 `--mpq`、`--debug-pipe`、`--debug-run` 不再跳过角色界面；显式 `--class`、`--load`、`--hidden`、`--level`、`--region` 等场景／批处理选项仍直进游戏。启动器仅在显式传入 `-Level`／`-Region` 时附带区域参数。角色确认后重新显示原 MPQ 加载动画，覆盖读档、世界构造和场景资源上传。角色前端只支持资料片普通角色，转换和专家模式控件禁用；D2S 的明确支持范围见[存档](SAVES.md)，旧内部档不迁移。

直接选择职业开始测试，不经过 UI 或存档：

```powershell
.\Play.cmd -Class Sorceress
.\Play.cmd -Class Necromancer -Level 8 -DebugPaused
.\build\bin\d2x.exe --class Sorceress --level 8 --hidden --debug-pipe d2x-skill
```

`--class` 按当前 MPQ 原职业名匹配：`Amazon`、`Sorceress`、`Necromancer`、`Paladin`、`Barbarian`、`Druid`、`Assassin`；生成名为 Hero 的全新一级角色及本职业初始装备。未指定 `--save` 时不在退出时自动保存，手动 F11／管道 save 仍可保存。需要保留场景输入时使用独立输出：

```powershell
.\Play.cmd -Load .\saves\Scenario.d2s -Save .\artifacts\scenario-result.d2s -DebugPaused
```

示例输入必须是已有 D2S。`-Class` 与 `-Load` 互斥；脚本保存路径相对调用时工作目录解析，EXE 路径相对进程工作目录。存档加载仍从城镇开始，进入后可通过开发目录或管道 `travel` 到目标地图。不会通过测试入口原地改变现有角色职业。

游戏内 Esc 优先关闭面板，无面板时返回角色列表；角色来自列表／`-Load` 或显式指定了 `-Save` 时先保存，失败则留在游戏。无保存路径的临时新角色不自动保存。旧会话与视图销毁后重新打开列表，下一角色不会继承上一局地图参数或保存路径。关闭窗口与调试 quit 仍退出进程。

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式见[存档说明](SAVES.md)，旧档不迁移。原资源只维护项目 `assets/mpq2` 一份，EXE 通过 `--mpq` 读取，不为本地打包抽取或复制 MPQ，也不生成精简资源包。原 MPQ 文件清单见 [MPQ 资源](MPQ_RESOURCES.md)。

## 当前本地分发目录

当前运行目录固定为 `dist/current/`，入口 `Play.cmd`，根目录放置 `d2x.exe`。在仓库根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\package.ps1
.\dist\current\Play.cmd -Mpq .\assets\mpq2 -Class Sorceress
```

`package.ps1` 只复制已构建程序、两份运行脚本及说明／许可，覆盖更新同一目录，不复制 MPQ、不删除该目录的存档或截图、不运行程序。未指定 `-Mpq` 时启动脚本从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处须传实际原资源路径。`-Mpq/-Load/-Save` 按调用时工作目录解析为绝对路径。程序工作目录为包根，默认角色存档和截图与包放在一起。整个 `dist/` 忽略 Git。

2026-09-26 本轮鼠标目标保持和步行边界修订通过 Windows Release 构建并更新上述目录，按用户要求未运行游戏或测试；旧包的简测结果不能代替本轮鼠标手感和过界复验。八个历史包已清除 40 份与 `assets/mpq2` SHA-256 完全相同的 MPQ 副本及纯程序目录，共回收约 6539.4 MiB。原始 MPQ、旧压缩包、存档、截图、日志和参考资料保留；旧包目录仅供历史证据，不再作为运行入口。物品显示、技能及角色启动的既有短程记录保留在各 `artifacts/*2026092*/` 目录。

后续共用行走修复及鼠标锁定已由用户确认可用。本次包包含这些修复，以及锁定目标顶部名字／血量持续显示、不可施放技能红色着色、Esc 返回列表和新旧界面输入隔离；Windows Release 编译通过，未执行游戏或测试。D2S 走跑选择只完成格式证据核对，未修改编码，详见 [存档](SAVES.md)。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
