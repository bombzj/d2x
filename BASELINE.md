# 项目基线

更新：2026-10-10。开始修改前阅读[AGENTS](AGENTS.md)与[对应模块](docs/README.md)，实现状态以当前源码为准。历史过程由Git追溯，技能原规则／运行证据由专题维护，后续顺序见[总计划](docs/architecture/MULTIPLAYER.md)。

## 产品与代码边界

D2X提供Single Player、TCP/IP局域网宿主与既有原服入口。选角至入局只有一套客户端：RealmFrontend → RealmSession／RemoteWorld → RemoteTown／RemoteUiClients／RemoteScene → SceneController／SceneView。自研宿主收发原1.13c MCP／D2GS包，内存字节队列和TCP共用服务；原服账号走PvPGN，自研Realm预认证。断线不切换权威，客户端不读取D2S或执行旧单机规则。

服务端拆为hosting协议／内容准备／存储／实例调度及server领域。GameInstance只组装AreaStore、PlayerStore、GameSystems、类型化命令FIFO、25Hz固定步和可靠事件；状态由各领域持有，窄Ports协作，不恢复万能GameSession。内核不读取MPQ、设备、GPU、socket、文件或Win32。两端共用NativeMapGenerator和原生成／reveal函数，运行状态独立；原包投影不替代原地图。入口见[架构](docs/architecture/OVERVIEW.md)、[数据流](docs/architecture/DATA_FLOW.md)、[内核系统](docs/modules/SERVER_SYSTEMS.md)。

有参考依据、匹配原版的公共函数提取及客户端修复已获授权；每处记录MPQ／原包／原函数证据。禁止为兼容自研宿主单独改客户端或增加私有协议。客户端仅以已知本人属性计算显示，不推算远端隐藏值。先参考master的单机实现，再用当前MPQ及本地reference补证据；D2Game权威执行不等于完整技能两端共用。职责依据见[参考设计](docs/architecture/REFERENCE_DESIGN.md)。

## 当前源码范围

| 领域 | 已执行范围 | 主要限制 |
| --- | --- | --- |
| 局前／传输 | 原服认证、MCP角色／房间；服务端D2S角色列表／建删选；内存与LAN TCP；独立控制台D2GS及D2CS／D2DBS适配已构建打包 | 独立账号、Ladder、完整错误恢复未完成；PvPGN本机代表路径已有有限冒烟，故障恢复／跨机器／长期运行未验收 |
| 世界／旅行 | 原五幕1–136生成器；区域准备、碰撞、走跑／边界及UNIT_TILE；传送点、本人双向回城门、任务红／蓝门、Warriv／Meshif双向跨幕及第二幕机关旅行／真墓开墙 | 长绕墙／动态拥挤、完整房间活动／兴趣、完整特殊任务内容、剧情演出及队友门未完成 |
| 库存／成长 | 背包／Cursor／腰带／装备／切组／合堆／书本转装、镶嵌／普通方块／全品质生成；装备总值、经验升级、加点学技／热键、永久奖励、第一幕任务物品与Akara免费重置 | 复杂容量／争用、全部职业属性／奖励、复杂多人任务资格未完成 |
| 战斗／技能 | 普通空手／近战／弓弩、投掷／左手动作、六通道；药瓶、隐藏Kick、Unsummon及四项卷轴／书本；女巫26主动／4被动、亚马逊24主动／6被动 | 其他职业主动、完整双持组合、吸血／吸魔／压碎／撕裂与触发、受击恢复／格挡／耐久完整规则、PvP未完成 |
| 怪物／伙伴 | 第一幕63可击杀战斗类型／25类AI，含57普通池身份、十三固定超级暗金、冠军／暗金／仆从、五首领策略及石像鬼陷阱；精英数值／光环／诅咒／死亡事件、原rank投影；Hydra、诱饵／女武神及血乌奖励罗格的归属／AI／换区与内容准备 | 其他幕怪物、完整雇佣／复活／装备／成长及其他召唤未迁入；首领／精英代表路径有限V2；全部随机分支、精英染色／专用死亡演出及完整首杀／任务掉落未认证；完整Vision／房间调度／拥挤及原版随机流顺序未复刻；女武神重甲死亡组件待原版核实 |
| 掉落／资源／死亡 | 普通掉落拾取、金币丢弃／部分拾取；自然恢复、药水／定时效果；死亡惩罚、城镇复活、本人尸体回收 | 近队友NoDrop／任务掉落、全部负面状态清除、硬核／部分拾回／多尸体及异常退出边界未完整认证 |
| 物件／NPC／任务 | 普通门箱桶、部分神殿／水井；静态NPC、普通买卖／回购、个人赌博／批量、鉴定／充能修理／仓库；五幕27项任务个人主流程的资格、机关／任务物品／奖励／旅行与存档 | 陷阱／爆炸桶／其他特殊机关、完整城镇AI、完整后三幕怪物、城镇演出与组队共享未实现；准确范围见[ACT1](docs/gameplay/quests/ACT1.md)、[ACT2](docs/gameplay/quests/ACT2.md)、[ACT3–5](docs/gameplay/quests/ACT3_5.md) |
| 多人 | 1–8人同局名册、走跑／装备外观／聊天；GameHandle多实例、独立连接／租约及定向私有事实 | 玩家交易和广播／私聊／聊天屏蔽权威已接，见[玩家交易](docs/gameplay/items/PLAYER_TRADE.md)；队伍、PvP、跨机器／长期稳定性未验收；独立EXE本机双房有有限隔离证据，跨机器与高负载未验收 |

所有暂缓意图显式拒绝／NotImplemented，不用普通攻击替代技能、不漏条件发奖。邪恶洞穴缺少敌对怪物规则时，仅用已授权沉沦魔替身并保留真实身份；中立不可替换。缺规则／无法放置的人口保留未完成数；清场资格仅给当时在洞内的人物。不因此宣称缺失类型完成；第一幕当前范围及限制见[第一幕台账](docs/gameplay/monsters/ACT1.md)。

Kick（ID1）是原表隐藏通用程序，SkillDesc.ListRow=-1；各职业破普通木桶时自动KK，不是其他职业可学习的踢腿技能。刺客职业踢腿另属职业技能。木桶OperateFn5调用skills窄入口；objects／loot仍执行破坏与掉落。十项依据、公共武器／药瓶与物品技能入口见[GENERAL](docs/gameplay/skills/GENERAL.md)，全部技能状态及迁入规范见[COMMON](docs/gameplay/skills/COMMON.md)。[技能目录](docs/gameplay/skills/README.md)已建立七职业、怪物／精英、同行者、物品与特殊分类入口，未实现范围明确保留；完整MPQ逐ID台账仍待补。定义／树／提示或文档可用不等于职业执行或视觉完成。

库存／人物事务同时复验revision，一次发布物品／人物事实后提交持久值、资源与总值。技能开始与释放分别复验装备、目标、区域及资源；背压重试不重复扣费或掷伤。伙伴内容准备在hosting，状态及种子由独立领域持有。详见[库存](docs/modules/INVENTORY.md)、[人物](docs/modules/CHARACTER.md)、[内核](docs/modules/SERVER_SYSTEMS.md)。

P1日常操作已收敛：地面装书／部分合并／自动装备，脱带／卖带余药落地，方块目标鉴定，NPC距离／视线与交谈revision绑定，拾取／拾尸／复活背压等待、唯一物品限制与死亡落地；药水原8.8剩余值／帧合并及双倍判定、满值／到期／死亡清理、NPC按MPQ清毒／冻结／curable状态。尸体沿原回收与第一具非空尸体写档，热键补原清除编码。P1未改客户端，随P5统一构建打包；金币丢弃／拾取及自恢复物品的等待拾取已有下列有限V2，其余新增路径仍为V1。

P5物品侧新增原38／58任务加工、operation28组装、牛门／Pandemonium真实地图及材料事务、任务消耗与Token、充能技能源／扣费、装备六类触发事件、商店周期、地面自恢复／过期与MonStats首杀TC选择。客户端仅补原协议已有的任务加工与充能来源／显示，同样用于D2GS；不增加自研宿主分支。公共恢复规则、按物品GUID持有的时钟、独立effects触发队列及原子事务保持领域分工。准确范围与依据由[物品目录](docs/gameplay/items/README.md)链接负责专题。P5仍有前置任务／队伍／其他职业与专用触发程序依赖，未达到整体可玩和验收条件，不能标为全面完成。

## 入口与保存

Single Player使用原MCP角色界面，选角后自动建普通单人房、跳过大厅；首页TCP/IP Game使用原MPQ Host／Join页面，本机IPv4由应用提供，Join默认选中127.0.0.1。Host选角后自动建角色名普通8人房，跳过大厅；Single Player／LAN选角不显示Realm控件。界面Host监听全部IPv4，当前源码默认MCP6113／GS4000；LAN Join与原服游戏默认端口同为4000，显式配置仍可覆盖。同机原D2GS、独立服务与LAN不能共享游戏端口。自定义端口及`--host-lan`／`--lan`仍保留，加入回包使用连接实际到达的接口地址。返回菜单不停止其他玩家房间。连接失败显示实际IP／端口及底层原因，玩法不分连接类型。见[联网](docs/modules/NETWORK.md)。

自研存档属hosting／persistence，D2S v96不变，当前源码规则指纹为`d2x-character-admission-v30/native-wire113c/d2s96/act1-hireling/pvpgn-newbie89/player-trade-chat`，包含第一幕佣兵服务、装备、成长、主人死亡联动及PvPGN新角色登记初始化；后者仅接受带INIT的130字节v89空登记，不迁移已有旧角色。整局角色锁、校验后原子替换／.bak、失败保留实例与租约；旧规则不静默迁移。F11保存、Ctrl+F11校验后原协议退局重入及`--load`／`--save`入口保留。佣兵身份、经验、死亡位与装备沿原字段保存，AI、药水恢复队列和名单不写盘。限制见[存档](docs/modules/SAVES.md)。

Single Player的ESC菜单／失焦同时暂停单机宿主与世界表现，菜单仍可操作，恢复不补算暂停时间；共享房间／原服继续推进。世界表现时钟源码随本次完整构建入包，未专项运行认证。现有named pipe提供权威快照／有界历史、时钟覆盖、保存／重载、资源／经验／金币、MPQ物品／怪物准备等管理入口；正常玩法仍发送原online-*包。消息目录及stub不代表全部实现。接口见[调试管道](docs/development/DEBUG_PIPE.md)和[服务端协议](docs/modules/SERVER_PROTOCOL.md)。

<a id="当前运行包与有限冒烟"></a>

## 当前运行包与验证边界

2026-10-10玩家交易／聊天批完成Windows **Debug**客户端及独立PvPGN服务端构建打包。当前`dist/current/d2x.exe` SHA256为`F84F2975887B90F39BD69C2392E3FF15202603FF37C7B408A7E1F97DDC02CF58`，`dist/server/d2x_server.exe`为`E6B43746C872285516C663C31D2BDB437D163E39550341AE4C79B06C2AD7656A`，网络DLL仍为`72AD07696429BB5A1A35D1EAE9CBB97E7F91B828E02885933FAE0022C0B1274B`。MinGW内核目标启用big-obj以容纳Debug模板段；既有任务内容／NPC视图警告仍在。规则v30，磁盘D2S v96。两名已备份测试角色的本机PvPGN代表路径冒烟通过：广播／具名私聊／屏蔽／原错误包、头顶到期与晚入投影，交易邀请／拒绝、物品交换／撤销确认／确认锁、金币报价与成交、带Cursor取消、打开报价期间自动保存、断线回滚及在线停服重启重入。金币50/0报价期间不扣，成交后30/20；成交与取消结果重启后保持。详情及未覆盖边界见[玩家交易](docs/gameplay/items/PLAYER_TRADE.md#有限冒烟)与[联网](docs/modules/NETWORK.md#多人只读副本)，证据保留在忽略目录`artifacts/trade-chat-smoke-20261010`。未新增测试脚本／用例／程序；本批启动的两客户端和独立服务已正常退出，原PvPGN进程及配置未变。

2026-10-09第一幕佣兵及此前消息／技能／实体重构已完成Windows **Debug**客户端目标构建，修复新增佣兵名单事实遗漏调试事件名称导致的编译断言。沿共享构建配置交付，不标为Release；未构建独立PvPGN目标。客户端包为`dist/current`，EXE SHA256为`9A806F06906F31B28C5C4C1531CD24C6C9DF9A0292596295CC45C13D3773AEB4`，网络DLL为`72AD07696429BB5A1A35D1EAE9CBB97E7F91B828E02885933FAE0022C0B1274B`。构建交付时未运行游戏或测试；后续佣兵有限冒烟及阻塞见下段。既有任务内容／NPC视图警告仍在，不宣称零警告或整体运行验收。源码提交排除另一任务的独立PvPGN服务及端口变更，当前工作树构建包含并行共享入口改动，包不能视作仅由该提交重现的纯净产物。

罗格当前已接卡夏雇佣名单／替换／复活、头盔／护甲／弓、腰带／光标喂药、经验升级、本人属性与装备原包投影。主人死亡由death在当前MPQ死亡动画结束帧统一触发佣兵／召唤物死亡，保留佣兵死亡记录；回城及死亡保存等待事务完成。2026-10-09至10-10对上述Debug包执行有限Single Player冒烟：名单／雇佣／替换、腰带喂药、换区同行、延迟死亡、费用／复活、经验奖励及存活／死亡新进程恢复取得代表证据；换装与光标喂药被客户端NPC服务范围检查阻塞，自然射击未取得命中，下一等级经验显示0。完整观察、源码原因、证据目录与未覆盖边界见[佣兵](docs/gameplay/characters/HIRELINGS.md#当前包有限冒烟)，不标整体通过；本轮未重新构建／打包。旧master规则与reference核对入口、特殊装备效果及多人候选竞争限制也由该页维护。磁盘仍为D2S v96，佣兵冒烟包规则v28；后续服务端规则v29增加PvPGN新角色登记入口，当前v30增加玩家交易／聊天边界，旧运行证据不认证新增路径。

上一批独立PvPGN服务端`d2x_pvpgn`（输出`d2x_server.exe`）于2026-10-10按共享Windows **Debug**配置完成目标增量构建及独立打包。当批EXE SHA256为`4DAA960EE66F2236C92879FCC33C60EE6218C5EC32D5DCECB49B24A3FD890A1C`，目录`dist/server`；当批客户端仍使用上述原Debug包，未覆盖`dist/current`。本机PvPGN PRO 1.99.7.2.1冒烟取得注册／创角／选角、建加房、双客户端地图／名册／移动／聊天、库存取放、大厅列表／详情、双房隔离、定时／正常退局保存、在线停服及重启重入、密码拒绝与断线保存代表证据。修复D2CS初始化第二字符串、新角色短登记与PowerShell停服记录／退出确认；规则v29，磁盘v96。证据在忽略目录`artifacts/pvpgn-server-smoke-20261010`，先备份数据库；未修改原PvPGN配置／脚本或停后端进程。空目录首次列表超时仍保留；后端事务串行等待、无自动重连／签名／反作弊，跨机器、高负载、后端失败／崩溃恢复和完整原版客户端未认证，不能宣布完整原D2GS替代。准确观察与限制见[PvPGN服务端](docs/development/PVPGN_SERVER.md#有限冒烟)。

怪物／NPC／物品协作重构已随当前客户端构建：非玩家实体受伤／攻击能力由Actor集中表达并由技能和命中复用；商店／任务加工共用npc服务访问但每阶段复验租约，普通／个人赌博货架查询收敛；客户端阵营保留Unknown，未知不授予友方技能资格，物品主人／光标查询要求已知玩家GUID。未迁移静态NPC至敌怪AI，未运行认证；入口见[怪物](docs/modules/MONSTERS.md)、[NPC](docs/modules/NPC_QUEST.md)、[库存](docs/modules/INVENTORY.md)。

技能分派重构已随当前客户端构建：服务端普通释放改为具名程序策略表，跨领域处理器独立到activation，装备触发复用分类但保留上下文资格／执行重试；客户端CltDo能力集中登记，互斥逐帧行为单次分派。未知技能行为明确NotImplemented，不增加技能；未运行认证。职责与扩展约定见[技能公共规范](docs/gameplay/skills/COMMON.md#程序分派与扩展)。

此前消息系统重构Release包EXE SHA256为`0A7A103718F85D03DCD071E53EB07FD252E92B5D2541D3C0A92D7D563B76FF0C`，网络DLL为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`，当时规则v27／D2S v96。该包未运行游戏或测试，不代表当前佣兵交付。

两端共享network/protocol的73项C2S和142项S2C身份／分帧定义；客户端发包与宿主收包共用C2S校验，宿主回包与客户端分帧共用S2C元数据。目录有编译期唯一性／完整性约束；新增11个C2S具名无副作用stub，补49项S2C身份预留；未知长度、Warden及B3仍拒绝，不能当作玩法完成。分派统一检查剩余字段，聊天扩展格式与未支持语义分开处理。server-protocol补阶段／分帧／长度元数据，4F为partial。消息列表、编码与参数唯一维护于[消息速查](docs/modules/MESSAGES.md)，结构边界归[服务端协议](docs/modules/SERVER_PROTOCOL.md)。

### 此前技能表现包

2026-10-09此前通用施放表现包完成Windows Release构建、打包及有限自研／原D2GS冒烟，当时位于`dist/current`，保存规则为v27／D2S v96。历史EXE SHA256为`BF767C41680FAD44DD825B8BD877D82D30629C03CCB0B45A79038243FD55A52A`；网络DLL为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。证据在忽略目录`artifacts/skill-presentation-20261009`，未新增测试程序或提交Git，不认证后续包。

按原Rcv0x12语义修复鼠标松开误删普通动作／待释放飞弹，并使背包开门已发送意图进入同一表现链；原函数、入口及所有权见[COMMON](docs/gameplay/skills/COMMON.md#定义来源与客户端状态)。不增加服务端类型分支或私有包。调试管道新增只读localCast／clientMissiles诊断。

最终包的Single Player用实际UI按下后立即松开，观察Fire Ball62、Ice Bolt59及Frozen Orb260／261生成、移动与散射，并有原图截图；火球命中管理入口准备的真实zombie1，生命7→0。Inferno开始后有10个视觉弹体，Stop后本人施法为空、视觉／权威弹体及pendingReleases清零。背包开门共用入口消费卷轴并生成蓝门。最终host failures=0，ignoredPackets=0，mapErrors／effectLimitations为空，stderr为空。

同一最终包连接本机1.13c D2GS／PvPGN参考栈，在冰冷之原用实际UI快速点击复查Fire Ball／Ice Bolt，Stop后仍观察到原飞弹和截图；`online-town-portal`共用背包入口观察本人219施法、数量1→0、原59门对象及蓝门图形。原服既有ignoredPackets=16，mapErrors／effectLimitations为空、stderr为空。前一连接曾只收到AF后加载超时，重新选角建房恢复；未认定修复其原因。以上为代表路径有限V2，未认证全部职业／参数、多人、三难度、精确逐帧／像素或Linux；旧任务证据不因此扩大范围。

## 此前任务运行证据

| 历史包 | 覆盖与证据入口 |
| --- | --- |
| v27第三至第五幕 | 15项个人任务主流程、加工奖励、旅行与新进程恢复；历史EXE为`9440CD86160F5635C2921F14AA37F2D053B75D91D84383C29C46675D96E8F5E4`，最终构建`build-14.log`及证据在`artifacts/act3-5-quests-20261009`。管理换区／补材料／击杀只准备条件，不是正常通关认证；范围与限制见[ACT3–5](docs/gameplay/quests/ACT3_5.md#依据与运行证据) |
| v24第一幕 | 六项个人主流程、城镇奖励服务、Warriv往返与新进程恢复；原条件、包摘要与未覆盖范围见[ACT1](docs/gameplay/quests/ACT1.md#有限运行证据)，忽略目录`artifacts/act1-quests-20261009` |
| v26第二幕 | 材料／技能书、日食、日志／真墓、插杖／开墙、救援／Meshif往返和新进程恢复；同包复验Cain339说明位及准备等待。原条件、包摘要与未覆盖范围见[ACT2](docs/gameplay/quests/ACT2.md#有限运行证据)，忽略目录`artifacts/act2-quests-20261009`及`artifacts/act2-recheck-20261009` |

以上仅覆盖对应历史源码与列出路径，不能认证当前新增分支、完整首领AI／正常通关、队伍、三难度、原服或Linux。传送点运行范围与未覆盖边界统一见[物件](docs/gameplay/world/OBJECTS.md#自研传送点)。

## 此前物品批次与有限冒烟

历史v23物品经济包及v20物品收尾的包身份、角色条件、实际路径、失败样本与未覆盖范围统一见[物品证据](docs/gameplay/items/EVIDENCE.md)。该页保留充能Hydra、自恢复拾取、金币、商店周期／维修、过期、Token与保存的有限记录；旧包当时位于dist/current不能作为当前包身份。

任务物品／加工／永久消耗的后续v24／v26／v27证据链接各幕任务，不把管理准备当正常战斗通关。当前原D2S v96／规则指纹不变；完整物品／属性／配方、特殊身份样本、三难度与多人仍未全量验收，P5保持开放。

### 历史物品与掉落有限冒烟

v20自然普通掉落、普通方块／Crafted、Stealth／回购、AutoMagic、赌博／批量与维修、保存恢复的准确范围见[物品证据](docs/gameplay/items/EVIDENCE.md#历史物品与掉落有限冒烟)，不在基线复制历史台账。当前分类、规则与缺口见[物品目录](docs/gameplay/items/README.md)，模块仅维护入口。

### 历史第一幕全部怪物证据

2026-10-09第一幕全部可击杀怪物收尾：63战斗类型／25类AI、十三SuperUniques、精英阶级／继承、首领技能与独立效果时钟。旧master提供迁入起点，当前MPQ、本地D2MOO及原1.13c客户端静态依据用于核对。修复A1技能释放事件、原生命触发位／GH生命槽及受击重复同步；补电／冰强化原客户端回调，两种服务端共用同一客户端。数据、共享函数、协议与未完成边界由[怪物目录](docs/gameplay/monsters/README.md)链接唯一负责专题；模块页只维护代码入口。该怪物批次未改变当时v19准入规则，磁盘仍为D2S v96。

Windows Release最终构建成功，运行包为`dist/current`。EXE SHA256为`95624C814123FCA8436F8C524D4A2E5843C4A3B53E7088DC8681924F8675E945`，网络DLL为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。最终增量构建无编译warning／error；早期诊断及修复日志仍保留，不能把整个批次概括为无warning。构建、准入、原包、截图及保存证据在忽略目录`artifacts/act1-all-monsters-closeout-20261009`，没有新增测试脚本、用例或程序。

普通难度临时92级女巫：正常走跑营地→鲜血荒地，六种新增战斗类型／十三固定超级暗金准入；代表首领技能、电／冰强化、原C2S冰霜新星伤害／减速和保存新进程重入完成有限冒烟。最终宿主failures=0、characterIssues为空，客户端ignoredPackets=0、unavailableUnits=0、mapErrors／effectLimitations为空，stderr为空。这是列出路径的有限V2，其余分支V1；没有本批完整D2GS回归、噩梦／地狱、多人／背压或Linux认证。

### 历史普通怪物证据

57普通身份／19类AI代表动作、复活／巢生／蛛网及普通原服样本保留在`artifacts/act1-monsters-20261008`与`artifacts/act1-monsters-closeout-20261009`。准确范围、失败样本及限制统一见[第一幕怪物证据](docs/gameplay/monsters/ACT1.md#有限运行证据)；历史V2不认证本次新增分支。

### 历史通用十项有限冒烟

2026-10-08通用十项包，EXE SHA256 `122CE3973A21D2C258CADE81B737A106DD864710198144DA7F1E0E4E73C92866`。普通难度41级临时亚马逊，有限验证普通近战／弓箭／标枪、火毒药瓶、本人女武神Unsummon、鉴定／回城卷轴与书本、保存新进程恢复；普通桶KK、左手及本批原服／多人没有运行认证。角色条件、具体JSON及未覆盖项只维护在[GENERAL](docs/gameplay/skills/GENERAL.md#有限运行证据)，证据保留在忽略目录 `artifacts/common-skills-smoke-20261008`。历史包不认证当前新增分支。

### 历史亚马逊有限冒烟与原服回归

对应历史提交53579f3，非当前包。2026-10-08自研41级Hero（种子3739460588，30项基础等级1）有限验证魔法／多重／导引／牺牲箭、戳刺、闪电之怒、伙伴和保存；原服AmaSmokeOct验证多重箭、导引箭、诱饵／女武神及闪电之怒代表路径。历史EXE SHA256为`69F50C17AE89D063C8D81333139D820FF63EB17B34466F79535FF010312B4C37`，当时规则v18。证据在`artifacts/amazon-smoke-20261008`和`artifacts/amazon-d2gs-20261008`；原包差异、一次原服watchdog中断及女武神死亡图缺口仅维护在[亚马逊](docs/gameplay/skills/AMAZON.md#有限运行证据)。

### 其他历史有限证据

| 历史源码 | 实际范围／记录入口 |
| --- | --- |
| 2fcc439女巫 | 普通41级Hero、种子1961888707；30项学习保存、列出的施法／状态／召唤及同机TCP观察；原服99级SkillTestSor代表技能及常规73／本人4C发包收敛。详细数值、原版依据及限制见[女巫证据](docs/gameplay/skills/SORCERESS.md#历史女巫有限运行证据)，忽略目录sorceress-smoke-20261008／sorceress-d2gs-20261008 |
| 8623446六项基础 | 掉落／金币／鉴定、生命药水、普通难度死亡复活／完整拾尸、普通物件、买卖／修理／仓库、洞穴奖励和本人回城门／保存。普通箱桶不等于陷阱、爆炸桶或复杂机关完成；记录在artifacts/p1-smoke-20261008 |
| 873ac34基础闭环 | 库存／腰带／装备／切组、升级／加点／学技、营地→鲜血荒地→洞穴、普通近战／初始法术、保存跨进程。同机TCP双客户端同房互见／走跑、人物成长隔离及嵌入宿主两房基础观察；未认证独立EXE或跨机器。记录在artifacts/closeout-smoke-20261008 |
| 既有原服客户端 | 双账号同房互见、Join详情／密码、原图HUD／树／NPC菜单、物品／金币交易及保存重入等有限观察；准确包身份、失败样本和未覆盖范围由[NETWORK](docs/modules/NETWORK.md#当前批交付)维护 |

历史证据只覆盖对应源码和列出的路径，不认证当前新增功能。更早骨架／行走记录留原忽略目录。原服既有未知包、一次D2GS死锁提示或握手／绑定失败、长绕墙无进展及新角技能初始owner=0仍有诊断，不将重启／重新选择后的成功算作原因已修复。七职业／五幕三难度／全部多人、像素／音频及长期稳定性未完整验收。

## 客户端表现与维护入口

原地图、真实GUID和原图UI共用现有客户端。人物面板、技能提示／树、库存、NPC菜单、交易、HUD、聊天、祭坛／野外物件、声音和照明的规则／限制分别维护在[人物](docs/modules/CHARACTER.md)、[HUD](docs/gameplay/ui/CLASSIC_HUD.md)、[库存](docs/modules/INVENTORY.md)、[NPC／任务](docs/modules/NPC_QUEST.md)、[物件](docs/gameplay/world/OBJECTS.md)及[联网](docs/modules/NETWORK.md)。已有定义和协议请求不等于全部报价／赌博组合、佣兵、全部任务／物件或七职业视觉完成；晚入视野不重播历史施法／弹体，宠物头像HUD未接。

自研宿主手动丢弃物品／金币采用事务发布掉落事实，再编码原0x9C action=2，触发现有客户端的MPQ翻转动画及掉落音效；静态地面同步仍用action=0。该修正已随当前完整Release构建入包，未在怪物批次专项运行；怪物／箱桶新生掉落也已接同一事件。细节见[库存](docs/modules/INVENTORY.md)。

原MPQ位于`assets/mpq2`；先查本地reference和当前资源，来源／固定版本／许可见[资料来源](docs/resources/THIRD_PARTY.md)、[MPQ](docs/resources/MPQ.md)。保留原资源、reference、旧压缩包和mvp；资源、缓存、截图、存档、运行包及参考仓库不入源码提交。独立d2x_assets保留原资源／地图报告及save-info工具。

[构建运行](docs/development/BUILD_AND_RUN.md)维护启动与打包；[调试管道](docs/development/DEBUG_PIPE.md)维护既有诊断。默认只修改源码／文档，构建、打包、运行按有效用户授权；历史离线结果不认证当前联机产品。
