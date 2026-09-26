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

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式见[存档说明](SAVES.md)，旧档不迁移。分发目录保留完整 `assets/mpq2`，EXE 直接读取原始 MPQ，不生成精简资源包或 ZIP。原 MPQ 文件清单见 [MPQ 资源](MPQ_RESOURCES.md)。

## 当前本地分发目录

当前角色直达／城镇规则包为 `artifacts/character-start-town-release-20260926/`，入口 `Play.cmd`，包含本轮角色施法状态归属重构和原地换职业入口移除，以及五个原始 MPQ。正式命令 `scripts/build.ps1 -Configuration Release` 已通过；包内女巫和死灵法师新角色直达、正确初始装备、城镇十项禁用法术无扣蓝／无弹体、冰封装甲允许、野外充能弹及地狱之火、D2S 场景载入和独立输出均通过短程检查。参数 `--class` 与 `--load` 冲突明确拒绝；源场景档哈希未变，未指定保存的新角色没有生成默认档。没有新增测试脚本、用例或专用程序。截图与加载／职业日志在包内 `artifacts/`；未完成七职业全部技能、长时间交互、Linux 或多人联机验收。

2026-09-26 物品显示审查包位于忽略目录 `artifacts/item-display-release-20260926/`，入口为其中的 `Play.cmd`。包含 Release EXE、现有启动／调试脚本、许可与文档，以及五个原始 MPQ；旧分发目录、源码资源和用户存档未覆盖。仅供持有这些原资源的本机使用，原 MPQ 不纳入源码提交。

已从包自身工作目录与 `assets/mpq2` 启动，载入独立测试 D2S、打开背包、导出截图、保存新 D2S，再用包内程序加载该存档，两次退出码均为 0。构建命令为 `scripts/build.ps1 -Configuration Release`；本轮未新增测试脚本，仓库 `tests/` 当前为空。运行验证覆盖四品质生成／拾取、储物箱与方块往返、玩家和佣兵装备、恰西购买／确认出售及存档重载；不是所有物品组合或像素级视觉还原的验收。截图与运行日志保留在忽略的 `artifacts/`，包内日志为 `package-runtime.log`、`package-reload.log`。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。
