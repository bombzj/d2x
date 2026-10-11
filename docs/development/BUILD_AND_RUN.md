# 构建、运行与打包

操作说明不记录各功能的重复构建流水；功能范围见[项目基线](../../BASELINE.md)，包身份见[运行包记录](#运行包记录)。命令需要当轮用户授权时才执行，默认不构建、运行或打包。

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

首次配置时，CMake 会从 GitHub 下载固定版本的 raylib、StormLib、Asio 和 BNCSutil，所以需要网络与 Git。不要复制 Windows 的 `build` 目录给 Linux 继续编译。Linux 图形应用不需要 Wine；联网认证会只读配置中的原版客户端文件。`--hidden` 仍会创建图形上下文，不能据此在无显示服务的服务器上运行。功能范围见基线，Linux验证限制见本页支持状态。

游戏内由原服提供角色、单位和区域状态，客户端重建 MPQ 地形。技能菜单悬停后用 `F1–F8` 绑定，`1–4` 饮药，`B` 展开腰带，`I` 整理库存；菜单不暂停联机会话。本地读写档与直进区域参数已退出产品入口。

## Windows 运行

构建已有EXE后，在项目根目录使用 `Play.cmd`，或运行 `build/bin/d2x.exe`。显式 `--mpq <目录或文件>` 优先；未指定时，依次搜索工作目录本身、EXE 所在目录本身、工作目录的 `assets/mpq2`，再搜索 EXE 所在目录及最多四级父目录中的 `assets/mpq2`。自动定位以 `d2data.mpq`／`D2Data.mpq` 为入口，找到首个目录即停止，挂载该目录全部 MPQ，不递归搜索子目录。工作目录与 EXE 所在目录可以不同。MPQ 不参与编译，但运行时必须可用。源码入口为 `CMakeLists.txt`、`cmake/` 和 `src/`；许可和素材来源见 `LICENSE`、[第三方说明](../resources/THIRD_PARTY.md) 及 `docs/licenses/`。

当前源码普通启动进入原图主菜单：Battle.net沿账号／Realm／角色／房间链，Single Player使用内存MCP／D2GS；TCP/IP在本机选角后按原版直连4000并交换本地存档，详见联网模块。F11保存、Ctrl+F11校验后重载；ESC／失焦暂停单机，共享房间继续推进。角色、战斗、库存及保存由所连接服务端执行，当前支持范围和缺口见[基线](../../BASELINE.md)。客户端`dist/current`与独立服务端`dist/server`均为Windows Release包；运行证据见对应专题，速度测量见本页，不将有限冒烟视为所有玩法验证。

Windows EXE 需要同目录 `d2x_bncs_legacy.dll`。`--online-config <路径>` 指定配置，原版客户端文件只读参与认证；PvPGN 模式无需 CD-key。网络 worker 独立于菜单、资源加载和绘制持续推进，`--hidden` 也不会进入本地玩法。`--load <file.d2s>`或`--class <MPQ职业名>`可启动嵌入宿主并通过原协议快捷入局，`--save <新file.d2s>`指定新目标；已存在目标拒绝覆盖，继续用--load。--seed／--map-seed／--population-seed／--level／--region／--difficulty等旧世界参数仍不可用。存储约束见[存档](../modules/SAVES.md)。

先通过 UI 记忆一次账号，可简化重复入局：

```powershell
.\Play.cmd -OnlineCharacter <角色名> -OnlineCreateGame <新房间>
.\Play.cmd -OnlineCharacter <角色名> -OnlineJoinGame <已有房间>
# EXE 对应参数：
.\build\bin\d2x.exe --online-character <角色名> --online-create-game <新房间> --debug-pipe d2x-debug
```

快捷流程只执行一次正常认证、选角、建房／加入；不绕过原服票据、不重复注册或建房。仅指定角色则进入大厅。失败或人工操作结束自动步骤，继续使用页面或 command；有密码或其他房间设置使用这些正式入口。记忆按配置文件路径隔离，复制到包后需在包目录登录一次。

ESC 优先关闭面板，无面板时打开游戏菜单；Save and Exit 请求原服退局保存，成功离局返回对应服务器／本机选角。关闭窗口／quit 仍持续服务退局交换至响应或期限后退出。测试 `pause/resume` 仅在显式调试管道启用，冻结客户端画面／界面输入，服务器与网络持续运行；不提供原服单步，恢复使用最新副本。详见[调试入口](DEBUG_PIPE.md)。

聊天操作与实现范围见[联网模块](../modules/NETWORK.md#多人只读副本)，本页不维护功能完成清单。

启动脚本不隐式构建：缺EXE时要求显式执行scripts/build.ps1。

EXE 的可选 `--startup-profile <路径.tsv>` 按单调时钟记录从 `runGame` 开始的首次生命周期节点：MPQ 挂载、平台、菜单资产、首个显示帧、服务端世界、客户端内容、游戏 UI 及首个已绘制游戏帧。每个节点只记录一次并立即刷新；未指定不产生文件。`--frames <数量>` 可有限运行后正常退局，`--hidden` 仍建立 OpenGL 上下文但跳过音频设备初始化。测速需注明运行条件，文件首帧时间不包含操作系统创建进程之前的耗时，也不代表首次磁盘冷启动或后续每次入局耗时。

启动优化的有限测量见[启动测量](#启动测量)；包身份由本页[运行包记录](#运行包记录)维护。

双客户端从两个终端分别运行 `dist/current/Play.cmd -PipeName d2x-player-one` 和 `dist/current/Play.cmd -PipeName d2x-player-two`，在各自窗口登录不同账号并选择不同角色。第一位创建房间，第二位点击 Join、选择真实列表项查看详情后加入，或输入名称／密码直接加入。参考服可能不在列表展示密码房间或资格不符房间；原服决定加入结果。调试管道名必须不同，账号记忆仍按配置路径共享；第二个窗口修改记忆不会替换第一个窗口已登录的会话。

如果编译机器不能访问 GitHub，CMake 可使用 `external/raylib` 和 `external/stormlib` 的固定版本源码；系统编译器、CMake 和开发库仍需安装。当前源码存档格式见[存档说明](../modules/SAVES.md)，旧档不迁移。原资源只维护项目 `assets/mpq2` 一份，EXE 通过 `--mpq` 读取，不为本地打包抽取或复制 MPQ，也不生成精简资源包。原 MPQ 文件清单见 [MPQ 资源](../resources/MPQ.md)。

## 当前本地分发目录

独立无图形PvPGN游戏服务端使用`d2x_pvpgn`目标，输出`d2x_server.exe`，发行目录为`dist/server`，不覆盖下面的客户端包。配置、独立启停和保存限制统一见[PvPGN服务端](PVPGN_SERVER.md)。原服服务启动脚本不变；默认游戏端口统一4000，同机原D2GS必须先释放该端口。

客户端运行目录固定为 `dist/current/`，入口 `Play.cmd`，根目录放置 `d2x.exe`。下面是显式重新构建Release客户端包的操作示例，不代表当前已交付包的构建类型：

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\package.ps1
.\dist\current\Play.cmd -Mpq assets/mpq2
```

`package.ps1` 只复制已构建程序、两份运行脚本及说明／许可，覆盖更新同一目录，不复制 MPQ、不删除该目录的存档或截图、不运行程序。同步文档时移除源目录已不存在的说明文件，避免目录调整后留下过期文档。未指定 `-Mpq` 时启动脚本先查运行目录本身和 EXE 同目录，再从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处可把完整原 MPQ 放在 `d2x.exe` 旁边，或传实际原资源路径。`-Mpq/-OnlineConfig` 按调用时工作目录解析为绝对路径。程序工作目录为包根，客户端偏好与截图归包，角色持久化由原服处理。整个 `dist/` 忽略Git。示例路径按仓库根作为调用目录；例如从仓库根启动 `dist/current/Play.cmd -Mpq assets/mpq2`，不要把包目录当原资源相对根。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。

独立服务端在CMake Tools中选择`d2x_pvpgn`构建目标，成功后运行[scripts/package-server.ps1](../../scripts/package-server.ps1)。原`d2x`目标和客户端打包脚本继续保留；`d2x_server`是内核库目标，不能误当成EXE目标。两份脚本均复制当前已构建文件，不隐式编译或切换配置。服务端启动／关闭请使用[PvPGN部署页](PVPGN_SERVER.md)中的独立脚本，不使用客户端Play入口。

## 运行包记录

当前记录的运行包日期为2026-10-12，Windows **Release**，D2S v96；本批接通六项服务端协议缺口：本人资源／路径校正18／95／96、光标原件生命周期42、尸体首见74及全局归属8E、任务公共状态29／完整刷新52及目标资格、目标移动与动作10／68／6A／6B，以及入场失败B4。同步修正经验／金币增量、属性宽度、关联准入清理、59→0B入场依赖、9C／9D动作分派、5E／5F入场初始化、NPC27最多7条任务／介绍、原任务菜单类型转换、客户端MPQ闲谈及28交互确认、2F已有会话确认与TCP/IP退局05／06及消费等待，增加可选原服S2C日志。原版Host对照后修正介绍存档段、任务与介绍独立确认、第一幕房间任务序列、NPC文字标点匹配、旧任务本局标志清理、购买自动装备及回执原件、已售装备迟到消息和部分耐久修理。客户端与独立服务端为同一交付版本，具体入口与限制见[服务端协议](../modules/SERVER_PROTOCOL.md)。以下哈希标识当前记录的磁盘包。

| 产物 | 目录 | SHA256 |
| --- | --- | --- |
| 客户端 | `dist/current/d2x.exe` | `D2D1F89A0ADE7257B13BEE5A2837413B65BF39B396B7426550B51704B7E79704` |
| 独立服务端 | `dist/server/d2x_server.exe` | `2CDE920B7684031D9F830930127DFB8D76D291C6F2D601AAB943213F1070FCD8` |
| 网络DLL | 两包的`d2x_bncs_legacy.dll` | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

当前 Windows Release 客户端／独立服务端包已构建、打包。本批复用现有游戏与调试管道完成单机及双客户端TCP/IP有限冒烟，范围和未覆盖分支见[服务端协议有限冒烟](../modules/SERVER_PROTOCOL.md#本批有限冒烟)。未新增测试脚本、用例或专用程序，未新增PvPGN后端验证；原版1.13c已有入场、物品、移动、药水、领奖及正常退出重入的用户确认；其后发现NPC服务菜单及提前任务提示问题，本包已修正，并完成原版Host／D2X Join与自研Host的物品、代表技能及保存重入有限对照；最新原版Join／D2X Host人工复验尚未完成。具体证据及未核实差异由[协议对照](../modules/SERVER_PROTOCOL.md#原版-host-与自研-host-对照)维护。以下均为历史包的有限证据，不能认证本包：上一包通过元素法术隔离、近战四项效果与喷血、野蛮人双剑／单剑握持与物品说明、保存重入代表冒烟，环境弹体伙伴格挡为源码核对，见[收尾复核](../gameplay/items/EVIDENCE.md#相关功能收尾复核)。前一动画包通过吸血／吸魔／压碎原图、持续喷血／落地血迹、死亡／到期／离区／回区及Drain零代表冒烟，见[武器命中动画证据](../gameplay/items/EVIDENCE.md#武器命中动画有限冒烟)。此前结算包通过近战四项效果、伤口刷新／背压／到期、Drain零、左右手弓箭与过量伤害吸取上限、保存新进程重入及伤口击杀归功代表冒烟，实际结果及中间失败见[武器命中效果证据](../gameplay/items/EVIDENCE.md#武器命中效果有限冒烟)。前包双手剑、剑盾、双剑、职业说明差异及保存重入见[双手剑与左右手证据](../gameplay/items/EVIDENCE.md#双手剑与左右手有限冒烟)。更早的单机服务端动画补齐见[服务端动画证据](../gameplay/skills/COMMON.md#服务端动画补齐有限冒烟)；IAS／FCR／FHR／FBR断点、GH／BL、圣盾及双客户端TCP/IP见[动画断点证据](../gameplay/skills/COMMON.md#动画断点有限冒烟)，本包未重做全部断点及TCP路径。前包的玩家装备、防御技能、动作伤害／命中、四抗、破损／副武器组及保存重入覆盖见[人物属性证据](../gameplay/characters/ATTRIBUTES.md#当前包有限冒烟)；佣兵数值、装备、抗性封顶及保存重入见[佣兵证据](../gameplay/characters/HIRELINGS.md#当前包有限冒烟)。技能描述、耐久提示及启动测量分别由[描述专题](../gameplay/skills/DESCRIPTIONS.md#当前包有限冒烟)、[经典HUD](../gameplay/ui/CLASSIC_HUD.md#装备耐久提示)及[开发指南](BUILD_AND_RUN.md#启动测量)维护。

前包通过单机、双客户端TCP/IP及自研独立服务端接PvPGN后端的列出命令路径，范围与包身份见[命令分流有限冒烟](../modules/NETWORK.md#命令分流有限冒烟)。本轮未启动PvPGN／原D2GS，独立服务端只完成构建和打包；NoDrop概率、梦魇／地狱人数倍率、全部职业特殊动作、装备／套装／孔内组合、完整怪物行为、跨机器、Linux和性能尚未全面验收；其他版本的历史成功不能替代当前包验证。

## 启动测量

2026-10-10 Windows Release启动优化包的有限记录，D2S v96；客户端SHA256 `BD304511433DFEAB2EDE6D68B234A5954EE40F853D1EF8C7CE731FB7A2D51B66`。同机、同MPQ、同角色／地图种子的新进程对照：

| 路径 | 优化前 | 优化后 |
| --- | ---: | ---: |
| 主菜单，正常窗口含音频 | 5.29秒 | 0.79秒 |
| 单机直入，正常窗口 | 7.95秒 | 2.83秒 |
| 单机直入，hidden | 7.51秒 | 2.59秒 |
| 本机TCP加入，hidden | 8.23秒 | 2.70–3.84秒 |

挂载耗时4.49–4.69秒降至5.65–7.30毫秒；改动为延后枚举文件名、32 MiB有界解压缓存、TXT索引、同挂载只读内容共享和LAN Join不初始化闲置宿主。实现／原版依据见[MPQ](../resources/MPQ.md#查询与提取)与[参考设计](../architecture/REFERENCE_DESIGN.md#7-本项目的提取单位与所有权)。

计时从runGame到真实EndDrawing后首帧，hidden仍创建OpenGL但不初始化音频。TCP用隔离宿主／客户端及6223／4110端口，两次无调试干扰测得上述波动，另一次取得实际连接及failures=0；有限帧后正常退局保存，截图为原城镇／HUD，资源工具仍可枚举TXT／BIN，临时进程已退出。证据：`artifacts/startup-release-20261010`，无新增测试程序。没有重启清磁盘缓存、Debug／原版对照、PvPGN后端复验、跨机器或Linux测速；保留既有编译警告，不认证全部玩法，也不认证后续源码。
