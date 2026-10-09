# 项目基线

更新：2026-10-09。开始修改前阅读[AGENTS](AGENTS.md)与[对应模块](docs/README.md)，实现状态以当前源码为准。历史过程由Git追溯，技能原规则／运行证据由专题维护，后续顺序见[总计划](docs/architecture/MULTIPLAYER.md)。

## 产品与代码边界

D2X提供Single Player、TCP/IP局域网宿主与既有原服入口。选角至入局只有一套客户端：RealmFrontend → RealmSession／RemoteWorld → RemoteTown／RemoteUiClients／RemoteScene → SceneController／SceneView。自研宿主收发原1.13c MCP／D2GS包，内存字节队列和TCP共用服务；原服账号走PvPGN，自研Realm预认证。断线不切换权威，客户端不读取D2S或执行旧单机规则。

服务端拆为hosting协议／内容准备／存储／实例调度及server领域。GameInstance只组装AreaStore、PlayerStore、GameSystems、类型化命令FIFO、25Hz固定步和可靠事件；状态由各领域持有，窄Ports协作，不恢复万能GameSession。内核不读取MPQ、设备、GPU、socket、文件或Win32。两端共用NativeMapGenerator和原生成／reveal函数，运行状态独立；原包投影不替代原地图。入口见[架构](docs/architecture/OVERVIEW.md)、[数据流](docs/architecture/DATA_FLOW.md)、[内核系统](docs/modules/SERVER_SYSTEMS.md)。

有参考依据、匹配原版的公共函数提取及客户端修复已获授权；每处记录MPQ／原包／原函数证据。禁止为兼容自研宿主单独改客户端或增加私有协议。客户端仅以已知本人属性计算显示，不推算远端隐藏值。先参考master的单机实现，再用当前MPQ及本地reference补证据；D2Game权威执行不等于完整技能两端共用。职责依据见[参考设计](docs/architecture/REFERENCE_DESIGN.md)。

## 当前源码范围

| 领域 | 已执行范围 | 主要限制 |
| --- | --- | --- |
| 局前／传输 | 原服认证、MCP角色／房间；服务端D2S角色列表／建删选；内存与LAN TCP | 独立账号及无图形宿主、Ladder、完整错误恢复未完成 |
| 世界／旅行 | 原五幕1–136生成器；区域准备、碰撞、走跑／边界及UNIT_TILE；传送点、本人双向回城门、任务红／蓝门、Warriv／Meshif双向跨幕及第二幕机关旅行／真墓开墙 | 长绕墙／动态拥挤、完整房间活动／兴趣、完整特殊任务内容、剧情演出及队友门未完成 |
| 库存／成长 | 背包／Cursor／腰带／装备／切组／合堆／书本转装、镶嵌／普通方块／全品质生成；装备总值、经验升级、加点学技／热键、永久奖励、第一幕任务物品与Akara免费重置 | 复杂容量／争用、全部职业属性／奖励、复杂多人任务资格未完成 |
| 战斗／技能 | 普通空手／近战／弓弩、投掷／左手动作、六通道；药瓶、隐藏Kick、Unsummon及四项卷轴／书本；女巫26主动／4被动、亚马逊24主动／6被动 | 其他职业主动、完整双持组合、吸血／吸魔／压碎／撕裂与触发、受击恢复／格挡／耐久完整规则、PvP未完成 |
| 怪物／伙伴 | 第一幕63可击杀战斗类型／25类AI，含57普通池身份、十三固定超级暗金、冠军／暗金／仆从、五首领策略及石像鬼陷阱；精英数值／光环／诅咒／死亡事件、原rank投影；Hydra、诱饵／女武神及血乌奖励罗格的归属／AI／换区与内容准备 | 其他幕怪物、完整雇佣／复活／装备／成长及其他召唤未迁入；首领／精英代表路径有限V2；全部随机分支、精英染色／专用死亡演出及完整首杀／任务掉落未认证；完整Vision／房间调度／拥挤及原版随机流顺序未复刻；女武神重甲死亡组件待原版核实 |
| 掉落／资源／死亡 | 普通掉落拾取、金币丢弃／部分拾取；自然恢复、药水／定时效果；死亡惩罚、城镇复活、本人尸体回收 | 近队友NoDrop／任务掉落、全部负面状态清除、硬核／部分拾回／多尸体及异常退出边界未完整认证 |
| 物件／NPC／任务 | 普通门箱桶、部分神殿／水井；静态NPC、普通买卖／回购、个人赌博／批量、鉴定／充能修理／仓库；五幕27项任务个人主流程的资格、机关／任务物品／奖励／旅行与存档 | 陷阱／爆炸桶／其他特殊机关、完整城镇AI、完整后三幕怪物、城镇演出与组队共享未实现；准确范围见[ACT1](docs/gameplay/quests/ACT1.md)、[ACT2](docs/gameplay/quests/ACT2.md)、[ACT3–5](docs/gameplay/quests/ACT3_5.md) |
| 多人 | 1–8人同局名册、走跑／装备外观／聊天；GameHandle多实例、独立连接／租约及定向私有事实 | 队伍、交易权威、PvP、跨机器／长期稳定性未验收；嵌入宿主双房观察不能替代独立EXE多房验收 |

所有暂缓意图显式拒绝／NotImplemented，不用普通攻击替代技能、不漏条件发奖。邪恶洞穴缺少敌对怪物规则时，仅用已授权沉沦魔替身并保留真实身份；中立不可替换。缺规则／无法放置的人口保留未完成数；清场资格仅给当时在洞内的人物。不因此宣称缺失类型完成；第一幕本轮新增范围及限制见怪物模块。

Kick（ID1）是原表隐藏通用程序，SkillDesc.ListRow=-1；各职业破普通木桶时自动KK，不是其他职业可学习的踢腿技能。刺客职业踢腿另属职业技能。木桶OperateFn5调用skills窄入口；objects／loot仍执行破坏与掉落。十项依据、公共武器／药瓶与物品技能入口见[GENERAL](docs/gameplay/skills/GENERAL.md)，全部技能状态及迁入规范见[COMMON](docs/gameplay/skills/COMMON.md)。定义／树／提示可用不等于职业执行或视觉完成。

库存／人物事务同时复验revision，一次发布物品／人物事实后提交持久值、资源与总值。技能开始与释放分别复验装备、目标、区域及资源；背压重试不重复扣费或掷伤。伙伴内容准备在hosting，状态及种子由独立领域持有。详见[库存](docs/modules/INVENTORY.md)、[人物](docs/modules/CHARACTER.md)、[内核](docs/modules/SERVER_SYSTEMS.md)。

P1日常操作已收敛：地面装书／部分合并／自动装备，脱带／卖带余药落地，方块目标鉴定，NPC距离／视线与交谈revision绑定，拾取／拾尸／复活背压等待、唯一物品限制与死亡落地；药水原8.8剩余值／帧合并及双倍判定、满值／到期／死亡清理、NPC按MPQ清毒／冻结／curable状态。尸体沿原回收与第一具非空尸体写档，热键补原清除编码。P1未改客户端，随P5统一构建打包；金币丢弃／拾取及自恢复物品的等待拾取已有下列有限V2，其余新增路径仍为V1。

P5物品侧新增原38／58任务加工、operation28组装、牛门／Pandemonium真实地图及材料事务、任务消耗与Token、充能技能源／扣费、装备六类触发事件、商店周期、地面自恢复／过期与MonStats首杀TC选择。客户端仅补原协议已有的任务加工与充能来源／显示，同样用于D2GS；不增加自研宿主分支。公共恢复规则、按物品GUID持有的时钟、独立effects触发队列及原子事务保持领域分工。准确范围与依据见[库存](docs/modules/INVENTORY.md)。P5仍有前置任务／队伍／其他职业与专用触发程序依赖，未达到整体可玩和验收条件，不能标为全面完成。

## 入口与保存

Single Player使用原MCP角色界面，选角后自动建普通单人房、跳过大厅；首页TCP/IP Game使用原MPQ Host／Join页面，本机IPv4由应用提供，Join默认选中127.0.0.1。Host选角后自动建角色名普通8人房，跳过大厅；Single Player／LAN选角不显示Realm控件。界面Host监听全部IPv4，默认MCP6113／GS4001；LAN Join同用4001，原服默认GS4000。自定义端口及`--host-lan`／`--lan`仍保留，加入回包使用连接实际到达的接口地址。返回菜单不停止其他玩家房间。连接失败显示实际IP／端口及底层原因，玩法不分连接类型。见[联网](docs/modules/NETWORK.md)。

自研存档属hosting／persistence，D2S v96不变，当前源码规则指纹为`d2x-character-admission-v27/native-wire113c/d2s96/act3-5-quests`。整局角色锁、校验后原子替换／.bak、失败保留实例与租约；旧规则不静默迁移。F11保存、Ctrl+F11校验后原协议退局重入及`--load`／`--save`入口保留。技能选择／热键、扣费后的资源、弹药／书页沿原字段保存；派生总值、动作、弹体、召唤、门户及短时效果不写盘。限制见[存档](docs/modules/SAVES.md)。

私有一人实例支持ESC／失焦自动暂停，共享房间／原服继续推进。现有named pipe提供权威快照／有界历史、时钟覆盖、保存／重载、资源／经验／金币、MPQ物品／怪物准备等管理入口；正常玩法仍发送原online-*包。消息目录及stub不代表全部实现。接口见[调试管道](docs/development/DEBUG_PIPE.md)和[服务端协议](docs/modules/SERVER_PROTOCOL.md)。

<a id="当前运行包与有限冒烟"></a>

## 当前运行包与有限冒烟

2026-10-09第三至第五幕15项个人任务主流程及相关内容准备缺口已完成Windows Release构建、打包和普通单人有限冒烟，当前运行包为v27，位于`dist/current`。EXE SHA256为`9440CD86160F5635C2921F14AA37F2D053B75D91D84383C29C46675D96E8F5E4`，与`build/bin`一致；网络DLL仍为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。最终构建`build-14.log`及证据位于忽略目录`artifacts/act3-5-quests-20261009`。

已验证第三幕古书／黄金鸟／基德宾奖励、意志破珠与Mephisto旅行，第四幕Izual／熔炉／五封印与跨幕，第五幕打孔／15囚犯／安雅双奖励／署名／古代人经验与回城重置／巴尔五波及最终出口；新进程恢复任务、永久奖励、加工物品和原progression。管理员换区、补材料与击杀仅准备条件，非正常战斗通关认证；完整材料链、多人、三难度及较大演出／AI缺口见[ACT3–5](docs/gameplay/quests/ACT3_5.md#依据与运行证据)。最终恢复入局ProtocolReady，地图／移动／玩家显示可用，host失败、存档问题、ignoredPackets、mapErrors和effectLimitations均为空或0。

## 此前任务运行证据

| 历史包 | 覆盖与证据入口 |
| --- | --- |
| v24第一幕 | 六项个人主流程、城镇奖励服务、Warriv往返与新进程恢复；原条件、包摘要与未覆盖范围见[ACT1](docs/gameplay/quests/ACT1.md#有限运行证据)，忽略目录`artifacts/act1-quests-20261009` |
| v26第二幕 | 材料／技能书、日食、日志／真墓、插杖／开墙、救援／Meshif往返和新进程恢复；同包复验Cain339说明位及准备等待。原条件、包摘要与未覆盖范围见[ACT2](docs/gameplay/quests/ACT2.md#有限运行证据)，忽略目录`artifacts/act2-quests-20261009`及`artifacts/act2-recheck-20261009` |

以上仅覆盖对应历史源码与列出路径，不能认证当前新增分支、完整首领AI／正常通关、队伍、三难度、原服或Linux。传送点运行范围与未覆盖边界统一见[物件](docs/gameplay/world/OBJECTS.md#自研传送点)。

## 此前物品批次与有限冒烟

2026-10-09 P1／P5及原生物品身份布局收尾已完成Windows Release构建，运行包在`dist/current`。EXE SHA256为`740E8E7B392BFBF0DB4E17F28A78AF4945439074058991E812AAA10D4094C3C8`，网络DLL为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`；包与`build/bin`一致。最终构建成功，证据在忽略目录`artifacts/items-economy-smoke-20261009`。打包曾因退局后进程尚未退出而失败，等待退出后成功，失败日志保留。没有新增测试脚本、用例或专用程序。

普通难度临时女巫Hero，地图种子3666265640；经验／金币和MPQ物品由既有管理入口准备，正常操作仍发原C2S。最终包的新进程实际验证如下，列出路径为有限V2，其余分支仍为V1：

| 实际路径 | 有限证据 |
| --- | --- |
| 原充能技能源／施法／热键 | Hexfire鉴定装备后，Hydra为6级、36次；营地正常走跑到鲜血荒地施法，充能36→35，法力不变。保存后新进程把来源重新绑定至GUID16，右技能与热键2一起恢复。final-reloaded-snapshot／items |
| 自恢复期间等待拾取 | Titan’s Revenge在地面自补充，人物靠近期间属性revision继续变化；原请求成功拾取，两端数量均为4。修复此前属性版本变化导致待拾取请求取消，改用独立的地面生命周期代次。final-restored-pickup／snapshot；旧失败样本保留 |
| 金币丢弃／拾取 | 原C2S丢100金币并拾回，钱包恢复500000。final-gold-dropped／recovered |
| 商店周期与充能修理 | Charsi货架63件在6000权威帧后重开为61件，与旧货架无GUID重叠；Hexfire报价6133，金币500000→493867，充能35→36。final-shop-before／after、final-repair-quote／repaired |
| 地面过期边界 | 普通hp1超过15000帧后两端移除；任务方块仍在地面且可解码。final-expiry-before／after／snapshot。没有验证所有品质、金币阈值或区域卸载分支 |
| Token与保存重入 | 学习Fire Bolt后使用Token，退回375属性点及75技能点，清基础技能、选择与16个热键；保存后新进程恢复同值，金币493867、Hexfire36次。final-token-before／after、final-save-info、reentry-snapshot／status |

最终新进程ProtocolReady，8件物品均decoded=true；ignoredPackets=0、unavailableUnits=0、mapErrors为空；宿主failures=0、characterIssues为空、stderr为空。已有组件着色／无形透明表现限制仍在effectLimitations中，不把它记为空或归因于本次协议。构建后的`save-info`保留原只读检查，并增加耳朵／Realm／Tempered／普通身体部件数量摘要。

此前物品批次指纹为v23，原磁盘版本仍为D2S v96。耳朵、身体部件、Realm尾部及Tempered布局已有源码支持，未取得符合准入条件的真实特殊身份存档进行往返；既有EpicSorc样本因Hardcore／dead／Ladder资格被拒绝，不能算布局验证。原38／58任务加工成功、全部方块配方、牛门／Pandemonium、首杀及特殊奖励、三难度、多人争用／背压、原D2GS与Linux均没有该批运行认证。第一幕资格／奖励及灌注本次证据见ACT1，其他幕、队伍NoDrop、其他职业及专用物品触发程序仍有依赖；[P5](docs/architecture/MULTIPLAYER.md#p5完整物品配方与城镇经济)保持未完成状态。

### 历史物品与掉落有限冒烟

此前v20物品包的有限V2证据保留在忽略目录`artifacts/items-closeout-20261009`：普通方块／Crafted、Stealth镶嵌及回购、AutoMagic、个人赌博、批量书本／药水、耐久维修、自然普通掉落拾取与保存重入。其源码／包身份及失败样本留原日志；历史成功不认证本轮新增分支。当前功能、原规则依据与限制只维护在[库存模块](docs/modules/INVENTORY.md)，避免重复记录旧范围或把旧数值当作当前认证。

### 历史第一幕全部怪物证据

2026-10-09第一幕全部可击杀怪物收尾：63战斗类型／25类AI、十三SuperUniques、精英阶级／继承、首领技能与独立效果时钟。旧master提供迁入起点，当前MPQ、本地D2MOO及原1.13c客户端静态依据用于核对。修复A1技能释放事件、原生命触发位／GH生命槽及受击重复同步；补电／冰强化原客户端回调，两种服务端共用同一客户端。数据、共享函数、协议与未完成边界仅维护在[怪物模块](docs/modules/MONSTERS.md)。该怪物批次未改变当时v19准入规则，磁盘仍为D2S v96。

Windows Release最终构建成功，运行包为`dist/current`。EXE SHA256为`95624C814123FCA8436F8C524D4A2E5843C4A3B53E7088DC8681924F8675E945`，网络DLL为`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。最终增量构建无编译warning／error；早期诊断及修复日志仍保留，不能把整个批次概括为无warning。构建、准入、原包、截图及保存证据在忽略目录`artifacts/act1-all-monsters-closeout-20261009`，没有新增测试脚本、用例或程序。

普通难度临时92级女巫：正常走跑营地→鲜血荒地，六种新增战斗类型／十三固定超级暗金准入；代表首领技能、电／冰强化、原C2S冰霜新星伤害／减速和保存新进程重入完成有限冒烟。最终宿主failures=0、characterIssues为空，客户端ignoredPackets=0、unavailableUnits=0、mapErrors／effectLimitations为空，stderr为空。这是列出路径的有限V2，其余分支V1；没有本批完整D2GS回归、噩梦／地狱、多人／背压或Linux认证。

### 历史普通怪物证据

57普通身份／19类AI代表动作、复活／巢生／蛛网及普通原服样本保留在`artifacts/act1-monsters-20261008`与`artifacts/act1-monsters-closeout-20261009`。准确范围、失败样本及限制统一见[怪物模块](docs/modules/MONSTERS.md#有限运行证据)；历史V2不认证本次新增分支。

### 历史通用十项有限冒烟

2026-10-08通用十项收尾后，Windows Release最终构建成功并更新`dist/current`，包括此前未提交的局前／端口／连接错误详情。EXE SHA256：`122CE3973A21D2C258CADE81B737A106DD864710198144DA7F1E0E4E73C92866`；网络DLL：`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。最终增量构建无warning／error；早期构建的诊断及修复不删，不能把整个批次概括为无warning。构建及运行证据保留在忽略目录`artifacts/common-skills-smoke-20261008`；没有新增测试脚本、用例或程序。

本批在临时存档副本上，以同一客户端原C2S完成普通近战／标枪投掷、普通弓箭最后一箭与空弹药拒绝、火／毒药瓶、本人女武神Unsummon、鉴定卷轴／书本原3F及27、书本转装、回城卷轴／书本消费和门户替换。保存后最新EXE新进程入局ProtocolReady，标枪58、箭347、两本书各5页及鉴定结果恢复；消耗品／召唤不恢复。最终宿主failures=0、characterIssues为空，客户端ignoredPackets=0、stderr为空。条件、数值和具体JSON名只维护在[通用技能证据](docs/gameplay/skills/GENERAL.md#有限运行证据)。

普通桶KK和左手两项没有本批运行覆盖；自研地图本轮没有可操作普通桶样本，不制造物件。弩／全部投掷物、空书／多人／取消／背压、原服回归和Linux未认证。最新包仅复查Single Player保存自动入局，界面Host／Join改动已入包而未重跑。代表路径是有限V2，其余V1，不把全部十项或战斗整体标V3。

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
