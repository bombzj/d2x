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

构建已有EXE后，在项目根目录使用 `Play.cmd`，或运行 `build/bin/d2x.exe`。显式 `--mpq <目录或文件>` 优先；未指定时，依次搜索工作目录本身、EXE 所在目录本身、工作目录的 `assets/mpq2`，再搜索 EXE 所在目录及最多四级父目录中的 `assets/mpq2`。自动定位以 `d2data.mpq`／`D2Data.mpq` 为入口，找到首个目录即停止，挂载该目录全部 MPQ，不递归搜索子目录。无论是否显式 `--mpq`，另在工作目录、EXE 所在目录及其最多五级父目录中查找 `assets/mpq`：其中的补充包最先挂载，优先级低于 `--mpq` 目录，原版成员始终优先，补充包只填补原版未提供的路径；两目录指向同一位置时不重复挂载。工作目录与 EXE 所在目录可以不同。MPQ 不参与编译，但运行时必须可用。源码入口为 `CMakeLists.txt`、`cmake/` 和 `src/`；许可和素材来源见 `LICENSE`、[第三方说明](../resources/THIRD_PARTY.md) 及 `docs/licenses/`。

`--lang <eng|chi|deu|fra|esp|ita|jpn|kor|pol|rus 或 0-9>` 覆盖语言：字符串取自 `data/local/lng/<目录>/` 的原始 string／expansionstring／patchstring.tbl，缺失回退 `eng`；未指定时读 MPQ 的 `data/local/use`。字形取自 `data/local/font/<语言目录>/`（西语系共用 `latin`，中日韩各用 `chi`/`jpn`/`kor`）；所选语言只提供 `latin` 字体时回退 `latin`，此时该语言的非 ASCII 字符无字形可绘。原版五个基础 MPQ 只含 `latin` 字体，中日韩界面需资源包内含对应 `data/local/font/<chi|jpn|kor>/` 字体；`Play.cmd -Lang chi` 转发该参数。字形纹理按需上传：字体表可含上万码点（中日韩 font16 约 16651 帧），仅在实际绘制时上传对应 DC6 帧，避免启动时逐帧建纹理。散装数据目录可用 `d2x_assets <data目录> pack-dir <前缀> <新.mpq>` 打成 MPQ 后挂载（成员名为前缀加相对路径）；繁体中文优先取原版 MPQ 自带的 `data/local/lng/chi/` 文本，仅当原版缺 `expansionstring.tbl` 与 `data/local/font/chi/` 字形时才补入 `PD2/Data` 的对应内容，不使用 SOE 资源；该缺口由 `assets/mpq` 的补充包自动补齐（如 `pd2-chinese.mpq` 提供 chi 的 `expansionstring.tbl` 与 `data/local/font/chi/`），否则这些文案回退英文。ESC 菜单与选项页标签按原版 `%s\UI\%s\...` 形式读 `data/local/ui/<语言目录>/<名称>.dc6`，所选语言缺同名文件时回退 `data/local/ui/eng/`；本机原版 MPQ 中各语言标签图与英文逐字节相同，非英文标签图须由资源包提供。

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

### 当前镶嵌材料换拿修正包

2026-10-19，基于`15f46357`及已有未提交工作区，Windows／MinGW／Ninja Release的`d2x`构建通过（4个增量步骤），现有`package.ps1`更新`dist/current`。修正符文／宝石／珠宝对非空孔目标错误选择镶嵌、移除背包拖拽操作提示卡；独立服务端未重建／打包。前一客户端备份于`artifacts/inventory-swap-20261019/previous-client/`，原MPQ与用户存档保留。下文旧包记录保留各自历史身份。

| 当前客户端程序 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 35292818 | `228C6458B5C5571F82C550E3961D337D19214B4AA2F9FB32CA08986BDD894872` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

直接运行上述包内EXE、当前五个MPQ、英文、1280×816、嵌入宿主原字节协议及新建独立SwapSmoke存档。管理生成符文／宝石／珠宝与两顶帽子、原物品请求摆放仅用于准备条件；经既有ui-input连续按下／松开执行真实控制器，确认符文↔药水、宝石↔卷轴、珠宝↔药水、符文↔无孔／满孔帽子及已装备帽子两次镶嵌，原回包均Updated；实际查看同帧手持截图，未出现自制操作提示卡。正常quit保存后新进程重入，物品位置及两颗孔内符文恢复，两次退出码均为0，宿主failures=0，缺图单位0、mapErrors为空。具体操作与限制见[包裹界面](../gameplay/items/INVENTORY_UI.md#操作)。

本轮仅认证列出路径；未覆盖全部堆叠／装书、仓库／方块／交易、未鉴定／损坏目标、多人／原服、音频或Linux。没有新增测试脚本／用例／专用程序。构建／打包日志、包身份、原包日志、隔离存档、截图和重入快照位于`artifacts/inventory-swap-20261019/`。原大面板及怪物的既有运行证据仍只认证各节所列旧程序。

### 当前客户端大面板修正包

2026-10-18，基于`15f46357`及当前未提交工作区，Windows／MinGW／Ninja Release的`d2x`构建通过（43个增量步骤），保留非致命警告；现有`package.ps1`更新`dist/current`。本包在前轮大面板修正上加入物品放置的原版双轴补偿与背包关闭按钮凹槽定位，详情见[包裹界面](../gameplay/items/INVENTORY_UI.md)。本轮只重建和打包客户端，未启动游戏，等待用户实测；独立服务端保持下节的程序身份。上一客户端程序备份于`artifacts/inventory-alignment-package-20261018/previous-client/`，原MPQ、旧包及用户存档保留。

| 当前客户端包内程序 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 35292836 | `2ACAEEF28D5A64DD15537A13EE49D65F4943ABD9A6D924993BCD764507DA2A3F` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

前一客户端（35292728字节，SHA256 `8D4C88BA71FBC6B0808C5D521094512CE76EBCA324F73B6B97EEF097FDBE2E78`）已有有限UI冒烟：直接启动其包内EXE，使用当前`assets/mpq2`、英文、1280×816和独立UISmoke存档；通过嵌入宿主的原字节协议、既有调试输入及本进程窗口的原生键盘／滚轮消息完成。覆盖人物／女巫技能按压、底栏组队／日志映射、M日志、背包装备拖放、金币丢弃／存入、仓库及任务／佣兵／传送点／ESC面板，实际查看截图。正常quit后新进程重入确认人物／技能／物品位置及金币保存，再次正常quit，两次退出码均为0，宿主failures均为0、无缺图单位或地图错误。该证据不覆盖本包落格半格边界与关闭按钮的新定位；具体操作与未完成边界由[经典UI](../gameplay/ui/CLASSIC_HUD.md#主要面板对照边界)维护。

传送点远距离接近曾超时，不能记作导航通过；改用管理旅行准备近距离条件后确认原0x63面板，只认证面板。`--hidden`仍建立真实OpenGL上下文但不初始化音频；未覆盖实体键盘人工操作、其他职业／窗口尺寸、多人／原服、Linux或完整原版逐像素对照。没有新增测试脚本／用例／专用程序。前轮构建／打包、运行与截图证据位于`artifacts/ui-panel-package-20261018/`；本轮构建／打包日志及程序摘要位于`artifacts/inventory-alignment-package-20261018/`。

### 五幕常规怪物关键收尾包（历史客户端、当前独立服务端）

2026-10-18，基于`15f46357`及当时未提交工作区，Windows／MinGW／Ninja Release客户端`d2x`与独立服务端`d2x_pvpgn`构建通过，保留非致命警告。该轮两包纳入[台账01至122项](../gameplay/monsters/ACT3.md#逐项接入)，新增HellMeteor／投石机落点与飞石、NpcBarb、王座Baal波次／Diablo命令恢复、迷乱随机借用，以及实机发现的CltDo43附着表现和目标节点诊断修正。使用现有打包脚本更新两包；旧程序／配置已备份，原MPQ和用户存档保留。其客户端身份已由上节替代，原有限怪物运行范围仍只认证本节程序。

| 该轮包内程序 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 35277480 | `395F80C4C95E6472D8F25941703A3857F349F9D27B0900FCB8DD10BBBD23C3B3` |
| `dist/server/d2x_server.exe` | 27871104 | `5539A3A444D00B2B9462577278082612B99F9CB438ED58E8B67DF7AA5DA71E11` |
| 两包`d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

实际启动最终包内客户端，使用当前`assets/mpq2`、隔离PackHost存档（90级女巫／普通难度）及`--hidden`图形上下文：进入库拉斯特海港75，管理旅行到血腥丘陵110推进200帧，再返回75推进250帧；ProtocolReady，nativeMapReady／movementAvailable／playerDisplayed均为true，缺图单位0、宿主failures=0，正常quit退出码0。实际查看城镇、野外及HUD截图。自然预设act5barb1登记节点8、连续A1攻击deathmauler1，敌方实际选中并使其受伤／死亡；修正后的406附着弹跟随投石机且超过普通表寿命仍保留，effectLimitations清空。首次旅行因区域准备返回Unavailable，检查准备状态后重试成功，不计为成功旅行。服务端EXE无参数输出用法并返回约定的2，只验证装载和参数入口，未连接PvPGN后端。

本轮没有新增测试脚本／用例／专用程序；管理旅行不认证正常战役通行，hidden不覆盖音频。未验证全部怪物／技能、其他投石机弹药、迷乱战斗／背压、王座五波和Diablo卸载恢复、完整随机／多人或Linux；不能据此宣称五幕全部复刻完成。完整NPC／MapAI及其他重建缺口见[人口](../gameplay/monsters/POPULATION.md#公共出生压缩与特殊恢复)，钥匙／红门高级事件继续暂缓。构建、前后产物摘要、首次发现及修正后的快照／截图／退出证据保留在忽略目录`artifacts/campaign-package-20261018-01/`。


### 后三幕怪物历史收尾构建与冒烟（55606a14后）

2026-10-18，基于`55606a14`及本批源码，Windows／MinGW／Ninja Release的`d2x`与`d2x_pvpgn`构建通过，保留非致命警告。补齐新增文件的直接类型依赖、变量作用域及QuestStatusFact诊断名称；实机暴露的问题已修正：原AC赋值代次、缺可选palshift表的怪物显示、110级怪物经验准备、原表倒置生命界限，以及无攻击槽的SuicideMinion管理准入。

| 该轮构建产物 | 字节 | SHA256 |
| --- | --- | --- |
| `build/bin/d2x.exe` | 34614805 | `E49CFB497E3ABF01DC1AAC59FB28EC09225976B11CEBC7B943D50950E7B455B1` |
| `build/bin/d2x_server.exe` | 27275662 | `E2BF851AF9E267F080F25BEEA05250201A569630D58D1B653FA9C905DE087481` |

使用当前`assets/mpq2`、隔离PackHost女巫90级／普通难度／种子1，真实EXE以`--hidden`建立图形上下文，通过既有调试管道和原客户端请求操作；生成、补资源及旅行只用于准备条件。证据在忽略目录`artifacts/act345-closeout-20261018-01/`，构建日志为`artifacts/act345-monsters-20261017-01/build-closeout-*`。

| 实际观察 | 程序及范围 |
| --- | --- |
| 最终EXE入场／显示／自爆 | 管道`act345-last-18`：修正可选调色表后，吹箭Fetish场景外观不可用为0（前版为13）；SuicideMinion准入后自行进入DEATH／DEAD，步进未阻塞。未隔离认证自爆伤害／碰撞／随机 |
| 最终EXE费用 | Succubus施加Blood Mana；Fire Bolt扣2.5生命，Teleport扣23生命，法力均保持583。余3.5生命时Teleport后变为1生命、状态114撤出；无诅咒时Teleport保持529生命并扣法力（12帧后561.34765625，含回蓝） |
| 最终EXE Mephisto／移动 | 观察到PrimePoisonNova（192）与角色受伤；原图可绘制、无地图错误。普通移动从(5229,4724)到(5227,4724)，停止后位置一致 |
| 最终EXE保存／新进程 | 正常退局后管道`act345-reopen-18`重新启动同一EXE；金币200204、精力220、属性点0、右技能54、生命529／法力583一致；入局failures=0、无缺图／地图错误，正常退出码0 |
| 中间构建`1B282A9A` | UberIzual准入并施放冰甲／新星，公开chillingarmor状态；MinionSpawner1保留原表两界限72／50并实际生成Minion1；VileMother生成三个幼体；Witch施放SuccubusBolt，FrozenHorror观察到普通攻击；Imp实际骑乘SiegeBeast，双向sourceunit及attached发布，坐骑死亡后撤出并恢复移动 |
| 中间构建`C3DF49AE` | Izual施放Frost Nova，Mephisto毒云／受伤及原图有限观察；只认证该中间程序，不能替代最终EXE逐项重测 |

前述中间构建不计为最终EXE全家族复测；ReanimatedHorde本次死亡样本未触发自复活。仍未验证全难度／全家族、原DLL逐随机轨迹、完整房间卸载恢复、多人TCP、冰甲反击伤害／随机及跨房传送、Blood Mana精确零生命与全部充能／光环／召唤／背压组合。Clt54连续发射仍缺；Mephisto攻击音效存在Compound组解析限制，另观察到identity298脚步FsOff未建立；音频设备因hidden未测试。独立服务端仅构建，未启动PvPGN后端或原D2GS。

该轮未新增测试脚本／用例／专用程序，也未打包；随后打包及最小冒烟见下节。构建包含交易邀请标题改动（`trade_invite_view.*`及NETWORK文档），未做该界面专项验证。第三至第五幕源码／未完成项见[逐项台账](../gameplay/monsters/ACT3.md)，不把本轮有限冒烟标为完整复刻完成。

### fb7cb723客户端与独立服务端打包

2026-10-18，源码`fb7cb723`，Windows／MinGW／Ninja Release增量构建`d2x`、`d2x_pvpgn`均为no work to do；现有两份打包脚本已更新`dist/current`及`dist/server`。本轮无需新增编译修复，前轮修复与较广运行范围见上节。

| 该轮包内产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 34614805 | `E49CFB497E3ABF01DC1AAC59FB28EC09225976B11CEBC7B943D50950E7B455B1` |
| `dist/server/d2x_server.exe` | 27275662 | `E2BF851AF9E267F080F25BEEA05250201A569630D58D1B653FA9C905DE087481` |
| 两包`d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

包内程序摘要与`build/bin`一致，旧程序和配置备份于`artifacts/client-package-20261018-01/previous`，原存档／MPQ保留。直接启动包内客户端，读取隔离PackHost副本、当前MPQ，以内存单机入场库拉斯特海港（75）：ProtocolReady、场景／玩家可显示、地图错误为空、不可用单位0、宿主failures=0；通过既有管道quit正常结束，进程已退出。证据在同目录`online-status.json`、`server-snapshot.json`及启动日志。

这次仅按要求做入场／显示／退出最小冒烟；hidden跳过音频，未做战斗、多人、独立服务端启动或全家族验证。第三至第五幕进度仍见逐项台账。

打包后的第73项底座已提交并推送为`bd540bf9`；该版Release客户端SHA256为`DD9A7AC035C0514C9FFEF3045E15A3FC32D31AB5954FAEC6E0105BE8F1E0233F`，服务端为`91A191A606318E559711FA15D335549FFFDDC2EE31CAA1238547B19C2C62B180`，隔离角色最小入场／显示／quit退出码0，证据见同目录`source-smoke`。随后第73至74项Diablo／clone、Nihlathak及共用Clt54接入后的源码，Release双目标最终构建通过；客户端SHA256为`D04062EF902AD9B8ECB805C5DEFF9C1E29A6F9BBE1B3C6F400B23F75A9447EF3`，服务端为`7D0B734BA7247D3E1B01130CFCB556ABC715521E936305265F136973E5C5A962`。证据见`artifacts/act345-monsters-20261017-01/item74-verified-build.log`及`item74-build-result.json`；该版尚未启动或重新打包，不能沿用前者的冒烟结论，也未认证新增首领战斗或后三幕全部完成。

### 当前逐房间地形构建

2026-10-17，基于`0bc3c264`后的本批源码，Windows／MinGW／Ninja Release的`d2x_assets`、`d2x`、`d2x_pvpgn`构建通过，打包前增量为no work to do。宿主不再消费固定全区投影的地形／活动随机，改由每幕房间会话随实际初始化、引用传播及释放物化；入场／旅行07改为目的房间加有序近邻。已用既有脚本更新客户端和独立服务端两包，包内EXE／DLL与构建产物摘要一致，旧程序及配置已备份。

| 构建产物 | SHA256 |
| --- | --- |
| `dist/current/d2x.exe` | `7724F0DAEE630D74FB6D065CF26F77813235DE613630C91E1355CB4F9FB40B47` |
| `dist/server/d2x_server.exe` | `BB354A27C608716E172CE1CB601CC1F1722320BBD80081F7A0A538787198C465` |
| `build/bin/d2x_assets.exe` | `2332246481E4E1A90EAFFC8EB8B02C7783EB33D7601DCE4A42EA38D347801185` |

构建EXE先以隔离MapTraceS、当前MPQ、seed=1普通验证4区南侧探索／北侧返回后的实际卸载与重入；城镇35个逻辑房间仅9个活动，初始07为目的房间加9项近邻。随后直接运行包内EXE，以隔离PackHost／PackGuest通过127.0.0.1临时端口完成双客户端TCP/IP冒烟：

| 路径 | 实际观察 |
| --- | --- |
| 双人入局／探索 | 同种子、不同GUID，聊天送达；两人分别沿南／北路线探索至目标房间，两客户端及权威各1,600格与原版静态投影一致，问题格均为16 |
| 留守／卸载／重建 | 宿主回WP、客人留守时目标保持加载；客人正常退局后loaded=false、随机为空；宿主北侧重入creationOrder由49增至103，两端1,600格仍一致 |
| 自然单位／技能／库存 | 原火弹击杀自然goatman1，生命27→0且rewardComplete；稀有飞刀经原take／place确认从背包(2,0)移至(5,1) |
| 保存／新进程恢复 | 双方正常69退局返回CharacterSelection、error为空；两个新进程单机读回90级角色，宿主金币200197及飞刀新格／词缀、客人金币200074及原格均保留，地图就绪、failures=0，再正常退出 |
| 独立服务端包 | 隔离配置挂载原MPQ；不可达D2CS明确报错并以1退出。未连接实际PvPGN后端，不算注册／建房验收 |

旅行至WP、补资源和宿主金币增加123仅用于准备条件；探索、施法、物品操作和退局均走正常原请求。截图确认真实画面，各宿主failures=0，所有自有进程已退出。完整地形结果由[地图验证](../gameplay/world/MAPS.md#实机首差与合成调用边界)维护；未认证原版Join、全种子、完整单位随机或门／墓墙任务。

构建及原版对照保留于`artifacts/map-room-runtime-20261017-01`；双包备份、包内冒烟、存档与摘要保留于`artifacts/package-smoke-20261017-01`。首次构建EXE预检心跳超时，不计通过；包内客人提前加入空宿主获得明确拒绝，开局后新连接通过。首次隔墙火弹未观察扣血，移至无遮挡侧后正常击杀，未据单次请求认证命中。构建同时修正人口迁移遗留的三处编译错误，保留非致命初始化警告；未新增测试脚本、用例或专用程序，原存档与包内server.json保持不变。

### 此前自然生成规则双包

2026-10-17，基于`0bc3c264`及当前未提交工作区，Windows／MinGW／Ninja Release的`d2x`、`d2x_pvpgn`构建成功。本包在既有人口实现上收尾自然怪物与随机物件／关联出生规则：保留真实类型的出生掩码、附加占位及动态占位；DS1环境控制器／非交互中立预设进入实际出生流程；普通NPC执行原点／参数4重试，Init62囚犯按激活顺序执行25次／最多5人／最多3组；补治疗祭坛替换、waypoint房间标记及零售特殊地图预设排除。具体原函数、保留的原空分派和暂缓边界见[人口](../gameplay/monsters/POPULATION.md)与[物件](../gameplay/world/OBJECTS.md#自然生成源码边界)。

| 当前产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 33636922 | `1A3D84D2D3D69FE9BB73E8F8F6ADF00DFF30F631D7411DBE12A9977CB315313E` |
| `dist/server/d2x_server.exe` | 26325972 | `ABC02504F7A3027E082840D82E0185FE430EC8ADC4A787297C125949BA6A2C4A` |
| `两包/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

两包由现有`package.ps1`／`package-server.ps1`更新，打包时EXE／DLL摘要与当时`build/bin`一致；原存档、资源和服务端配置保留。本轮先因内存压力中断8路构建，经2路恢复后以4路完成剩余编译；最后5步增量将城镇非交互中立预设修正同时纳入客户端和独立服务端，保留已有非致命初始化警告。只构建／打包及核对产物，未启动游戏、运行测试或新增测试程序；Waypoint出生保护及MF现状不因编译成功获得完整实机认证，逐随机流、GUID及完整休眠仍列后续。日志、源码摘要／补丁、只读表／反汇编及旧双包程序备份在`artifacts/population-rules-20261017/`。

### 当前地形资源工具复核

2026-10-17，基于68237e72及当前工作区，d2x_assets的Windows／MinGW／Ninja Release构建通过；原版d2mapapi_piped以VS2022 Win32 Release／静态CRT重建。保留既有非致命初始化警告。

| 资源工具产物 | SHA256 | 本轮用途 |
| --- | --- | --- |
| build/bin/d2x_assets-parity.exe | 5255786A1B0F1BCE9B9730636922E2C404F82E8CBE7F21B4E2679FD0A46D9500 | 同一既有资源目标的临时输出名，固定修正后的地图机制；486历史组合和130区完整邻房明细重放 |
| build/bin/d2x_assets.exe | 2D0303FF8F9D695C87B9074F2B0D0E19AFD8B7982CAE7A9FF8D98228FC3A4400 | 同一修正后的地图机制，额外增加ownedOnly诊断明细选项；6个大区域重放 |

对照结果见[地图验证](../gameplay/world/MAPS.md#当前源码逐事件复核)，报告及工具／MPQ／源码摘要位于`artifacts/map-parity-20261017-01`。该专项当时仅构建资源工具；随后客户端工作区改动已纳入下方ED442FC0包，资源工具对照不替代客户端画面验收。

后续调用链排查基于`0bc3c264`及当前工作区，复用既有资源目标的编译／链接命令，将对象和EXE隔离输出至`artifacts/map-call-chain-20261017-01`；链接依赖摘要前后相同。`d2x_assets-current.exe`的SHA256为`1D9D82212A7C9A2A67472C46F7A1EE8F978E4D4306273B34A5E0B9CEE4EEB837`；原DLL导出器扩充DRLG事件后的`d2mapapi_piped_chain.exe`为`51BE8BA5BB54926E203E10C501A388BB574434DC07FE31909B7A7942BBD960D4`。该专项实际启动原版Game.exe，核对同进程单机服务端／客户端及原DLL／D2X资源重放；没有另构建或打包D2X客户端／服务端。命令、源码及原版二进制身份见该目录，结果与已知宿主差异见[实机首差](../gameplay/world/MAPS.md#实机首差与合成调用边界)。

### 此前自动地图显示客户端包

2026-10-17，基于`68237e72`及当前工作区，Windows／MinGW／Ninja Release的`d2x`目标构建成功，最终增量确认返回`ninja: no work to do`。本包纳入自动地图大小／定位、侧栏／裁剪、DC6基线／城镇拼图、原PL2 Fade和方向键事件操作，以及当前工作区的地图、人口、物品、玩法和原版互通修复。显示依据和待验证边界见[自动地图](../gameplay/world/AUTOMAP.md#输入与选项)；保留非致命编译警告。

| 当前客户端产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 33586891 | `ED442FC077BBC43149AD8B2C93165AFBB79DE741E0F5ADD16F873B659025905C` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

使用现有`package.ps1`更新客户端目录，保留原存档、截图和资源；独立服务端本轮不更新。首次链接遇到构建期间共享源码更新造成的旧符号引用，按当前定义完成322步增量重编译后链接通过，未为打包修改玩法实现。构建／打包日志、源码状态、产物摘要及前一9086包备份保留在`artifacts/automap-package-20261017-085427/`。本次未启动游戏、运行测试或新增测试程序；自动地图画面、按键、混色性能及工作区其他改动等待用户测试，不将构建成功视为原版视觉一致性验收。

### 此前原版互通崩溃修复客户端包

2026-10-17，基于`68237e72`的当前工作区，Windows／MinGW／Ninja Release的`d2x`目标184步构建及随后49步增量构建成功。本包包含原版Join的技能节点删除修复、房主关闭时的退出顺序／重复结束包修复，以及工作区的地图、人口、物品和玩法改动；回归提交、原版DLL／收包依据及源码修复边界见[联网记录](../modules/NETWORK.md#tcpip有限冒烟)。保留此前补齐的companions Actor前置声明，以及AI／物件实现文件的effects／world头文件依赖。构建保留非致命警告，不将编译通过视为这些玩法改动的运行认证。

| 当前客户端产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 33201431 | `9086A86BF884CEEB7776539D0465DC1B35B6BC72C5A98A33FA341F3EBE5629D4` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

客户端包由现有`package.ps1`更新，程序摘要与`build/bin`一致；根目录`Play-Diagnostic.cmd`使用同一构建产物，可继续原目录角色与双向消息记录。前一包`033EB7AC`的程序及DLL备份在忽略目录`artifacts/original-host-exit-crash-20261016/previous-package/`，本次构建日志、最终摘要及退出崩溃分析也在该事件目录；更早的入场修复证据保留于`artifacts/original-join-crash-20261016/`。用户已确认033包原版Join能进入场景，随后房主关闭出现D2Net崩溃；当前9086包的退出修复尚待用户重测。本次只构建与打包客户端，未启动游戏、运行测试或新增测试程序；完整装备／技能及工作区其他玩法回归未验证。独立服务端沿用下方此前包身份。

### master 合并交付（此前包）

2026-10-16，`master`合并提交`23628ee2`同时包含`battle`的`5ef7d867`及最新`origin2/master`的`18c7b495`，运行源码为`9afc1ed0`。物品提示冲突保留当前MPQ格式／数值逻辑，库存保留原图定位／镶孔／无形绘制，同时接入Unicode字体与动态布局；修正旧字体消费者、商店／传送点缩放缓存，并保留玩家敌意姓名和轮廓字体后端的MPQ文字颜色。Windows／MinGW／Ninja Release的all目标581步构建通过，最终`build-confirm.log`为`no work to do`，两包EXE与构建产物SHA256一致；既有非致命警告保留。当前CMake无已注册测试，未新增测试脚本、用例或专用程序。

| 当前产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 33142811 | `AD13F909CD517A407E1A347AF544A933DE87C76143D72865BA843AD19078B7F5` |
| `dist/server/d2x_server.exe` | 25911211 | `BE699D0B9E2587786E0BC92EA7AC82D979CCE7DB1104F9B6700B071AB3BC6797` |
| `build/bin/d2x_assets.exe` | 10537972 | `95664BFE4CF703FC4419D36D2FC1426945225DC03E3BEB7D5BF024045C076E63` |
| `两包/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

包内客户端使用35级圣骑士隔离副本、当前`assets/mpq2`及低优先级`assets/mpq`补充资源，英语、1280×816、Single Player和既有调试管道完成有限冒烟。截图实际查看了主菜单、选角、库存原图／短剑提示、Charsi商店及跨面板商品提示、自然怪物／容器和魔法护身符蓝色名称／属性。自然沉沦魔被普通攻击击杀，白热对僵尸产生伤害及耗蓝；自然class140容器mode0→2、pending及掉落等待清零，落地护身符经原请求拾取并用鉴定卷轴鉴定为+2敏捷。管理旅行只准备合法近处位置，没有生成怪物／物件或直接扣血；屋外到屋内容器的请求曾按碰撞返回no-route，移到合法近处才交互成功，不据此认证跨障碍寻路。

原282支箭经Take／Place从背包(2,0)移至(3,0)，护身符占(2,0)。回城casts／pendingReleases／damage／spellImpacts／missileCount／eventsQueued均为0，正常保存／退局后新进程保留35级、10项基础技能、箭数量／新位置、已鉴定护身符及+2敏捷，卷轴已消费，临时动作为空。最终宿主failures=0、characterIssues为空、frontend.notice为空；正常离局返回CharacterSelection，主冒烟、重入及独立主菜单进程退出码均为0，原输入存档SHA256未变。

包内独立服务端用隔离恢复目录和空闲监听`127.0.0.1:46216`，现有D2CS／D2DBS接受注册并达到Game listener ready；包内停止脚本完成优雅关闭并取得退出码0，原后端进程保留。本轮未进行PvPGN角色实际入局或多人检查。

证据、源码／包身份、构建／打包日志、原游戏消息、快照、截图和隔离存档在忽略目录`artifacts/merge-master-20261016/`。两套本地MPQ均缺`data/local/font/chi/font16.tbl`，中文画面暂缓，未补造／下载替代资源；本轮不认证音频（`--hidden`）、窗口尺寸矩阵、Linux、全部技能／装备／区域／难度、掉落概率或原版全局时序。前次野外照明疑点仍未定性，旧现场保留于`artifacts/field-inspection-20261016/`，本轮有限截图不认证完整照明算法。

### 历史战斗动作与近战迁移交付

2026-10-16，`codex/combat-migration`基于`912dba85`，在独立工作区固定战斗迁移源码，未纳入共享目录同时发生的人口／任务等后续改动。当前可达动作／AI／移动／物件／物品时钟和旧近战消费者的迁移范围、逐程序D2MOO／1.13c差异及删除入口见[内核子系统](../modules/SERVER_SYSTEMS.md#调度与动作的职责及迁移边界)，不据此宣称整个R2／R3或完整原版战斗验收。

Windows／MinGW／Ninja Release配置及依赖构建通过；剥离混入的无关人口修改后，`isolated-repair.log`263步编译／链接通过，`isolated-final-build.log`4步通过。初次冒烟发现持续队列清除显式handler后，新增暗金怪的效果事件不能分派，固定步无法推进；同时修正玩家移动的同类问题，改用customId保留域身份及去重／撤销。`isolated-smoke-repair.log`最终7步编译／链接通过，d2x、d2x_pvpgn、d2x_assets均已生成；保留既有非致命警告。双包由这份独立构建生成，未使用共享目录未完成的build产物。

| 当前产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 32924153 | `E7AA4C4EA0B737F53C4456628729C0311A0FC48A6C38BB61CA3A75052F2BE5A3` |
| `dist/server/d2x_server.exe` | 25805018 | `AE963233BA866388090EA5D8DBF5648320FB3251BA23AF759BF05DF5336CA1CB` |
| `build/bin/d2x_assets.exe` | 10484121 | `02641471DB5B717885879BDDB1F31A1AC814FE17F266163CC86372FBE4236492` |
| `两包/d2x_bncs_legacy.dll` | 639578 | `A2B9AAD2522071A3000BE5F960814F27F167E04D7CE222CBCDF302D3A04EFD4B` |

最终哈希包通过现有调试管道完成Single Player有限冒烟：35级圣骑士牺牲起手持有原生近战联系，动作兑现后联系清空并扣目标HP；白热命中两个目标；冲锋移动、受击中断及重新施放伤害；普通移动到达请求坐标。84级德鲁伊狼人／狂怒多次命中、真实灵狼自主攻击及敌怪移动／攻击正常。两角色回城后casts／pendingReleases／damage／spellImpacts／missileCount／eventsQueued均为0，保存成功；新进程保留已学技能（德鲁伊30项、圣骑士本次学习4项），临时状态／宠物及联系未恢复。两个重入进程和德鲁伊主冒烟进程退出码均为0，最终frontend.notice为空、宿主failures=0、characterIssues为空。最初失败包及现场单独保留，未计为通过。

物件补查使用当前MPQ Objects原class111／OperateFn22／Parm0=750：正常交互使井水uses由2→1、mode由0→1并恢复HP，只登记一个AiThink补充事件；越过750帧后uses回2、mode回0且该事件消失。管理旅行、真实怪物／水井生成和补资源只用于准备条件，不认证自然通关、人口概率或掉落。

证据均在忽略目录`artifacts/combat-migration-20261016/`，包括构建／打包日志、包哈希、旧入口扫描及`smoke/`权威快照、原协议请求结果、日志和隔离存档。无新增测试脚本、用例或专用程序。`--hidden`创建真实OpenGL但不初始化音频；未验证画面／音频、Linux、双客户端、PvPGN后端实际入局、全技能／装备／难度／PvP／背压矩阵及完整原版逐帧／随机轨迹。独立服务端本批仅构建与打包。原MPQ、用户存档与原目录并行修改保留。

### 历史德鲁伊技能交付

2026-10-16，`battle`分支、构建时HEAD `ffd08b76`之上的德鲁伊工作区：全部30项及变形、动物／藤蔓／灵体和隐藏／客户端依赖接入后，按授权执行Windows／MinGW／Ninja Release CMake配置、all目标编译排错、双包更新及Single Player有限冒烟。`cmake-configure.log`配置通过，`build-frozen.log`395步全量构建通过；灵体接线后的`build-spirit-auras.log`290步通过，最终`build-fenris-finish.log`6步编译／链接通过。最终d2x、d2x_pvpgn、d2x_assets均有对应产物，既有非致命警告保留。未新增测试脚本、用例或专用程序；收尾源码与文档统一提交，提交身份以Git历史为准。

| 当前产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 32663176 | `5D37545A2BA39CD59580E8ADA53940C16113BFE0664831C9051F6F39D2478A81` |
| `dist/server/d2x_server.exe` | 25546549 | `EBB3538BFC4B2CCEDD5AFFB1A23C8FB6A80663F8BBC6B90722C34C42BC46F456` |
| `build/bin/d2x_assets.exe` | 10480345 | `5F67FB255701A59A0B6DB74E253F49ADB0270B141D544E248B5E8A17FC58B394` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

两包的`d2x_bncs_legacy.dll`一致。临时runtime7为客户端包相同EXE／DLL字节的隔离运行副本；MPQ只读当前`assets/mpq2`，不复制到分发目录。依据为本地固定D2MOO及原版1.13c DLL；D2MOO的Do117计数分支、灵狼攻击／追击判断采用同版本原证据裁定。完整30项台账、公共边界及数值／事件依据由[德鲁伊](../gameplay/skills/DRUID.md)维护。

84级隔离德鲁伊Hero的30项均通过正常学习请求取得并保存；管理经验／资源、真实怪物和地图旅行仅准备条件，不认证自然成长／通关。中间包已观察三系施放、形态／蓄力、真实宠物／上限／互斥、主人死亡撤出。最终runtime7专门重新验证狂狼接近／消费真实尸体并获得138状态（属性25=100）、Oak与两资源藤、Fury、Hurricane／Armageddon原图表现及回城清空；`final-town7.json`的casts／pendingReleases／damage／missiles／eventsQueued均为0。新进程`final-reload8.json`保留全部30项，临时状态与宠物均为0。最终刺客副本回归157→159互斥、影子替换和野外战士124自主三次释放251。最终两职业宿主failures=0、characterIssues为空，进程退出码0；没有将早期所有施法样本冒充在最终哈希包逐项重跑。

日志、原版分析、隔离存档、快照和截图均在忽略目录`artifacts/druid-implementation-20261016/`；包身份另保存于`package-identities.json`。中间runtime4一次后台进程消失未捕获退出栈，后续挂调试器及正常运行未复现，详见德鲁伊专题；不能宣称该次原因已定位。未运行Linux、双客户端、原版Join及独立服务端新技能实际入局，独立服务端仅完成构建与打包。`--hidden`建立真实OpenGL但不初始化音频，本次不认证音频。完整队列1／2、全局随机／物品事件交错及全部装备／难度／PvP／背压／同条件逐帧数值矩阵仍按[总计划](../architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)验收。

### 历史刺客技能交付

2026-10-16，`battle` 分支／构建时HEAD `ac16bd06` 之上的工作区，按授权在全部职业程序接入后统一进行 Windows／MinGW／Ninja Release 的 CMake 配置、all 目标编译排错、客户端与独立服务端打包及 Single Player 冒烟。最终源码修正的构建为 `build-release-repair18.log`，3 个编译和 3 个链接步骤成功；`d2x`、`d2x_pvpgn`、`d2x_assets` 均已有本轮构建产物，既有非致命警告保留。工作区还含此前物品、世界、NPC等改动；此包包含这些输入，但刺客冒烟不认证所有工作区功能。未新增测试脚本、用例或专用程序；本批源码与文档在收尾时统一提交，实际提交身份由Git历史维护。

| 当时产物（非当前包） | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 31927836 | `5FE0CA1AD7692A2F33C368054BD6AF645162AAA19BF03CA68174AA16BDA45BF6` |
| `dist/server/d2x_server.exe` | 24863126 | `545871680D0398AADDF999B0513661B1DE9DD79F56C772FC40AE92F5CF8A764A` |
| `build/bin/d2x_assets.exe` | 10402884 | `BD45AAEFCD11A93FCC65E9F298C559F2D32A8C11ADAEF23AB51DDAF658F574FA` |
| 两包 `d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

执行依据为当前 `assets/mpq2`、本地固定D2MOO与原版1.13c DLL。资源参数动态准备；原版裁定踢击预分配、心灵爆震转换／撤出、喷火nSkill及影子远距出生探测等版本敏感分支。具体源码与30项台账见[刺客](../gameplay/skills/ASSASSIN.md)。本轮排错还修复了双龙爪HUD公式上下文、Expansion分隔行导致的刺客装备职业识别、双爪ht2外观、非玩家实际AnimData动作事件、影子刀盾周期路由、刺客宠物原0x7A归属及纯单位技能赋值的空输出事务。管理旅行先激活真实目标碰撞房间，再寻找合法落点，没有跳过碰撞或授予通行资格。

隔离角色 `assassin-smoke-20261016.d2s` 为92级刺客Hero，30项基础技能均经正常学习请求授予；使用管理经验／属性／资源、原装备生成和原地图旅行准备条件，不计为自然成长或通关。双手实际装备 `ktr`、鞋子 `lbt`。普通难度，部分目标使用原 `/players 8` 设置，怪物经当前MPQ真实身份准入；管道 `d2x-assassin-20261016`。`--hidden` 仍建立真实OpenGL并可截图，但不初始化音频。以下武学／法术观察跨本轮迭代包；最终哈希包重点重新验证影子、陷阱上限、普通攻击终结与保存重入，不把每个旧样本冒充最终包重新运行。

| 路径 | 实际观察与边界 |
| --- | --- |
| 武学六种蓄气／四种终结 | 虎击、火焰拳、雷电爪、双蛇击、寒冰刃及凤凰攻击有原状态层数快照；双龙爪、12级龙爪踢、摆尾与飞龙在天实际释放、扣血并清除蓄气。凤凰一／二／三层分别产生流星、链电、混沌冰；飞龙在天改变落点后释放寒冰刃，有原图截图。未作全部取整／命中／吸取精确数值矩阵对照。 |
| 投掷／持续／防御 | 火焰爆震385、雷电网389和刃怒506实际推进；刃怒原Hold产生连续飞弹，Stop撤出状态12与待释放。速度爆发157被Fade159替换，Venom31、刀盾158、斗篷153有观察；斗篷暗化有截图。双爪被普通怪物近战命中时记录实际格挡动作帧534／968，不将此当作全部格挡类别认证。 |
| 六类陷阱 | BladeCreeper229／392、ChargedBoltSentry231／306、Wake237／281、Lightning262／313、Inferno294喷火、Death312／324实际出现；亡者守卫消费原尸体69，`lastCorpse=69` 且尸体不可再用。早期洞口样本未触发尸爆，移至真实开阔地并具备尸体后才计成功。 |
| 心灵爆震 | 正常怪物实际转换为owner=1／alignment=2；期限后恢复无主人／alignment=0且仍存活。心灵战锤也有正常请求与目标生命变化观察，但有其他行动者，不作隔离伤害断言。 |
| 最终包影子／公共属性事务 | 战士114的AI继续推进，自主释放继承的273及251，发射者为真实影子；大师133替换战士，自主蓄气280并放置276陷阱，原解散请求成功撤出。220帧批量推进完成、宿主failures=0、技能／输出等待清空；影子周期和技能继承未再重复阻塞。 |
| 最终包上限／离区／跟随 | 连续六次261放置，159进入life=0／removeAt=754，160–164保留五座有效陷阱；回城后陷阱撤出。战士165从野外远距跟随到城镇，主人(3993.5,4948.5)、影子约(3978,4947.95)，有截图；此管理定位样本不认证自然跨图通行资格。 |
| 最终包普通攻击终结 | 原254接触获得状态122；随后原技能0攻击，在帧422撤除122并使同一champion目标66生命1049.67578125→1038.828125。此样本验证执行链，不代替精确公式对照。 |
| 最终包保存／新进程恢复／退出 | 正常save／quit后新进程恢复92级、经验2000000024、全部30项基础技能及双爪body4／5、鞋子body9；临时影子／状态未持久化。最终回城后casts、pendingReleases、damage、spellImpacts和eventsQueued均为空，failures=0、characterIssues为空、无前端notice；原退局返回CharacterSelection，再正常退出。 |

证据、构建日志、快照、原版分析、隔离存档和截图均在忽略目录 `artifacts/assassin-implementation-20261015/`；最终包证据为 `warrior18.json`、`master18.json`、`unsummon18.json`、`trap-cap18-*.json`、`shadow-follow18.json/png`、`final-reload19*.json`、`attack19-*.json`、`final-exit19.json` 与 `final-left19.json`。原用户存档、MPQ、参考仓库和历史包未纳入提交或分发复制清单。

未运行Linux、双客户端／原版Join或独立服务端新技能实际入局；独立服务端本轮只有编译与打包，不能沿用前次注册成功认证新技能。未完成全局帧队列1／2、物品事件交错／全局随机、全部难度／装备／PvP／死亡中断／背压及同条件逐帧数值矩阵。当前通用声音仍有复合one-shot／部分怪物脚步门槛，hidden样本不认证音频；斗篷截图不等于整套原版调色。完整精确性阶段仍按[总计划](../architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)推进。

### 历史物品显示与生成收尾

2026-10-15在`battle`分支按用户授权完成Windows／MinGW／Ninja Release的CMake配置、首轮60步及收尾6步all目标构建，以及客户端与独立服务端两包更新。本次覆盖当前工作区的物品提示、库存原图／染色／镶孔、物品生成／掉落、管理命令及此前性能改动；非致命警告保留。未新增测试脚本、用例或专用程序。`ctest -N`列出的engine来自旧CTestTestfile，当前CMake没有对应定义，未运行或当作本轮通过证据。

| 当时产物（非当前包） | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 30277277 | `FE70B8F134EEFB53137F74EC951619E9AAA5EDBC21854F1B0EAD2D672928E9C5` |
| `dist/server/d2x_server.exe` | 23264551 | `FD3A02E534D19CB543C7CE9E43FFF259B2DCA11C44EDAB55BD8BAC6FAD3F3D22` |
| `build/bin/d2x_assets.exe` | 10291588 | `C39CA123E67DFD164E5040BBEE2EBD8EFBE32DC19634E53B806D9E974625FD76` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

最终6步纳入首轮构建后新增的药剂回调／非玩家FullStats门槛／快捷费用资源编码修正，随后重新打包并复验物品读入／悬停、药剂、施法及保存重入；最终源码输入SHA256与构建时清单一致。首轮详细物品操作对应的过渡包保留在证据目录`first-smoked-package/`，不以旧包替代最终包的验证。

有限Single Player冒烟使用包内EXE、当前`assets/mpq2`和隔离90级女巫副本。正常拾取、鉴定、镶嵌、Take／Place／Drop、装备／卸装及两次武器组切换均消费原请求／回包。观察了以下代表路径：

| 物品／路径 | 实际观察 |
| --- | --- |
| 6孔水晶剑 | 按红／蓝／紫／绿／黄／白宝石顺序镶满；原包6孔／6子项及归属一致，悬停显示孔底与原图，移开后只显示宿主。 |
| 4孔橡树之心连枷 | 按Ko／Vex／Pul／Thul镶入，原包符文之语20557及4子项一致；背包与副武器组悬停均绘制，后正常卸装放回。 |
| 2孔魔法水晶剑 | 原生记录193 Cruel／236 of Anthrax成功生成、鉴定并插入红宝石；后缀绿色保留，剩余空孔显示孔底。 |
| 1孔永恒边缘 | 原0x400000无形标志保留；鉴定、黄宝石镶入、悬停、Cursor半透明及放回背包获有限截图观察。 |
| 管理生成 | propertyQuery、preview、显式词缀实际生成通过；`properties`预览20% swing2得到of Alacrity／stat93=20，不合法999明确拒绝。条件准备不认证自然掉落。 |
| 保存／新进程恢复 | 正常save后69离局到CharacterSelection并退出；新进程恢复4件物品的格子、品质／鉴定、孔数、全部嵌入顺序、词缀、符文之语和无形标志一致，再正常离局退出。两个阶段宿主failures=0。 |
| 最终包资源／状态 | 生命509→516.3046875／HEALTHPOT状态100→529／状态撤除；正常寒冰装甲施放后法力406.40625，使用法力药剂后413，装甲状态10保留。带状态10保存后，新进程临时状态为空。 |
| 独立服务端 | 包内EXE使用隔离配置／恢复目录与空闲监听46180，现有本机D2CS／D2DBS接受注册，达到Game listener ready；stopFile正常关闭、退出码0。未停止原有后端。 |

最初词缀配置误把TXT的Expansion分隔行计入原生行号，被资格校验拒绝，依据propertyQuery校正后成功；一次过快物品请求被正常限流，等待后继续，未当作成功执行。独立服务端连接未启用的隔离后端地址时明确报D2DBS connection failed／退出1；改连现有开发后端后的注册／监听／退出才计成功路径。管理旅行在区域3的默认及指定落点均返回Unavailable，未绕过原碰撞；本轮未复验野外普通飞弹快捷费用／随机分支。失败证据与旧EXE均保留。

证据、截图、包身份和可供原版对照的4件复杂物品存档位于`artifacts/item-release-20261015-200027/`，角色文件为`saves/ShotSorceress.d2s`；原用户存档未写入。只覆盖上述有限路径，不认证全部属性程序／自然掉落概率、3／5孔与五幕逐像素、套装／特殊使用底色、多人、独立服务端实际入局或Linux。未取得用户原版截图，不宣称原版像素一致性已验收；此前性能数值仍以其原测量包为准。

### 当前性能修复与实测

本节性能与产物记录对应下表所列测量包。之后的药剂回调顺序、非玩家FullStats门槛及飞弹快捷费用增量编码修正已纳入页首收尾构建／两包，有限资源观察以页首为准；详见[源码语义核对](../gameplay/skills/COMMON.md#性能优化的复刻约束核对)。未重复全部性能窗口，不能将本节数值外推为后续源码、全部原生事件／随机顺序或野外飞弹分支的验收。

2026-10-15按最新授权完成Windows／MinGW／Ninja Release的CMake配置、all目标构建、客户端和独立服务端两包更新。最终增量构建通过；修正工作区物品显示代码误用`gems`，沿用现有`socketGems`字段。保留其他工作区改动及非致命警告，没有新增测试脚本、用例或专用程序。

旧包`CF852D1C…`和最终包在当前`assets/mpq2`下使用同一份隔离90级、15件物品女巫存档，原始SHA256为`C9AF443C5514126E29EA762AF589A8D79ECA94B6949694B95F2679CB5991E17A`；不写用户存档。证据目录为忽略的`artifacts/combat-performance-20261015-181045/`，包含旧包副本、10次主线程采样、各阶段候选包身份、快照、计时和图像。最终产物如下，旧检查点身份保留在下节。

| 最终产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 30250059 | `3F7B46E605DC9F45CA7760BB57BEF2A20E8488CD1F03CA41437B46FF6C8D3CAA` |
| `dist/server/d2x_server.exe` | 23257652 | `ACE542108B5AE631F546DD67847DBB0A6E94BF5330779CBA2F0959A271E64813` |
| `build/bin/d2x_assets.exe` | 10286225 | `5122FB5315CBB401364487C50FC8A2E5C2E909258D4325598A69CB8D25671542` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

实测根因及处理：旧包满资源空闲仍在每个STATREGEN中经资源轨迹与人物事务至少两次重算装备／被动／完整属性；10次采样都在这条路径。单步一帧约337–407毫秒、八帧约2.145秒，宿主连续追八帧放大了画面停顿。普通飞弹扣法力／推进随机游标也误走完整人物事务，候选包抓到每次约107毫秒尖峰；改为同一资源事务提交费用、拥有者随机游标和已准备的飞弹事实，保留聚气／死亡触发／复合费用的原路径。光环脉冲仍有峰值，进一步定位到非扩展属性叶子为大量不存在的属性分别建立compute／stored缓存；叶子现直接读BaseStats，扩展列表及操作求值规则保持原路径。

玩家普通再生读取已发布FullStats，仅提交资源变化；满资源不创建人物事实。实际状态撤出仍保留原有序回调。非玩家未变化列表和相同有序状态查询复用已存属性，Blaze仅跨格时求值，物品恢复到期才复制库存。常规追帧在完整固定帧边界按墙钟预算让出，保留帧债务；批量step为可查询／取消任务，真实完成才计数。管道重叠I/O独立于渲染，脚本按块接收、按任务进度等待；没有重发非幂等step。规则来源与工程边界见[公共机制](../gameplay/skills/COMMON.md)，操作语义见[调试管道](DEBUG_PIPE.md)。

| 观察路径 | 修复前／中间候选包 | 最终包 |
| --- | --- | --- |
| 同档城镇空闲effects阶段 | 旧包237.586毫秒／权威帧 | 0.0157毫秒；10.024秒推进251帧，约25.04 Hz |
| 同档画面稳态 | 旧包已复现八帧约2.145秒阻塞 | 均值16.683、p95 16.945、最大17.457毫秒／呈现帧 |
| 连续四次冰封球 | 中间包宿主峰值106.795毫秒 | 宿主峰值4.159毫秒；画面均值16.75–16.96毫秒，最大43.688毫秒，无百毫秒帧 |
| 圣冰／圣火／狂热周期宿主峰值 | 叶子优化前88.975／167.142／77.723毫秒 | 7.696／9.270／9.970毫秒；三组各6秒推进150帧，无百毫秒帧 |
| 250帧批量step | 旧包长批量曾阻断原包及超时 | 约4.670秒完成250／250，无失败 |
| 运行中大快照与任务取消 | 旧包大响应曾提前断开 | 空闲约21毫秒，任务中约41毫秒；取消在5／250帧生效，之后tick保持不动 |

光环使用隔离35级圣骑士与真实MPQ区域3；圣火／狂热阶段另外按当前MPQ人口准入两只`brute1`，用于准备附近目标，不认证自然掉落或出生概率。圣火目标生命实际下降，三种拥有者状态为43／35／49。女巫冰封球创建／到期清理正常，冰封装甲状态10、护甲32；法力恢复到上限后停止增长。同档10项基础／派生数值和有效技能映射与旧包一致。正常保存、退到CharacterSelection、退出及最终EXE新进程恢复通过，等级90、15件物品、基础技能和已选技能保持一致，宿主失败计数为0。

呈现耗时按最近120个完整帧统计，含正常限帧／垂直同步；同时观察各整段的累计百毫秒帧增量，避免只看p95漏掉周期峰值。最终比较在构建结束后运行，截图不插入正式计时窗口。中间包一次主动截图的GPU读回约150毫秒、该帧约162毫秒，归于debug阶段，不能算普通施法成本。冷启动／地图准备仍有一次性开销；上述结论只覆盖列出的Single Player场景，没有认证全部视觉、全部职业、大规模多人或Linux，也没有新增1.13c逐函数反汇编验证。

### 当前 battle 分支打包与最小冒烟

2026-10-15按用户授权完成Windows／MinGW／Ninja Release的CMake配置、all目标编译链接和两包更新。修复任务对象Ports、事件发布结果转换、旧减速字段残留、即时物品光环tick参数及飞弹公共函数的直接头文件依赖。非致命警告仍有保留，未新增测试脚本、用例或专用程序。

新包首次点击Single Player暴露当前Missiles TXT第568行pSrvHitFunc=`*12`的严格整数读取错误。服务端Do／Hit／Damage回调字段改从当前MPQ的missiles.bin读取原WORD，按0x1A4记录尺寸、行数和逐行Id复验TXT／BIN对应关系；其余数值继续严格校验。当前原BIN该Hit值为64948，不能解释为普通12或自行设零。

| 修复前battle检查点产物 | 字节 | SHA256 |
| --- | --- | --- |
| `dist/current/d2x.exe` | 29971295 | `CF852D1C8EF7C4A3AAB14C5C2F41380050D8779DA8A5EAC44F6EB3A28774DCF5` |
| `dist/server/d2x_server.exe` | 23105453 | `A6C66337D4E15B3BE4B4CAA78FA4AAF7D92F6E6B924B37F0D7343D457E07D697` |
| `build/bin/d2x_assets.exe` | 10227026 | `7BA65604FD0096735DCC62CCC0D064F51CA46D886C9F0DFD804A8FF9635DD367` |
| `dist/current/d2x_bncs_legacy.dll` | 639578 | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

有限Single Player冒烟使用包内EXE、当前assets/mpq2和隔离90级法师副本：主菜单点击Single Player进入选角；隔离副本正常入局并渲染营地；原协议选择／释放冰封球，tick119记录260主弹体与261子弹体共13个、法力389.9375；另一轮连续运行至tick705确认弹体清空、宿主failures=0。管理旅行只准备施法场景，未认证自然旅行／掉落／目标击杀。正常save成功、69退局返回CharacterSelection；新进程恢复90级、lifeRaw135424／manaRaw105728、临时states和missiles为空，failures=0，随后正常退局退出。

限制：一次120帧批量step造成调试请求、输出队列及心跳超时；连续运行的另轮成功路径没有宿主失败，但部分调试请求和截图响应仍超时。营地人物effects阶段曾记录单帧约195毫秒，仍需后续定位性能；远处管理旅行也可能因当前房间／碰撞准备不可用而拒绝。截图没有完整认证冰封球动画、命中表现和新区域加载完成，不将权威弹体记录等同视觉认证。未运行独立服务端联机、多客户端、Linux、完整数值或全部职业／飞弹程序验收。

实际产物身份、构建／失败及成功运行记录、截图、原BIN取证和隔离存档保留在`artifacts/battle-package-20261015/`；替换前的两包EXE／DLL保留在其`package-before/`。dist和上述证据均不纳入源码提交。战斗重构其余缺口仍见[内核子系统](../modules/SERVER_SYSTEMS.md)。

### 此前代表路径构建与冒烟

2026-10-15按本轮用户授权完成Windows／MinGW／Ninja **Release** CMake配置、all目标编译链接及游戏内冒烟。修复飞弹随机源可写接口、受击耐久事务种子提交、人物命中因子取值和雷云类型／头文件错误；非致命警告仍存在。最终all增量报告`ninja: no work to do`。未新增测试脚本、用例或专用程序。

本轮新产物在`build/bin`；下表仍记录未更新的dist分发包。客户端29063503字节，SHA256 `47A7BD8C12AFE2DD34F334EC3DB695BB4FAAA1B1EC82A1E0AB55F4CEC1E5B70F`；独立服务端22216926字节，SHA256 `F4F5B53184896930CE05B52B160B51D86DA36BAD182468E8E89AF2D6E2AC2821`。资源工具10182713字节，SHA256 `486BB29D61CE0EE46F3F5F77288CDB1B5D44452AC5A9DA175B862EF5841D02BE`。完整身份、构建、原版字节及运行证据保留在`artifacts/combat-smoke-20261015/`。

| 有限Single Player路径 | 实际观察 |
| --- | --- |
| 圣冰／圣火／狂热 | 两目标取得圣冰state44和三项−30减速并正确变蓝；圣火逐目标收到0x2D并显示fire_hit，冰伤为0x3D。狂热state49切换生效；停止后state44撤除，帧事件队列清空。 |
| 毒新星 | 显示绿色及MPQ poisonhit；重复命中不再卡在飞弹帧事件。存活目标tick788→888、生命306.84375→197.296875，毒state2到期撤除，弹体清空。该存活到期样本使用/players 8准备条件。 |
| 冰封球 | 观察到260主弹体与261冰弹、目标击杀，100帧后弹体清空。 |
| 正常离局／重入 | 圣骑士Save／69离局回CharacterSelection；新进程恢复84级、临时状态及帧事件为空。成功路径宿主failures=0。 |

冒烟采用当前MPQ与隔离历史角色副本，通过正常原协议选技／施放；管理旅行及生成怪物只准备条件。修正原States.colorshift的一基编号（原1.13c D2Client RVA6C607／6CDD1先减一），按原命中类别播放逐目标元素动画，并修复毒刷新将本帧待结算误判为逾期的等待。初次毒新星卡帧记录保留为失败证据，修复后重跑通过；不将此次观察扩展为全技能、多人、Linux或完整原版数值一致性认证。

### 历史期限模块分发包



当前Windows **Release**包包含实例帧事件底座、持续效果REMOVESTATE、迷乱／吸引独立AIRESET及实体撤出取消。CMake配置、all目标181步和最终增量7步构建通过，两包已更新。最终增量纳入持续事件的同帧派发及活冻结怪物跳过AIRESET的原版规则；后者由原1.13c RVA769A0核对，未做冻结组合运行认证。既有非致命警告保留，没有新增测试脚本、用例或专用程序。

| 产物 | 路径／字节 | SHA256 |
| --- | --- | --- |
| 客户端／嵌入宿主 | `dist/current/d2x.exe`，28442116 | `905CC8997446F55EF01BAC6CCAB21E0B1520F58456E4E04D9113070E17F8DC52` |
| 独立服务端 | `dist/server/d2x_server.exe`，21594803 | `7035723D3E05FCA85853322BC88302093712229E737BE6CAD4BB97C91A8DADA3` |
| 资源工具 | `build/bin/d2x_assets.exe`，10157293 | `0F9A378C15ED65F9AD6400572E2AEEFB82D6C6B674DF34C37D4F3D425CD612BF` |

用隔离35级死灵副本执行Single Player、正常技能／物件交互及新进程恢复。管理定位、目标／祭坛生成只准备条件；期限按实际逻辑帧观察，管理单步受背压时请求步数不等于已完成帧数。字段错误及资源异步准备不算成功路径，产物身份及正常玩法以实际包／状态为准。

| 有限路径 | 实际观察 |
| --- | --- |
| 进入游戏 | 新包正常渲染游戏，初始states为空、frameEvents.count=0，failures=0。 |
| 迷乱替换 | 单位90安装迷乱，分别登记状态移除与AI重置，均due882；虚弱替换后另登记due1002。881帧三个事件仍在；882帧旧两个事件消耗，AIRESET记录清空，原客户端仍收到虚弱state19／伤害−33。 |
| 虚弱到期 | 1001帧仍有一个事件；1002帧队列为空，原客户端state19撤出。目标保持7生命，failures=0。 |
| 吸引 | 选中单位90的状态事件和旁边121的独立AI重置均due1313；121先死亡，仍保留目标90的选择器／期限。1312帧事件仍在；1313帧两事件消耗，90的group9节点和121的残留选择器／期限清空。此例不认证全部活单位AI行为。 |
| 玩家祭坛期限 | 当前MPQ防御祭坛code6／2400帧；正常交互安装state128，登记玩家类型的REMOVESTATE due3714。3713帧仍在；3714帧状态及定时器撤出，人物raw生命23956不变。 |
| 保存／恢复 | 正常激活战斗祭坛state129并带一个未到期事件退局，返回Characters后退出。新进程恢复raw生命23956／最大24576，states／effects为空、事件总数0、failures=0；随后正常退局退出。 |

证据目录`D:/Projects/d2x/artifacts/frame-events-20261015-025625`保存两包身份、旧EXE、原DLL片段、构建／运行记录和隔离存档，原用户存档及MPQ保留。没有认证全部周期技能／物品光环、AI思考／动作、同步命中排序、冻结组合、容量极限、多人或Linux。事件类别底座已经接入，不代表完整原版调度或全部战斗复刻完成；后续大模块继续迁移，整块收尾后再编译／排错／打包。

### 历史原生伤害事件包

该历史Windows **Release**包包含持续效果的原生技能事件登记、伤害前回调及诅咒AI收尾；下表路径与摘要记录该轮打包时的产物，已由页首当前包替换。CMake配置与all目标构建通过；修复缺少直接包含／枚举名称后130步完成，运行修复后的最终71步完成。直接伤害使用没有MPQ定义的伪弹体，普通攻击曾触发`map::at`；现仅对实际飞弹读取ReturnFire，修复后的普通攻击完成。未新增测试脚本、用例或专用程序。

| 产物 | 路径／字节 | SHA256 |
| --- | --- | --- |
| 客户端／嵌入宿主 | `dist/current/d2x.exe`，28369073 | `12BE0967DE3BAAB7A98DBC9281D7813F8BF6EDB71BA3AD70034706F54D99DF09` |
| 独立服务端 | `dist/server/d2x_server.exe`，21524320 | `F7DD87E9DBB7D15EFF863047B01258D6EF852C2EA5CDC9DA865BF82D46D10533` |
| 资源工具 | `build/bin/d2x_assets.exe`，10157293 | `63B32648CDD44B6E55CFCD70046134F47BE7C1F276AD8AF0AEAFF11ACC6D4345` |

通过现有管道，以隔离死灵／女巫执行Single Player及正常技能请求。位置、目标生成、补资源和飞弹命中注入只准备条件，不认证自然寻路、掉落或完整怪物射击。

| 有限路径 | 实际观察 |
| --- | --- |
| 骨甲 | state14安装；先承受20点物理伤害而生命96不变；再承受60点，剩余30点护盾耗尽，人物生命96→66、状态撤出。 |
| 偷取生命 | state58安装后普通攻击击杀剩余1HP的目标，人物raw生命16896→17363；467raw回复来自扣血前934raw物理伤害的50%，没有按目标剩余生命截断。 |
| 铁处女 | state55安装于Unique僵尸；随后没有人物／宠物攻击，目标生命34→24.71875，人物受到近战伤害，failures为0。该场景含其他敌人，不认证逐击精确反射百分比。 |
| 碎冰甲 | state88安装，僵尸近战尝试后先出现反击命中／覆盖图43并死亡，人物生命不变，事件记录没有佣兵先行攻击。 |
| 寒冰装甲 | state20替换88；管理注入当前MPQ箭0的1点伤害后，人物raw生命减少256，生成skill60/rank2的真实反击弹，目标随后死亡。 |
| 冰封装甲 | state10替换20；受击后两只僵尸安装冻结state1／覆盖图42。它们随后由佣兵44击杀，不能把死亡算作冰封装甲伤害。 |
| 保存／恢复 | 两个隔离角色正常退局并在新进程加载；死灵保留raw生命16896，女巫保留134039，两者临时states／effects为空，宿主failures为0，随后正常退出。 |

本轮未认证全部诅咒AI、血魔／粘土石魔事件、护盾组合顺序、容量背压、多人或Linux。全局帧事件、队列2物品事件交错、特殊目标组8、未实现家庭AI和德鲁伊／刺客仍未完成，不能据编译与有限冒烟宣称所有buff或整个战斗完全复刻。证据目录`D:/Projects/d2x/artifacts/buff-native-events-20261015-013149`保存构建、运行与包身份及旧程序；原用户存档未改动。

### 历史药剂／治疗及动作增益包

该历史Windows **Release**包包含药剂／治疗、AI控制、动作增益和护盾登记顺序的收尾；下表路径与摘要记录该轮打包时的产物，已由页首当前包替换。CMake配置与all目标构建通过；首轮修复弹体整数加法缺少直接包含，第二轮143步完成。两包已更新，既有聚合初始化警告保留，未新增测试脚本、测试用例或专用程序。

| 产物 | 路径／字节 | SHA256 |
| --- | --- | --- |
| 客户端／嵌入宿主 | `dist/current/d2x.exe`，28106973 | `F5C13FCF299AF153F9CFA39F04E1B597CE15C1CFDAF159E78DEE1BCE3A75E5AE` |
| 独立服务端 | `dist/server/d2x_server.exe`，21264722 | `0C5B268D2E0FC604F0B8CFB31E0B23FC77B1F2550EEC81EE0FBFF220C75ED359` |
| 资源工具 | `build/bin/d2x_assets.exe`，10145381 | `38BDD46C0E35A971F4421AD87464C559C403063CA4C8846591DD09E20C15D697` |

新包以隔离58级野蛮人执行Single Player加载、管理准备野外位置、正常战斗体制及技能祭坛交互。state32安装后MAXHP 43264→58406、MAXMANA 29952→40435；随后安装state134。正常退局返回Characters且协议error为空，新进程读取该隔离存档后生命／法力上限恢复43264／29952，临时states／effects为空；两次宿主failures均为0，随后正常退局退出。命令字段错误及暂停时技能选择等待超时不计成功样本；实际增益以状态／资源观察为准。管理定位／生成不认证自然通行和掉落。

本轮未运行验证药剂、宠物治疗遍历、专心／狂战士／旋风、护甲／护盾新登记次序、怪物重复Cursed、全部诅咒AI、容量背压、多人或Linux。部分接触反应仍产生于基础扣血后，完整伤害前及全局事件顺序未完成；不能据本轮编译／有限冒烟宣称全部buff或整个战斗过程精确复刻。证据目录`D:/Projects/d2x/artifacts/buff-completion-20261014-233340`，旧两包、构建日志、EXE摘要与隔离角色均保留；原用户存档未改动。

### 历史持续效果包

此前包为2026-10-14 Windows **Release**持续效果收尾包，D2S v96。CMake配置及客户端、独立服务端、资源工具的all目标构建通过，修复技能枚举／公共查询的定义头依赖及治疗入口链接错误；最终增量4步通过。两包已更新，非致命聚合初始化警告保留。没有新增测试脚本、用例或专用程序，原MPQ、旧包及用户存档保留。

| 产物 | 路径／字节 | SHA256 |
| --- | --- | --- |
| 客户端／嵌入宿主 | `dist/current/d2x.exe`，27965518 | `3DF5D1A78C22A0EB2F5CF2FE904639B064E3838A43194250370441461768DDA6` |
| 独立服务端 | `dist/server/d2x_server.exe`，21120457 | `103D8BC011A64E6678F8BF99409ECE096167F38844FEAB90F4EFAAA01C4747F3` |
| 资源工具 | `build/bin/d2x_assets.exe`，10142309 | `93391B26050B026E92E0E5807E93CC4F0F11E7C9EBCD22BD4EC9F237A66EF0AE` |

通过已有调试管道，以隔离死灵、野蛮人和女巫角色执行正常技能／物件请求。管理生成、定位、清除干扰伙伴与扣资源只准备条件，不算自然战斗、掉落或正常寻路认证。

| 有限路径 | 实际观察 |
| --- | --- |
| 诅咒／骨甲 | 骨甲吸收10点物理伤害，生命96不变；伤害加深物抗−100，微暗灵视与恐惧切换控制；迷乱alignment=1、group 9序号及独立AIRESET登记，后续普通诅咒清节点／恢复阵营而保留未到期选择器；衰老物抗−50，吸引安装中立／节点。其余标准诅咒施放及原状态回包保留于证据，未逐通道核准全部反伤／吸取数值。 |
| 三种友方战吼 | Shout状态26提高防御13→23；Battle Orders状态32使raw最大生命43264→58406、最大魔法29952→40435；Battle Command状态51提高有效技能及被动。到期后状态／列表清空，生命与魔法上限、防御恢复初值。 |
| 三种祭坛 | 正常交互领取技能祭坛134，使Shout有效等级2→4；防御祭坛128替换技能祭坛，等级回2；耐力祭坛136正常安装。推进到tick6596后全部效果撤出，耐力49587等于当前最大值。 |
| 女巫护甲／护盾／治疗 | Frozen／Shiver／Chilling Armor依次只保留状态10／88／20；附魔16正常投递。能量盾30在140点物理诊断伤害下，4061 raw魔法消耗完、吸收2096 raw，生命280→148.1875，状态30撤销。Akara正常交谈恢复280生命／315魔法；定位仅准备交谈条件，不认证该段普通寻路。 |
| 保存／恢复 | 三角色正常保存、原69退局、返回Characters且error为空；女巫新进程恢复41级、280生命／315魔法，状态与效果为空，failures=0，再正常退出。截图确认真实游戏画面及HUD，未重做逐像素对照。 |

证据与全部中间构建／失败样本位于`artifacts/buff-closure-20261014-211109`，运行均使用上述同一客户端。早期目标被伙伴击杀、城镇战吼资格拒绝、无可用默认落点及未到达NPC的样本不算通过；随后分别隔离目标、使用野外可走位置和正常交谈核对。独立服务端只完成构建／打包；未运行PvPGN后端、多人、Linux、全等级／装备／AI家族或容量背压组合。完整全局事件／伤害前反应／死亡排序、PlayerPets及目标链整体顺序、未实现职业程序仍需后续阶段完成，有限结果不认证全部原版复刻。

### 此前光环包


此光环历史包为2026-10-14 Windows **Release**，D2S v96，已由后续包替换。光环收尾纳入同步来源回调、毒／诅咒期限候选、原列表到期事件、物品GUID计时器、旧装备撤出，以及非玩家逐写充能登记；真实铁魔刷新和女武神装备来源也进入共享候选。CMake配置及客户端、独立服务端、资源工具编译／链接通过；最终增量为1个编译及3个链接步骤。仍有非致命聚合初始化警告。未新增测试脚本、用例或专用程序，原MPQ、用户存档和历史包保留。

本轮修复运行发现的物品光环卸装残留。最终包隔离角色正常入局，力量／狂热／神圣冰冻的本人raw状态及切换撤销通过；Beast装备登记9级狂热及13级变熊5/5充能，换回普通剑后状态／计时器／充能立即撤出；Bramble佣兵装备登记荆棘及13级荆棘灵气7/33充能，同区卸装后计时器／充能立即清空。正常保存、原69退局返回CharacterSelection，error为空、宿主failures=0，随后退出进程。此前同轮修复包另观察神圣冰冻对实际怪物的脉冲／减速、Bramble跨相邻房间作用及存档新进程恢复。证据、构建日志、备份和包哈希在`artifacts/aura-closure-20261014-170741`，细节见[圣骑士](../gameplay/skills/PALADIN.md#有限冒烟)。不认证全部等级／装备、铁魔材料光环实机、多人、完整全局事件排序或伤害前反应。

上一版F955FFBFE211的包内EXE使用隔离存档Hero（女巫），管理入口准备10000000经验／35级，正常原请求依次学习39／44／45／55／59／64，各1级。进入普通难度Blood Moor，右技能64确认后原协议施放Frozen Orb：开始帧218，帧225的ManaFact为19968 raw，较原26368 raw扣除25点；帧228生成主球260与冰弹261，帧258观察21条冰弹及16条末端散射262，帧358技能弹体／待释放／施放记录均清空。截图确认冰球效果，宿主failures=0。原69退局返回CharacterSelection，无notice／error，正常quit后D2S解码通过；新进程读取保留35级、六项技能及施放后法力21032 raw，随后新进程再次正常入局。此验证只覆盖入局、一次冰封球生命周期和退出／恢复，不认证光环、全部伤害数值或完整战斗复刻。

证据及更新前两包备份在artifacts/orb-smoke-20261014-090719。上述冰封球记录认证上一版程序；当前光环包认证范围见前段与圣骑士专题。

房间改造阶段Release首次计划365个构建步骤，三份日志共涉及350个不同C++编译单元、365次编译动作（包括失败与重编译）。大范围重编译来自navigation.hpp、area_store.hpp、旅行接口及resources/archive.hpp等公共依赖变化，未清空构建缓存／切换配置或构建all目标。两次失败分别为spatial缺AreaStore完整头文件、调试快照仍读取已删除的旧跨界字段，均修正后增量续建。最终收尾为2个C++编译及3个链接步骤，成功生成d2x.exe。证据为`artifacts/flavie-boundary-20261012/build-rooms-release.log`、`build-rooms-release-2.log`和`build-rooms-release-3.log`；更早的连续寻路构建见同目录`build-release.log`。现有非致命警告保留，diff空白核对通过。原D2MOO结构尚未完全复刻的范围由[地图限制](../modules/MAP.md#地图缓存与分帧准备)及[基线](../../BASELINE.md)列明。

用户23:38对照诊断发现普通04预测走到NPC中心、等96才交谈、入场WP模式0及stash斜角预测／权威分叉。本轮已构建、打包并重放；规则与证据维护在[NPC交互](../gameplay/npc/INTERACTIONS.md)、[自研传送点](../gameplay/world/OBJECTS.md#自研传送点)和[共用寻路](../modules/MAP.md#共用寻路)。构建共涉及170个不同C++单元、526次编译动作（含迭代、失败和重编译，并非一次编译526文件）；三次较大增量分别150／168／145次，原因是PlayerState持有定点状态、公共interaction_geometry头经game_messages等广泛传播，期间公共接口多次调整重复触发依赖。诊断快照类型调整另触发43次；后续私有源码修改每次1–4个C++单元。没有清缓存、切配置或构建all。一次链接失败来自构建过程中修改源码导致旧导航库与新调用方不一致；另一次诊断编译错误是误把AreaMetadata当成含Grid的AreaDefinition，均修正后续建成功。日志在`artifacts/path-smoke-20261013-002214/build*.log`，最终构建为1个C++单元及2次链接。

包中保留此前地图缓存／分帧准备、战斗索引、阵营／组队、进退场与PvP，以及Party敌意红字、右上角骷髅和姓名颜色能力。此前表现的有限运行范围见[联网记录](../modules/NETWORK.md#组队邀请客户端)，旧构建日志在`artifacts/hostility-display-20261012`及`artifacts/area-performance-build*.log`；这些记录不等于当前包重新运行通过。

下面的提交前有限冒烟属于此前EXE；不将这些路径视为当前新增修正或全部玩法验收。宿主区域蓝图在独立MPQ线程准备，客户端房间消息消费仍为软预算，不保证每帧严格限时或完全无停顿；Play-Diagnostic.cmd保留双方消息和[area-load]阶段耗时。原MPQ、存档、历史程序和artifacts保持原位置，不复制进新的交付内容。

前批两幕三难度、组队任务及物品补齐的基础范围分别见[第一幕](../gameplay/monsters/ACT1.md#三难度重新核对)、[第二幕](../gameplay/monsters/ACT2.md#三难度重新核对)、[任务系统](../gameplay/quests/SYSTEM.md)与[物品核对](../gameplay/items/EVIDENCE.md#客户端与服务端物品核对)。以下哈希标识当前磁盘包。

本次启动修复更新前的两包EXE／DLL备份保留在`artifacts/single-player-startup-20261014/package-before`。此前本轮替换前的两包EXE／DLL备份保留在`artifacts/combat-build-20261014/package-before-20261014-073236`。前次保留的程序`dist/current/d2x-retained-20261012.exe`及`d2x_bncs_legacy-retained-20261012.dll`继续保留。本轮更新前的包内EXE／DLL另留在`artifacts/flavie-boundary-20261012/d2x-before.exe`与`dll-before.dll`，更早备份在`artifacts/diagnostics-fix-20261012`。根目录Play-Diagnostic.cmd使用新构建及既有角色目录，包内同名入口使用包内程序／角色目录，并记录双方消息。

| 产物 | 目录 | SHA256 |
| --- | --- | --- |
| 客户端／嵌入宿主 | `dist/current/d2x.exe`（27787845字节） | `4E12BC13C3B548A6D6C7CE7FF3B626C0326D3C9B92A7393ED672DA68A73C48AE` |
| 独立服务端 | `dist/server/d2x_server.exe` | `912C0CB9164D5824C7386F7B05A46B05316E1DCF335ABE4C1801F39AC865F43D` |
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
