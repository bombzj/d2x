# 构建、运行与打包

本页维护构建、运行与打包方法；功能范围、原版依据及缺口见[项目基线](../../BASELINE.md)与对应专题。实际构建、运行和打包情况在当次交付中说明，不维护包身份或冒烟历史台账。命令按用户有效授权执行，默认不构建、运行或打包。

需要人工操作时留存消息，用根目录`Play-Diagnostic.cmd`；它沿用普通Play的程序和角色目录，打开游戏及默认调试管道，分别记录客户端／嵌入宿主的双向D2GS消息、程序输出和EXE身份。它不自动构建或修改角色。目录、脱敏范围和故障现场读取方式见[原服收包记录](DEBUG_PIPE.md#原服收包记录)。

## 构建验收要求

构建交付必须同时满足成功退出和编译／链接零警告；已有警告也应定位并修复，不能仅凭退出码为0宣称构建通过。保留当前警告选项和优化配置，不通过关闭警告、添加抑制或降低优化级别规避诊断。第三方依赖产生的警告须明确归属并处理，不能静默忽略。

修复后沿用原目标与配置重新构建，确认涉及的编译单元实际重编译并检查完整输出；无工作可做的增量构建不能证明警告已消除。交付时说明实际构建范围与仍未验证的平台／配置。此要求不改变构建、运行和打包的授权边界。

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

`--lang <eng|chi|deu|fra|esp|ita|jpn|kor|pol|rus 或 0-9>` 覆盖语言：字符串取自 `data/local/lng/<目录>/` 的原始 string／expansionstring／patchstring.tbl，缺失回退 `eng`；未指定时读 MPQ 的 `data/local/use`。字形取自 `data/local/font/<语言目录>/`（西语系共用 `latin`，中日韩各用 `chi`/`jpn`/`kor`）；所选语言只提供 `latin` 字体时回退 `latin`，此时该语言的非 ASCII 字符无字形可绘。原版五个基础 MPQ 只含 `latin` 字体，中日韩界面需资源包内含对应 `data/local/font/<chi|jpn|kor>/` 字体；`Play.cmd -Lang chi` 转发该参数。字形纹理按需上传：字体表可含上万码点（中日韩 font16 约 16651 帧），仅在实际绘制时上传对应 DC6 帧，避免启动时逐帧建纹理。散装数据目录可用 `d2x_assets <data目录> pack-dir <前缀> <新.mpq>` 打成 MPQ 后挂载（成员名为前缀加相对路径）；繁体中文优先取原版 MPQ 自带的 `data/local/lng/chi/` 文本，仅当原版缺 `expansionstring.tbl` 与 `data/local/font/chi/` 字形时才补入 `PD2/Data` 的对应内容，不使用 SOE 资源；该缺口由 `assets/mpq` 的补充包自动补齐（如 `pd2-chinese.mpq` 提供 chi 的 `expansionstring.tbl` 与 `data/local/font/chi/`），否则这些文案回退英文。PD2 CN 副本把中文字符串表、字形和菜单图放在散装 `Data/local`，启动时 `Archives::mountLooseDirectory` 把它登记为 `data/local` 下的成员并优先于所有已挂载 MPQ，因此该 profile 不依赖补充包。ESC 菜单各级页面按所选语言取 `data/local/ui/<语言>/<名称>.dc6`，该语言缺图时回退 `data/local/ui/eng/` 原图（共用入口 `localUiArtPath`）；按键配置固定使用英文TBL和Latin FontInGameChat，保留原字号和坐标，不随所选语言或TTF字体替换。菜单范围与禁用项见[经典HUD](../gameplay/ui/CLASSIC_HUD.md#esc-菜单)。

当前源码普通启动进入原图主菜单：Battle.net沿账号／Realm／角色／房间链，Single Player使用内存MCP／D2GS；TCP/IP在本机选角后按原版直连4000并交换本地存档，详见联网模块。F11保存、Ctrl+F11校验后重载；ESC／失焦暂停单机，共享房间继续推进。角色、战斗、库存及保存由所连接服务端执行，当前支持范围和缺口见[基线](../../BASELINE.md)。客户端与独立服务端的分发目录分别为`dist/current`和`dist/server`，目录名不表示其现有程序的源码版本或构建配置。

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

启动测量在当次交付中说明程序版本、资源、启动参数及采样条件；临时测量文件可清理，不作为功能或原版一致性的长期依据。

## PD2 CN远端账号大厅

`PlayPD2.cmd`使用同一D2X程序和`scripts/play-pd2.ps1`，不会自动构建、安装／更新PD2或加载原DLL。先显式构建包含本功能的EXE；现有旧EXE不因新增脚本而获得兼容。默认普通`Play.cmd`仍为原版。

首次启动指定隔离副本的pd2目录，脚本创建被Git忽略的`pd2.local.json`；端点可显式指定，否则取环境变量`D2X_PD2_ACCOUNT_HOST`或该目录`zylpd2.ip`首个IPv4候选。当前样本的首行162.14.106.45:6112已可完成账号流程；其他线路或更新后的文件行序须另核实，可用`-AccountHost`覆盖首次生成值。已有配置则直接编辑后重启。CN默认选择服务器返回的第一个Realm，直接显示角色列表；不伪造Realm名称。

```powershell
.\PlayPD2.cmd -Pd2Directory 'D:\Games\DiabloII-PD2-Copy\pd2' -AccountHost '<CN账号服务器>'
# 后续沿用本机配置：
.\PlayPD2.cmd
# 只选既有角色并停在大厅；需先在该配置的UI内记忆一次账号：
.\PlayPD2.cmd -OnlineCharacter '<角色名>'
```

配置只保存`version=1`、`clientProfile=pd2-cn-normal-20261020`、`authentication=pd2-cn`、`originalClientDirectory`、`pd2Directory`、`accountHost/accountPort`、`gamePort`及`gateway`。原目录必须是pd2目录的父目录；相对路径按配置文件目录解析。账号密码在D2X界面输入，沿用每配置路径独立的Windows凭据管理器／Linux受限文件，不能写入启动命令、公开配置或文档。运行中修改配置定义会拒绝后续认证，需重启固定新服务与资源。

EXE入口为`--client-profile <file.json>`，PD2窗口标题为D2X - PD2 CN。启动核对固定样本SHA-256，按已恢复顺序挂载原包、CN patch及四个PD2包；未知构建或资源不符明确停止。认证只读原MPQ的key／owner资源与pd2目录三份版本文件，不执行目标程序。已支持当前服务的账号登录、Realm、既有角色选择、大厅列表及返回；新账号注册尚未重测。已显示当前角色原图，建房／加入请求已接入，MCP已返回成功；修正游戏端口后已实际显示罗格营地、本人、NPC和HUD，并正常退局返回选角；此阶段仅认证基础入场和城镇显示。房间详情、PD2单机／局域网及无窗口模式关闭；建角按原MCP 0x02（可选专家级0x04、天梯0x40）、删角按原MCP 0x0A编码提交，成败以CN服务回复为准。未指定 `--lang` 时该 profile 默认 `chi`，读取CN副本 `Data/local` 的中文表、字形与菜单图，`-Lang eng` 可改回英文。PD2登录自动选择返回的第一个Realm，直接显示角色列表。CN Normal默认gamePort=4001，来自固定BH对D2Net的端口补丁；原版默认4000。若沿用旧pd2.local.json中的4000，需改为4001后重启；不会由更新原客户端自动修改D2X配置。配置可选gameServer整数1–9（默认1）、lootAllocation布尔值（默认true）。CN创建游戏页从CN副本的Data/local/Lng/Chi/PatchString.tbl只读加载GS1–GS9名称及标题，使用原gsselectbuttons按钮；初选采用配置值，同一账号连接内保留本次选择，每次建房提交自己的GS编号，不改写配置。GS不是端口或传输选择；资源虽有CreateGameGS10，已核实的原选择器仅创建九个按钮，因此不开放GS10，旧配置值10须改为1–9。完整限制见[兼容设计](../architecture/PD2_COMPATIBILITY.md#当前代码接入范围)。

默认独立调试管道为`d2x-pd2-debug`，可用`-NoDebugPipe`关闭。大厅支持既有`online-login`、状态／列表别名、选Realm／角色、返回角色／Realm、取消列表、注销、`ui-input`、`screenshot`及`quit`，JSON请求格式沿用[调试指南](DEBUG_PIPE.md)。管道登录不记忆密码，状态仅给公开身份／挑战名称、包ID／计数和数值拒绝码，不保存认证载荷；需要记忆时仍通过UI登录。`-Hidden`建立隐藏的正常渲染窗口，便于沿相同入口自动操作和截图，不是无窗口协议模式。`-Diagnostic`记录程序身份、输出、启动节点及client-packets.jsonl的D2GS逻辑包；不记录SID／MCP原始认证载荷，0x68票据字段脱敏。没有到达GS收发阶段时文件可为空。游戏心跳问题可用`PlayPD2.cmd -Diagnostic`保留程序身份及游戏包记录，再取`online-status`的`gameHeartbeat`计数／时间及截图；只有游戏包完全断流才触发CN心跳超时。原版`Play-Diagnostic.cmd`语义保持原样。

## 无窗口协议客户端

PD2不支持本节的宿主、TCP/IP传档或无窗口组合。

现有`d2x.exe --headless <profile.json> --debug-pipe <独立名字>`可运行无窗口房主或一组原生TCP/IP客端，不初始化OpenGL、音频、UI、动画或客户端地图。`mode=host`读取`--mpq`，通过原内存MCP选角／建八人房，包含一个房主角色；`mode=clients`在一个进程中运行1–8个独立RealmSession，不读取MPQ。一个房主加七个客端即八个真实玩家，不增加房间容量或修改玩法规则。该显式诊断入口不替代普通客户端的地图／技能资格校验。

配置路径相对profile目录；`output`必须是尚不存在的新目录，输入D2S只读，角色名必须不同。输入副本和原生退局返回字节保存在`output/saves`，不覆盖输入；客端核对返回D2S头部校验和／姓名／职业，完整角色由宿主现有解码器验证。房主沿原CharacterStore保存。先退出客端，再退出房主；等待正常结束，超时报告失败并保留隔离副本。

```json
{
  "version": 1, "mode": "host", "address": "127.0.0.1", "port": 4000,
  "output": "run-host", "durationSeconds": 60, "manualStart": true, "pollMs": 10,
  "clients": [{
    "save": "fixtures/SorceressA.d2s", "area": 3, "anchor": [5688, 5148],
    "cycle": [
      {"kind": "wait", "durationMs": 10000},
      {"kind": "cast", "skill": 59, "offset": [5, 0], "durationMs": 20000, "intervalMs": 2000},
      {"kind": "move", "offset": [8, 0], "durationMs": 3000, "intervalMs": 500},
      {"kind": "move", "offset": [0, 0], "durationMs": 3000, "intervalMs": 500}
    ]
  }]
}
```

客端配置使用`mode=clients`，相同地址／端口、另一个新`output`，并在`clients`内列出其余角色。每人可指定不同cycle；角色必须实际学会技能，示例59为当前原表暴风雪，仍须按使用的MPQ确认。`cast`以收到的当前位置加offset为目标；`move`以固定anchor加offset为目标，省略anchor则在开始动作时记录一次收到的位置；也可用`point:[全局x,y]`指定绝对目标。目标各轴距收到的位置不得超过50格。`area`只等待该level的原房间分配，原包不含房间尺寸，不能由此证明本人当前区域或路径可达；坐标须先从同种子真实地图核实。无地图客户端不生成路径或预测位置；障碍、施法间隔、法力、伤害和目标资格由原服务端决定。`wait`不发输入，移动和施法按cycle轮换；轮询卡顿不补发积压动作。

默认等待`headless-start`，可先沿房主管道既有`travel`等管理命令准备区域，再向两进程发送`headless-start -Arguments @{delayMs=1000}`。需要同步时，先用`[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+3000`得到同一个未来时刻，向两进程传`@{atUnixMs=$startTime}`；不同时传delayMs。`manualStart=false`则本进程所有客户端入局两秒后开始。启动清零输入／位置／技能事件测量计数；duration从开始时刻计，不含选角／准备，网络字节与保存计数仍覆盖整次连接。`pollMs`为5–100ms，比较时须相同。房主pump推进内容、权威25Hz时钟和原包发布，轮询不是玩法tick；不同泵送频率不能直接比较。管理补资源等准备动作必须另记，不能当作正常战斗数据。

`headless-status`返回各角色就绪、法力／生命、已提交施法／移动、实际收到的位置变化、技能事件及错误；提交数不等于服务端执行数，原服通常不回显本人全部技能。`samples.jsonl`每秒记录状态及房主pump窗口mean／p50／p95／p99／max，`summary.json`记录退出结果。pump计时不含该轮客户端快照、JSON和磁盘输出，但包含宿主准备、权威步进及发布；进程CPU仍包含一个房主会话，不等于纯内核CPU。比较时另记实际Actor／飞弹／事件、tick推进和执行结果，区分空转、实际施法和法力耗尽。`quit`沿原保存退局，不提供绘图／UI命令。

持续施法需要足够法力。房主可显式配置`refillSeconds=1..3600`，从开始时刻起按间隔对已入局角色调用既有`RestoreResources`管理事务，恢复原最大生命／法力／耐力；默认0关闭，客端配置非零明确拒绝。它不取消服务端施法冷却或资源扣除规则，但会影响生存和资源轨迹，仅用于声明过的管理压测条件。`resourceRefill`单独记录应用／失败次数与耗时；耗时不计入hostPump，计入整进程CPU，比较时须保持相同间隔。

`serialAdmission=true`使客端按配置顺序逐一等待前一个会话实际入局，再连接下一个，用于稳定人物准入顺序。房主的`resumeOnStart=true`在共同开始时刻调用既有恢复事务，便于暂停准备后同步启动；默认均关闭。周期输入按共同开始时刻及动作间隔对齐，错过的间隔不补发；`missedInputIntervals`与`maximumInputDelayMs`记录本地输入调度偏差。

房主显式配置`detailedTiming=true`后，额外写出`ticks.tsv`和`host-phases.tsv`，每秒在pump之外刷新缓冲。前者记录每次`GameInstance::step`的开始／结束帧、完整调用耗时及领域纳秒计时；`before==after`是未完成重试，同帧的完整调用耗时应相加，领域耗时只取完成时的累计值，不能再累加重试行。领域按事件所属入口归类，飞弹事件中的同步伤害也计入missiles，不能把combat列当作全部伤害成本。后者拆分`GameHost::advance`、主线程内容交接、可靠事件发布和常规状态发布；异步区域准备worker、外层输入处理与日志开销不在这些分段内。纳秒值是经过时间而非线程CPU；进程CPU须另采样，并同时检查完成tick间隔，避免把低CPU的等待误认为流畅。

`ticks.tsv`末尾记录常驻碰撞格维护，仅在细分计时启用时读取时钟及更新计数。按每次step实际增量输出，阻塞重试行可以求和；初始化、准入及step之外的调用不在这些行内。经过时间已包含在完整调用及对应领域中，不重复相加。比较须注明是否启用计量，粗CPU可关闭细分计时。

| 列 | 含义 |
| --- | --- |
| `nativeUpdateNs` | writeFootprint调用的经过时间，含单位／物品／门户格归属及写入；不含既有飞弹／技能直接位操作、逐格读取、候选链及精确命中 |
| `nativeWrites` | 实际发生值变化的格写入次数，同值写入不计；回滚还原另由回滚次数观察 |
| `nativeReads` | 采样器、导航及移动探测读取格的次数 |
| `nativeMoveProbes` | 实际进入子格的移动探测次数，包含阻挡失败 |
| `nativeRollbacks` | 移动事务回滚次数，含未产生格写入的提前退出 |
| `nativeQueryScopes` | 寻路临时撤销／恢复作用域次数，非候选单位数量 |
| `footprintNavigationBuilds` | 导航视图建立次数，当前不构造单位脚印 |
| 其他`footprint*`列 | 旧版脚印构造计量保留兼容；常驻格实现中为零 |

不能用周期spatial阶段耗时代替全部碰撞成本；碰撞维护在各领域实际发布点执行。原版语义及维护范围见[动态碰撞格](../modules/SERVER_SYSTEMS.md#动态碰撞格的原版语义与迁移边界)。

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

需要可分发压缩包时，用`package.ps1 -OutputDirectory dist/<新的包名> -Archive`创建全新的包目录及同名ZIP；输出必须位于本项目dist内且尚不存在。压缩只针对这次新目录，避免将dist/current中已有的存档、凭据、截图和诊断文件带入；旧压缩包保留。原MPQ和PD2文件仍由使用者提供，复制的本机配置路径在其他机器上须重新选择。

`package.ps1`复制已构建程序、Play／PlayPD2／诊断入口、运行脚本及说明／许可，默认更新dist/current，不复制MPQ、不删除该目录的存档或截图、不运行程序。PD2配置只按白名单复制路径、服务和版本字段，账号密码、key及凭据文件不进入打包清单。程序、脚本和文档按长度／SHA256比较，内容相同则跳过复制，并报告复制文件数、字节数与跳过数；连接配置也只在内容变化时更新。同步文档时移除源目录已不存在的说明文件，避免目录调整后留下过期文档。运行目录已有的`artifacts`、旧程序和用户文件不会进入复制清单，也不会被清理，因此整个目录占用不代表本次交付大小。未指定 `-Mpq` 时启动脚本先查运行目录本身和 EXE 同目录，再从运行目录向祖先目录寻找 `assets/mpq2`，因此包留在仓库内可直接双击；移到别处可把完整原 MPQ 放在 `d2x.exe` 旁边，或传实际原资源路径。`-Mpq/-OnlineConfig` 按调用时工作目录解析为绝对路径。程序工作目录为包根，客户端偏好与截图归包，角色持久化由原服处理。整个 `dist/` 忽略Git。示例路径按仓库根作为调用目录；例如从仓库根启动 `dist/current/Play.cmd -Mpq assets/mpq2`，不要把包目录当原资源相对根。

依赖说明依据 [raylib 官方 Linux 构建文档](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)、[StormLib 官方源码](https://github.com/ladislav-zezula/StormLib) 及本项目固定版本的 CMake 配置。

独立服务端在CMake Tools中选择`d2x_pvpgn`构建目标，成功后运行[scripts/package-server.ps1](../../scripts/package-server.ps1)。原`d2x`目标和客户端打包脚本继续保留；`d2x_server`是内核库目标，不能误当成EXE目标。两份脚本均复制当前已构建文件，不隐式编译或切换配置。服务端启动／关闭请使用[PvPGN部署页](PVPGN_SERVER.md)中的独立脚本，不使用客户端Play入口。
