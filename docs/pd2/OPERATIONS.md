# 运行与部署

PD2专用启动、配置、部署与诊断操作。公共构建和命令格式仍由开发指南维护；CN与Pd2Local分别标明。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="运行与部署统一入口"></a>

## 运行与部署：统一入口

来源：[docs/development/BUILD_AND_RUN.md](../development/BUILD_AND_RUN.md)。

先显式构建当前EXE，再运行唯一的`PlayPD2.cmd`。默认进入与原版相同的主菜单，由玩家选择Single Player、TCP/IP或Battle.net；`PlayPD2Local.cmd`已移除。原版`Play.cmd`继续使用原版资源。两个入口共用同一EXE、前端、RealmSession、Remote客户端及`scripts/play.ps1`，PD2包装脚本仅负责准备固定资源／服务配置。

```powershell
# 首次配置资源；单机／TCP-IP不要求账号服务器。
.\PlayPD2.cmd -Pd2Directory 'D:\Games\DiabloII-PD2-Copy\pd2'
.\PlayPD2.cmd
# 可选：诊断记录、自动远端选角或TCP/IP快捷入口。
.\PlayPD2.cmd -Diagnostic
.\PlayPD2.cmd -OnlineCharacter '<角色名>'
.\PlayPD2.cmd -HostLan 127.0.0.1 -GamePort 14000 -HostSaves saves/pd2-host
.\PlayPD2.cmd -Lan 127.0.0.1 -GamePort 14000 -HostSaves saves/pd2-guest -PipeName d2x-pd2-guest
```

默认使用Git忽略的`pd2.local.json`；该文件不存在但已有`pd2-host.local.json`时复用旧配置。首次创建可用`-Pd2Directory`或`D2X_PD2_DIRECTORY`提供资源目录。账号服务器取`-AccountHost`、`D2X_PD2_ACCOUNT_HOST`或原目录的`zylpd2.ip`；未知时可留空，Battle.net登录时才检查。已有配置不被参数静默覆盖，修改服务／资源须编辑该配置或显式选择新的`-Profile`文件。账号密码只在D2X登录界面输入，不写命令行或公开配置。

EXE以`--client-profile <file.json>`选择资源目录和服务配置；本进程内保持同一资源集合，更新PD2后重启D2X。启动不按整包摘要白名单限制资源更新，具体检查见[资源加载](RESOURCE_LOADING.md#更新与兼容性检查)。连接前按菜单的本地接入／账号认证选择PD2协议，当前连接期间不切换。旧`-Local`／`-Pd2Local`／`--pd2-local`参数只保留兼容，不再限制菜单模式。默认调试管道为`d2x-pd2-debug`，默认不开诊断文件记录；多进程使用不同`-PipeName`。

<a id="运行与部署单机与tcpip"></a>

## 运行与部署：单机与TCP/IP

来源：[docs/development/BUILD_AND_RUN.md](../development/BUILD_AND_RUN.md)。

Single Player与TCP/IP使用自有宿主，默认角色目录`saves/pd2`，沿用原生PD2 D2S，和原版角色隔离。无需账号认证，不连接CN账号端点，不装载原DLL。TCP/IP两端使用相同PD2资源，各自选择本机角色；Host／Join页面、存档回传及返回主菜单复用原版流程。本地端口默认4000，与CN账号配置中的gamePort分开。

可组合`--class`／`--save`／`--load`及`--host-saves`准备本机角色。Pd2Local无窗口房主与图形TCP/IP客端已完成有限互通验证；独立PvPGN实验入口不在当前支持／运行验收范围，CN配置不开放无窗口模式；分别见[无窗口配置](#运行与部署无窗口配置)、[自研独立服务规则](#运行与部署自研独立服务规则)、[自有宿主](../architecture/PD2_COMPATIBILITY.md#自有pd2城镇宿主)及[PD2协议与存档基线](PROTOCOL_AND_SAVES.md#统一入口与连接生命周期)。

<a id="运行与部署cn账号与大厅"></a>

## 运行与部署：CN账号与大厅

来源：[docs/development/BUILD_AND_RUN.md](../development/BUILD_AND_RUN.md)。

在同一主菜单选择Battle.net，使用配置的CN账号服务；`-OnlineCharacter`仍可沿真实账号／Realm流程快捷进入，要求已通过UI记忆登录。脚本不自动构建、安装／更新PD2或加载原DLL。资源／认证绑定与原版相互隔离，修改配置后须重启。

配置只保存`version=1`、`clientProfile=pd2-cn-normal-20261020`、`authentication=pd2-cn`、`originalClientDirectory`、`pd2Directory`、`accountHost/accountPort`、`gamePort`及`gateway`。原目录必须是pd2目录的父目录；相对路径按配置文件目录解析。账号密码在D2X界面输入，沿用每配置路径独立的Windows凭据管理器／Linux受限文件，不能写入启动命令、公开配置或文档。运行中修改配置定义会拒绝后续认证，需重启固定新服务与资源。

EXE入口为`--client-profile <file.json>`，PD2窗口标题为D2X - PD2。启动按统一顺序挂载原包、CN patch及四个PD2包，不按整包摘要拦截资源更新；CN认证在实际登录时读取当前文件，按服务端挑战计算校验和；未知核心不再按摘要拦截，只将未核实的核心计数保留为未映射数据。认证只读原MPQ的key／owner资源与pd2目录三份版本文件，不执行目标程序。已支持当前服务的账号登录、Realm、既有角色选择、大厅列表及返回；新账号注册尚未重测。已显示当前角色原图，建房／加入请求已接入，MCP已返回成功；修正游戏端口后已实际显示罗格营地、本人、NPC和HUD，并正常退局返回选角；此阶段仅认证基础入场和城镇显示。CN房间详情及PD2无窗口模式关闭；单机／局域网通过同一主菜单进入；建角按原MCP 0x02（可选专家级0x04、天梯0x40）、删角按原MCP 0x0A编码提交，成败以CN服务回复为准。未指定 `--lang` 时该 profile 默认 `chi`，读取CN副本 `Data/local` 的中文表、字形与菜单图，`-Lang eng` 可改回英文。PD2登录自动选择返回的第一个Realm，直接显示角色列表。CN Normal默认gamePort=4001，来自固定BH对D2Net的端口补丁；原版默认4000。若沿用旧pd2.local.json中的4000，需改为4001后重启；不会由更新原客户端自动修改D2X配置。配置可选gameServer整数1–9（默认1）、lootAllocation布尔值（默认true）。CN创建游戏页从CN副本的Data/local/Lng/Chi/PatchString.tbl只读加载GS1–GS9名称及标题，使用原gsselectbuttons按钮；初选采用配置值，同一账号连接内保留本次选择，每次建房提交自己的GS编号，不改写配置。GS不是端口或传输选择；资源虽有CreateGameGS10，已核实的原选择器仅创建九个按钮，因此不开放GS10，旧配置值10须改为1–9。完整限制见[兼容设计](../architecture/PD2_COMPATIBILITY.md#当前代码接入范围)。

默认独立调试管道为`d2x-pd2-debug`，可用`-NoDebugPipe`关闭。大厅支持既有`online-login`、状态／列表别名、选Realm／角色、返回角色／Realm、取消列表、注销、`ui-input`、`screenshot`及`quit`，JSON请求格式沿用[调试指南](../development/DEBUG_PIPE.md)。管道登录不记忆密码，状态仅给公开身份／挑战名称、包ID／计数和数值拒绝码，不保存认证载荷；需要记忆时仍通过UI登录。`-Hidden`建立隐藏的正常渲染窗口，便于沿相同入口自动操作和截图，不是无窗口协议模式。`-Diagnostic`记录程序身份、输出、启动节点及client-packets.jsonl的D2GS逻辑包；不记录SID／MCP原始认证载荷，0x68票据字段脱敏。没有到达GS收发阶段时文件可为空。游戏心跳问题可用`PlayPD2.cmd -Diagnostic`保留程序身份及游戏包记录，再取`online-status`的`gameHeartbeat`计数／时间及截图；只有游戏包完全断流才触发CN心跳超时。原版`Play-Diagnostic.cmd`语义保持原样。

<a id="运行与部署无窗口配置"></a>

## 运行与部署：无窗口配置

来源：[docs/development/BUILD_AND_RUN.md](../development/BUILD_AND_RUN.md)。

PD2自研宿主可同时传入`--client-profile <pd2-local配置>`与`--headless <诊断配置>`。资源配置格式见[pd2-client.example.json](../development/pd2-client.example.json)，与[公共无窗口入口](../development/BUILD_AND_RUN.md#无窗口协议客户端)中的诊断配置是两个独立文件。房主和客端均固定Pd2Local协议、核对当前五个MPQ；房主准备PD2规则，管理区域范围为1–210，不能由管理旅行授予特殊开图资格。CN配置仍拒绝无窗口入口；已构建并验证无窗口房主＋图形TCP/IP客端，纯无窗口客端组合未在本轮重测。

<a id="运行与部署自研独立服务规则"></a>

## 运行与部署：自研独立服务规则

来源：[docs/development/PVPGN_SERVER.md](../development/PVPGN_SERVER.md)。

`ruleset=pd2-local`现沿同一个PvpgnServer／NativeRealmHost准备`Pd2TownHost`规则。新角从当前MPQ创建，D2DBS读写调用该内容的PD2存档codec；原版默认仍为`lod113c`。资源入口为`src/resources/pd2/archives.cpp`，检查五个MPQ可读并有序挂载，不读取ProjectDiablo.dll或BH.dll；表与格式由实际消费者校验，不以整包摘要拒绝更新。单包、批次及初始同步按同一Pd2Local协议校验。该实验组装仅完成构建；自研PD2当前支持限单机／TCP/IP，不测试其作为PD2 GS接PvPGN，也不据此声明支持或P1验收完成。

<a id="运行与部署独立服务配置"></a>

## 运行与部署：独立服务配置

来源：[docs/development/PVPGN_SERVER.md](../development/PVPGN_SERVER.md)。

PD2服务配置见[pd2-server.example.json](../development/pd2-server.example.json)，客户端见[pd2-client.example.json](../development/pd2-client.example.json)。客户端使用`d2x.exe --client-profile <配置路径>`，Battle.net沿普通PvPGN账号认证／MCP连接这套自研游戏服务，游戏消息选择Pd2Local；同一配置也用于单机、TCP/IP及既有无窗口入口。不使用CN认证、GS选择或CN周期报告，不能用它登录CN服务。账号认证的CheckRevision仍读取原目录三份版本文件；单机、TCP/IP与独立服务只需要对应资源。配置路径相对配置文件目录解析；示例路径需替换为用户实际资源目录。原生PD2客户端互通未认证。

<a id="运行与部署周期报告诊断"></a>

## 运行与部署：周期报告诊断

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

`online-status.online.pd2Reports`独立记录CN周期报告：`ticksSent/tickEchoes/unmatchedEchoes`对应5C/FF00及9D/2C回显，`positionsSent`对应5C/4，`bhReportsSent`对应特殊聊天通道的BH文件CRC。另有`lastTick/lastEcho/roundTripMilliseconds/lastPosition`；发送计数表示入队，只有匹配tick的回显才计确认，坐标／CRC没有已核实的独立ACK。异常断开后保留，下一局清空；原版不发送这些消息。它不包含认证材料，也不是全部反外挂检查通过标记。实现和待验限制见[周期报告](../resources/PD2_CN_DLL_ANALYSIS.md#原客户端抓包确认的周期上报与候选修复)。

<a id="运行与部署扩展状态诊断"></a>

## 运行与部署：扩展状态诊断

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

`online-status.online.pd2State`返回CN扩展副本：`discovery`含全量是否已知、收包计数和已发现暗金／套装索引；全量未知时未列出项不代表未发现。`coreMaterials.mappingAvailable`表示已核验核心版本且已准备索引映射；`knownSlots`列出已知槽及计数，增量先于全量时不补造其余零值；`snapshotCounts`为全量及后续增量合并结果，`unmappedUpdates`仅在版本／准备缺失时保留原类型报告。它不等于CN共享材料计数，也不授予存取能力。`effectReports`是83原字段与本人A9移除后的报告，不是已还原计时器；`hotkeys`只保存16槽服务器绑定。另有84编号／默认值、地图索引、外观资格、分配与剩余怪物。换区保留、退局清空，无新增写入命令或认证材料。

<a id="运行与部署地图广播诊断"></a>

## 运行与部署：地图广播诊断

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

`pd2State.mapAttributes`为最近一次55地图属性广播，含两个未命名头字段、区域、品质、物品记录索引及最多24条`stat/parameter/value`；未收到时为null。它不是地图布局、种子或本人的使用回执，不能用其判断地图已打开。门户地图属性说明已有消费者，具体显示边界见[物品界面](ITEM_UI.md)，权威开图与属性路由见[地图词缀](MAP_PROPERTIES.md)。

<a id="运行与部署仓库翻页命令"></a>

## 运行与部署：仓库翻页命令

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

CN候选的`storage-page`改为mdll1控制链：`page`为一基目标页（32位，固定样本元数据支持范围1–500，不代表线上总页数），`shared`为目标账号共享域。跨域仅允许目标首页，同域仅允许相邻页，分别发C2S 3A/1B、1C或19、1A；不发核心55，不为9D/function18补发5A。要求已初始化、同步、打开仓库且空Cursor；首次打开自动发送一次3A1B00，未确认时不把核心首屏视为个人仓库；若已是个人首页可无回包，仍允许切共享首页取得确认，之后须匹配域和页的9D确认。`online-snapshot`报告`shared`、`cnConfirmed`、`requestedShared`及两个原始flags，`online-items`每件物品增加`sharedStash`。不循环枚举、不自动重试；超时暂停仓库。已核实实际个人／共享切换和共享相邻翻页回包；已选中的首页在原模块中不重复回确认，不应靠重发取ACK；原版不开放此命令。

<a id="运行与部署材料和索引命令"></a>

## 运行与部署：材料和索引命令

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

CN仓库还支持`storage-page`的`navigation`字段：`previous-index`／`next-index`／`first`／`last-nonempty`／`previous-main-index`／`next-main-index`，此时不填page，shared须与当前域一致；结果页取9D/18。`storage-materials`用`amount=1/0`切材料页，不等待普通选页ACK。`material-withdraw`用`code`、`amount=1/5/20/50`及`toCursor`（Cursor仅1）；`materialOperation`默认`withdraw`，`cube`只允许非Cursor且须有真实可访问的本人方块，客户端自动填写并复验方块GUID／revision；`upgrade`用源code、消耗量3／15／150且非Cursor，仅允许原生3:1配对及已同步的双方计数。`ground`不受支持，CSL／CSR实际选择方块而非地面；`material-deposit`用当前Cursor或本人背包的itemId／itemRevision。材料存取要求98项代码计数初始化完成，并等待真实计数／物品事实；状态在`inventory.storage.materials`，不伪造物品GUID。普通页、材料页、共享／个人域的权限独立。

<a id="运行与部署无限书确认"></a>

## 运行与部署：无限书确认

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

固定CN的无限书使用记为`SentNoAck`，不等待不会发生的源物品消耗，也不将发送当作成功鉴定／开门。

<a id="运行与部署心跳判断"></a>

## 运行与部署：心跳判断

来源：[docs/development/DEBUG_PIPE.md](../development/DEBUG_PIPE.md)。

CN心跳独立持续发送；缺少一次Pong不阻塞后续心跳或断开仍在收游戏数据的连接。真正断流仍按既有timeout失败，失败快照保留计数。

## 打包配置

PD2配置只按白名单复制路径、服务和版本字段，账号密码、key及凭据文件不进入打包清单。原MPQ和PD2文件仍由使用者提供；复制的本机资源路径在其他机器上须重新选择。公共产物与目录约束见[构建与运行](../development/BUILD_AND_RUN.md#当前本地分发目录)。

## 管理旅行

`travel`的level为当前宿主准备的Levels身份，PD2包含扩展预设区域。未给坐标时使用原tile11／固定位置后备与权威空位搜索；通过原旅行事实同步，不授予任务或传送点，也不能作为开图、事件或首领资格认证。公共命令参数见[调试管道](../development/DEBUG_PIPE.md)。

## 仓库诊断

`online.inventory.storage`额外报告CN `page`／`requestedPage`／`initialized`／`synchronized`；分页超时撤回仓库访问，不将旧页号视作已确认。切换与材料操作见[仓库翻页命令](#运行与部署仓库翻页命令)及[材料和索引命令](#运行与部署材料和索引命令)。
