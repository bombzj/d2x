# 构建、运行与打包

操作说明不记录各功能的重复构建流水；功能范围见[项目基线](../../BASELINE.md)，包身份见[运行包记录](#运行包记录)。命令需要当轮用户授权时才执行，默认不构建、运行或打包。

需要人工操作时留存消息，用根目录`Play-Diagnostic.cmd`；它沿用普通Play的程序和角色目录，打开游戏及默认调试管道，分别记录客户端／嵌入宿主的双向D2GS消息、程序输出和EXE身份。它不自动构建或修改角色。目录、脱敏范围和故障现场读取方式见[原服收包记录](DEBUG_PIPE.md#原服收包记录)。

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

`--lang <eng|chi|deu|fra|esp|ita|jpn|kor|pol|rus 或 0-9>` 覆盖语言：字符串取自 `data/local/lng/<目录>/` 的原始 string／expansionstring／patchstring.tbl，缺失回退 `eng`；未指定时读 MPQ 的 `data/local/use`。字形取自 `data/local/font/<语言目录>/`（西语系共用 `latin`，中日韩各用 `chi`/`jpn`/`kor`）；所选语言只提供 `latin` 字体时回退 `latin`，此时该语言的非 ASCII 字符无字形可绘。原版五个基础 MPQ 只含 `latin` 字体，中日韩界面需资源包内含对应 `data/local/font/<chi|jpn|kor>/` 字体；`Play.cmd -Lang chi` 转发该参数。字形纹理按需上传：字体表可含上万码点（中日韩 font16 约 16651 帧），仅在实际绘制时上传对应 DC6 帧，避免启动时逐帧建纹理。散装数据目录可用 `d2x_assets <data目录> pack-dir <前缀> <新.mpq>` 打成 MPQ 后挂载（成员名为前缀加相对路径）；繁体中文优先取原版 MPQ 自带的 `data/local/lng/chi/` 文本，仅当原版缺 `expansionstring.tbl` 与 `data/local/font/chi/` 字形时才补入 `PD2/Data` 的对应内容，不使用 SOE 资源。

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
.\scripts\build.ps1 -Configuration Release -Target d2x
.\scripts\package.ps1
.\dist\current\Play.cmd -Mpq assets/mpq2
```

`build.ps1` 默认只构建`d2x`及其依赖；独立服务端用`-Target d2x_pvpgn`，资源工具用`-Target d2x_assets`，需要全部目标时显式指定`-Target all`。公共头文件仍会使依赖它们的编译单元重新构建；Ninja增量不会因为只选客户端而跳过必要依赖。

`package.ps1` 只复制已构建程序、两份运行脚本及说明／许可，覆盖更新同一目录，不复制 MPQ、不删除该目录的存档或截图、不运行程序。程序、脚本和文档按长度／SHA256比较，内容相同则跳过复制，并报告复制文件数、字节数与跳过数；连接配置也只在内容变化时更新。同步文档时移除源目录已不存在的说明文件，避免目录调整后留下过期文档。运行目录已有的`artifacts`、旧程序和用户文件不会进入复制清单，也不会被清理，因此整个目录占用不代表本次交付大小。未指定 `-Mpq` 时启动脚本先查运行目录本身和 EXE 同目录，再从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处可把完整原 MPQ 放在 `d2x.exe` 旁边，或传实际原资源路径。`-Mpq/-OnlineConfig` 按调用时工作目录解析为绝对路径。程序工作目录为包根，客户端偏好与截图归包，角色持久化由原服处理。整个 `dist/` 忽略Git。示例路径按仓库根作为调用目录；例如从仓库根启动 `dist/current/Play.cmd -Mpq assets/mpq2`，不要把包目录当原资源相对根。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。

独立服务端在CMake Tools中选择`d2x_pvpgn`构建目标，成功后运行[scripts/package-server.ps1](../../scripts/package-server.ps1)。原`d2x`目标和客户端打包脚本继续保留；`d2x_server`是内核库目标，不能误当成EXE目标。两份脚本均复制当前已构建文件，不隐式编译或切换配置。服务端启动／关闭请使用[PvPGN部署页](PVPGN_SERVER.md)中的独立脚本，不使用客户端Play入口。

## 运行包记录

当前客户端包为2026-10-13 Windows **Release**，D2S v96，保留刺客动作、房间引用／活动碰撞、连续跨区寻路、NativePathStep、NPC停止与请求距离、城镇WP真实入场模式、中立物件入场碰撞及途中路径保留，另纳入当前工作区的CastHandoff、怪物格挡包、公开装备基础属性和衣服拾取同步修正。本轮用户授权构建与打包，Release目标d2x成功；修正native_combat_wire的MotionTarget命名空间编译错误，保留prepareReload的非致命未初始化警告。未启动游戏或执行玩法测试，留待用户双客户端复验；未新增测试脚本／用例／专用程序。独立服务端`dist/server`未重建，本轮不能用其旧程序验收新修正。

房间改造阶段Release首次计划365个构建步骤，三份日志共涉及350个不同C++编译单元、365次编译动作（包括失败与重编译）。大范围重编译来自navigation.hpp、area_store.hpp、旅行接口及resources/archive.hpp等公共依赖变化，未清空构建缓存／切换配置或构建all目标。两次失败分别为spatial缺AreaStore完整头文件、调试快照仍读取已删除的旧跨界字段，均修正后增量续建。最终收尾为2个C++编译及3个链接步骤，成功生成d2x.exe。证据为`artifacts/flavie-boundary-20261012/build-rooms-release.log`、`build-rooms-release-2.log`和`build-rooms-release-3.log`；更早的连续寻路构建见同目录`build-release.log`。现有非致命警告保留，diff空白核对通过。原D2MOO结构尚未完全复刻的范围由[地图限制](../modules/MAP.md#地图缓存与分帧准备)及[基线](../../BASELINE.md)列明。

用户23:38对照诊断发现普通04预测走到NPC中心、等96才交谈、入场WP模式0及stash斜角预测／权威分叉。本轮已构建、打包并重放；规则与证据维护在[NPC交互](../gameplay/npc/INTERACTIONS.md)、[自研传送点](../gameplay/world/OBJECTS.md#自研传送点)和[共用寻路](../modules/MAP.md#共用寻路)。构建共涉及170个不同C++单元、526次编译动作（含迭代、失败和重编译，并非一次编译526文件）；三次较大增量分别150／168／145次，原因是PlayerState持有定点状态、公共interaction_geometry头经game_messages等广泛传播，期间公共接口多次调整重复触发依赖。诊断快照类型调整另触发43次；后续私有源码修改每次1–4个C++单元。没有清缓存、切配置或构建all。一次链接失败来自构建过程中修改源码导致旧导航库与新调用方不一致；另一次诊断编译错误是误把AreaMetadata当成含Grid的AreaDefinition，均修正后续建成功。日志在`artifacts/path-smoke-20261013-002214/build*.log`，最终构建为1个C++单元及2次链接。

包中保留此前地图缓存／分帧准备、战斗索引、阵营／组队、进退场与PvP，以及Party敌意红字、右上角骷髅和姓名颜色能力。此前表现的有限运行范围见[联网记录](../modules/NETWORK.md#组队邀请客户端)，旧构建日志在`artifacts/hostility-display-20261012`及`artifacts/area-performance-build*.log`；这些记录不等于当前包重新运行通过。

下面的提交前有限冒烟属于此前EXE；不将这些路径视为当前新增修正或全部玩法验收。宿主区域蓝图在独立MPQ线程准备，客户端房间消息消费仍为软预算，不保证每帧严格限时或完全无停顿；Play-Diagnostic.cmd保留双方消息和[area-load]阶段耗时。原MPQ、存档、历史程序和artifacts保持原位置，不复制进新的交付内容。

前批两幕三难度、组队任务及物品补齐的基础范围分别见[第一幕](../gameplay/monsters/ACT1.md#三难度重新核对)、[第二幕](../gameplay/monsters/ACT2.md#三难度重新核对)、[任务系统](../gameplay/quests/SYSTEM.md)与[物品核对](../gameplay/items/EVIDENCE.md#客户端与服务端物品核对)。以下哈希标识当前磁盘包。

前次保留的程序`dist/current/d2x-retained-20261012.exe`及`d2x_bncs_legacy-retained-20261012.dll`继续保留。本轮更新前的包内EXE／DLL另留在`artifacts/flavie-boundary-20261012/d2x-before.exe`与`dll-before.dll`，更早备份在`artifacts/diagnostics-fix-20261012`。根目录Play-Diagnostic.cmd使用新构建及既有角色目录，包内同名入口使用包内程序／角色目录，并记录双方消息。

| 产物 | 目录 | SHA256 |
| --- | --- | --- |
| 客户端／嵌入宿主 | `dist/current/d2x.exe`（25120666字节） | `33E69352223C0F4D8BCA5E7728E03A7A38D50A7CE21672FB6FA62680F77F6FA6` |
| 独立服务端 | `dist/server/d2x_server.exe` | `796E0D45A1878B6FB049DC6C191A28562D0668727BBB953CBC41AD67E3D14FC4` |
| 网络DLL | 两包的`d2x_bncs_legacy.dll` | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

用户此前用EA5256包进行TCP/IP近战验收，发现D2X本地重复攻击不恢复追击及原版Join崩溃。CastHandoff与格挡单位包／路径目标保留现已随当前包构建；证据及边界见[普通攻击](../gameplay/skills/GENERAL.md#原协议及客户端修复)和[怪物动作](../gameplay/monsters/COMMON.md#动作死亡与奖励)。公开装备与最后一次衣服拾取修正见[物品协议](../gameplay/items/PRESENTATION.md#已接显示与反馈)。这些新增修改尚未运行复验，下节单机记录仍只认证此前EA5256程序。

### 当前包单机消息日志冒烟

2026-10-13，包内EXE为上述EA5256哈希，当前MPQ；角色aaa（种子2119970076）与ShotAssassin（种子210）使用存档副本，未修改原始存档。日志及快照在`artifacts/path-smoke-20261013-002214/delivery-final`和`assassin-final`。

| 路径 | 实际观察 |
| --- | --- |
| stash转角 | 原03至(5461,4712)，两端均到该点，碰撞轮廓未缩小 |
| Akara／城镇WP | 远处单击Akara，两端在(5530.7841796875,4689.2158203125)停止，13至27／28约34.5毫秒；WP入场51 mode2，远处点击收到63且城镇unlocked |
| 城镇出口及野外转角 | 自然步行1→2→1→2，两端位置一致；此前失败的(5692,4944)两端均到达 |
| Flavie及2／3边界 | 鼠标显示Flavie，点击得到真实会话，关闭后可移动；自然步行Blood Moor／Cold Plains边界重复往返，两端位置一致 |
| 刺客普通攻击 | 出城后附近quillrat移动；原左键普通攻击自然fallen GUID84，生命1→0并rewardComplete，攻击释放后正常返城 |

上述有限样本宿主failures=0、characterIssues为空；自然走路／点击未使用travel、spawn、unlock、damage等管理准备命令。完整种子、延迟、动态单位占位、客户端全速度修饰及房间内存回收仍按[地图限制](../modules/MAP.md)列明，不能宣称全玩法与原版逐帧一致。

### 本批提交前冒烟

下列记录属于此前EXE SHA256 `FD5683B5224982A5D3049FD6BFF1651B51D5C7C318321C34FAEA745B8260FFD3`，不能替代本次新增表现的核对。

2026-10-12 Windows Release当时工作区，原MPQ，普通难度。使用全新隔离女巫SmokeHost／SmokeGuest，管理经验准备为12级，TCP/IP仅127.0.0.1:4400；后续新进程Single Player读回同一隔离角色。证据在忽略目录`artifacts/precommit-smoke-20261012-175026`，含双方消息、宿主快照、截图和读回记录，未新增测试脚本／用例／专用程序。

- 两个真实客户端入场，地图就绪、无协议错误；组队邀请／接受得到双方partyId3。通过原P面板开启敌意后双向关系位8，关闭后回0；右侧Ignore／Squelch按钮得到位2／4，普通消息送达，被屏蔽消息未进入对方历史。保存／新局不保留临时队伍关系。
- 暂停宿主后以原技能／攻击请求和固定步观察玩家对玩家的Fire Bolt及普通攻击扣血，随后准备低生命目标，原攻击造成死亡、收到5A/type6击杀通知并正常复活回城。准备伤害不计作PvP命中证据；未逐通道复验17%数值、伤口／吸取、全部技能、金币惩罚或耳朵。
- 原火弹击杀准入的fallen1，客户端生命比例0／模式12；管理生成hp1后正常拾取至背包、拿起和丢回地面，另一客户端看到同一GUID。新增单位的空间索引与地面物品索引没有阻断这些路径；不认证自然掉落概率或全部物品组合。
- 管理定位到边界附近后通过普通移动完成城镇→Blood Moor→城镇，客户端地图保持就绪，权威回到区域1／(4590,4645)。此前远点绕围栏停在障碍处，不计作整段营地寻路通过；该边界样本不认证全种子／延迟条件。冷内容准备的剩余耗时见[地图限制](../modules/MAP.md#地图缓存与分帧准备)。
- 客人正常退局收到type3离开提示，宿主失败计数0；两端正常退出。新进程Single Player恢复SmokeHost：12级、经验100001、金币1234、Fire Bolt基础1级、7件物品、地图就绪，失败计数0，随后正常退局并关闭。

首轮实际打开组队面板发现资源准入错误：当前partyboxes.dc6有18帧，源码错误要求恰好16帧。已改为接受18帧并直接使用原16／17红色禁用图标；增量构建后重启两端，以修正版重复上述按钮及战斗观察。修正前结果不认证修正版面板。没有操作原版客户端、PvPGN后端、独立服务端、Linux、全部难度和职业，也没有全面性能基准；只有列出的路径有限通过。

### 前批运行证据

下述运行记录属于5F修正之前的包，不能认证当前5F分支或其余工作区改动。

当前 Windows Release 客户端／独立服务端包已构建、打包；物品代表路径及新进程保存重入见[物品核对](../gameplay/items/EVIDENCE.md#客户端与服务端物品核对)，其中真实双客户端分金使用本批补齐后的此前客户端，最终客户端另复验低生命击杀。本轮未启动PvPGN、未重测原版人工Join或噩梦／地狱。前一两幕代表AI／技能／移动及保存结果属于此前包，见[两幕最小冒烟](../gameplay/monsters/ACT1.md#三难度核对后的最小冒烟)。任务收尾的此前双客户端证据见[任务有限运行核查](../gameplay/quests/SYSTEM.md#有限运行核查)。此前原协议单机及双客户端TCP/IP有限冒烟，范围和未覆盖分支见[服务端协议有限冒烟](../modules/SERVER_PROTOCOL.md#本批有限冒烟)。未新增测试脚本、用例或专用程序，未新增PvPGN后端验证；原版1.13c已有入场、物品、移动、药水、领奖及正常退出重入的用户确认；其后发现NPC服务菜单及提前任务提示问题，本包已修正，并完成原版Host／D2X Join与自研Host的物品、代表技能及保存重入有限对照；最新原版Join／D2X Host人工复验尚未完成。具体证据及未核实差异由[协议对照](../modules/SERVER_PROTOCOL.md#原版-host-与自研-host-对照)维护。以下均为历史包的有限证据，不能认证本包：上一包通过元素法术隔离、近战四项效果与喷血、野蛮人双剑／单剑握持与物品说明、保存重入代表冒烟，环境弹体伙伴格挡为源码核对，见[收尾复核](../gameplay/items/EVIDENCE.md#相关功能收尾复核)。前一动画包通过吸血／吸魔／压碎原图、持续喷血／落地血迹、死亡／到期／离区／回区及Drain零代表冒烟，见[武器命中动画证据](../gameplay/items/EVIDENCE.md#武器命中动画有限冒烟)。此前结算包通过近战四项效果、伤口刷新／背压／到期、Drain零、左右手弓箭与过量伤害吸取上限、保存新进程重入及伤口击杀归功代表冒烟，实际结果及中间失败见[武器命中效果证据](../gameplay/items/EVIDENCE.md#武器命中效果有限冒烟)。前包双手剑、剑盾、双剑、职业说明差异及保存重入见[双手剑与左右手证据](../gameplay/items/EVIDENCE.md#双手剑与左右手有限冒烟)。更早的单机服务端动画补齐见[服务端动画证据](../gameplay/skills/COMMON.md#服务端动画补齐有限冒烟)；IAS／FCR／FHR／FBR断点、GH／BL、圣盾及双客户端TCP/IP见[动画断点证据](../gameplay/skills/COMMON.md#动画断点有限冒烟)，本包未重做全部断点及TCP路径。前包的玩家装备、防御技能、动作伤害／命中、四抗、破损／副武器组及保存重入覆盖见[人物属性证据](../gameplay/characters/ATTRIBUTES.md#当前包有限冒烟)；佣兵数值、装备、抗性封顶及保存重入见[佣兵证据](../gameplay/characters/HIRELINGS.md#当前包有限冒烟)。技能描述、耐久提示及启动测量分别由[描述专题](../gameplay/skills/DESCRIPTIONS.md#当前包有限冒烟)、[经典HUD](../gameplay/ui/CLASSIC_HUD.md#装备耐久提示)及[开发指南](BUILD_AND_RUN.md#启动测量)维护。

前包通过单机、双客户端TCP/IP及自研独立服务端接PvPGN后端的列出命令路径，范围与包身份见[命令分流有限冒烟](../modules/NETWORK.md#命令分流有限冒烟)。本轮未启动PvPGN／原D2GS，独立服务端只完成构建和打包；NoDrop概率、梦魇／地狱人数倍率、全部职业特殊动作、装备／套装／孔内组合、完整怪物行为、跨机器、Linux和性能尚未全面验收；其他版本的历史成功不能替代当前包验证。

## 本地诊断构建

2026-10-12为原版Join／自研Host物品消息对照构建Windows Release目标d2x，`build/bin/d2x.exe` SHA256 `34BA7883B448FCBEB01E70E5451764E8A8DACACFABDF7BD22E88C1D06FFC78E3`。包含双向客户端／宿主消息观察、职业装备预检／保留Cursor及旧自动地图分流源码；未打包，独立服务端EXE及`dist/current`不变。新诊断入口、PS语法和diff空白静态核对完成。

用独立Hero角色库启动127.0.0.1:4000、普通难度8人TCP/IP房间，宿主到ProtocolReady、failures=0，两个日志取得方向明确的原请求／原回包；本次只准备人工对照，未执行物品操作或认证原版Join。证据与特殊角色副本在忽略目录`artifacts/item-protocol-20261012-080843`，客户端及宿主日志分开；原版人工操作结果归[物品对照](../gameplay/items/EVIDENCE.md#职业装备与原版拒绝对照)。自动地图旧侧文件／D2S保存往返、攻击问题及单机心跳未在此构建复验。

随后原版Join报告取物无反馈，原日志确定卸装已提交但回包丢失来源body／x，补齐来源字段后重新构建当前工作区。修正版EXE SHA256 `5B82BE77B893472E4BEBC30EB19B367936B28A685230AA5B26E522CEA122BDD4`，固定运行副本及DLL放`artifacts/item-protocol-20261012-090747-corrected/runtime`，避免运行时占用共享构建输出。该目录记录自有客户端卸装／放置／拿取／重装及异职业预检的有限结果、重新确认含爪的隔离存档和恢复的Host；原版Join随后取得拿取／放置／卸装／重装的回包与权威提交证据，用户确认鼠标显示及操作正常，异职业装备由原版本地禁止。同一副本随后D2X Join原版Host取得相同七条物品请求字段、红色职业预览／点击拒绝及正常保存／新进程重入证据，目录`artifacts/item-protocol-20261012-093315-original-host`，旧放置失败在该组条件未复现。全部容器／存档组合未认证，历史失败也不据新成功确定根因。具体范围及原副本实际没有ktr的准备错误见[物品对照](../gameplay/items/EVIDENCE.md#职业装备与原版拒绝对照)。此次未打包或提交Git，也不认证工作区其他进行中的玩法改动。

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
