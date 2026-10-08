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
| 世界／旅行 | 原五幕1–136生成器；区域准备、碰撞、走跑／边界及UNIT_TILE；传送点、本人双向回城门 | 长绕墙／动态拥挤、完整房间活动／兴趣、NPC跨幕与特殊资格、队友门未完成 |
| 库存／成长 | 背包／Cursor／腰带／装备／切组／合堆／书本转装；装备总值、经验升级、加点学技／热键、永久奖励与已支持被动 | 全品质／套装、复杂容量／争用、全部职业属性／奖励、重置与工艺未完成 |
| 战斗／技能 | 普通空手／近战／弓弩、投掷／左手动作、六通道；药瓶、隐藏Kick、Unsummon及四项卷轴／书本；女巫26主动／4被动、亚马逊24主动／6被动 | 其他职业主动、完整双持组合、吸血／吸魔／压碎／撕裂与触发、受击恢复／格挡／耐久完整规则、PvP未完成 |
| 怪物／伙伴 | 第一幕57普通身份／19类AI、A1／A2／远程与元素冷毒、萨满复活、巢出生、蛛网、GH／BL与实际盾牌、区域组件组合、初次察觉射线／追击、原命中类型；Hydra、诱饵／女武神归属、期限／AI／换区及装备准备 | 精英／首领、其他幕怪物、佣兵及其他召唤未迁入；完整Vision／房间调度／拥挤及原版随机流顺序未复刻；女武神重甲死亡组件待原版核实 |
| 掉落／资源／死亡 | 普通掉落拾取、金币丢弃／部分拾取；自然恢复、药水／定时效果；死亡惩罚、城镇复活、本人尸体回收 | NoDrop人数／任务掉落、全部负面状态清除、硬核／部分拾回／多尸体及异常退出边界未完整认证 |
| 物件／NPC／任务 | 普通门箱桶、部分神殿／水井；静态NPC、普通买卖、鉴定／修理／仓库；邪恶洞穴击杀、个人资格、Akara奖励／存档 | 陷阱／爆炸桶／特殊机关、完整城镇AI、赌博／批量／充能修理、其余任务及组队共享未实现 |
| 多人 | 1–8人同局名册、走跑／装备外观／聊天；GameHandle多实例、独立连接／租约及定向私有事实 | 队伍、交易权威、PvP、跨机器／长期稳定性未验收；嵌入宿主双房观察不能替代独立EXE多房验收 |

所有暂缓意图显式拒绝／NotImplemented，不用普通攻击替代技能、不漏条件发奖。邪恶洞穴缺少敌对怪物规则时，仅用已授权沉沦魔替身并保留真实身份；中立不可替换。缺规则／无法放置的人口保留未完成数；清场资格仅给当时在洞内的人物。不因此宣称精英或其他原类型完成。

Kick（ID1）是原表隐藏通用程序，SkillDesc.ListRow=-1；各职业破普通木桶时自动KK，不是其他职业可学习的踢腿技能。刺客职业踢腿另属职业技能。木桶OperateFn5调用skills窄入口；objects／loot仍执行破坏与掉落。十项依据、公共武器／药瓶与物品技能入口见[GENERAL](docs/gameplay/skills/GENERAL.md)，全部技能状态及迁入规范见[COMMON](docs/gameplay/skills/COMMON.md)。定义／树／提示可用不等于职业执行或视觉完成。

库存／人物事务同时复验revision，一次发布物品／人物事实后提交持久值、资源与总值。技能开始与释放分别复验装备、目标、区域及资源；背压重试不重复扣费或掷伤。伙伴内容准备在hosting，状态及种子由独立领域持有。详见[库存](docs/modules/INVENTORY.md)、[人物](docs/modules/CHARACTER.md)、[内核](docs/modules/SERVER_SYSTEMS.md)。

## 入口与保存

Single Player使用原MCP角色界面，选角后自动建普通单人房、跳过大厅；首页TCP/IP Game使用原MPQ Host／Join页面，本机IPv4由应用提供，Join默认选中127.0.0.1。Host选角后自动建角色名普通8人房，跳过大厅；Single Player／LAN选角不显示Realm控件。界面Host监听全部IPv4，默认MCP6113／GS4001；LAN Join同用4001，原服默认GS4000。自定义端口及`--host-lan`／`--lan`仍保留，加入回包使用连接实际到达的接口地址。返回菜单不停止其他玩家房间。连接失败显示实际IP／端口及底层原因，玩法不分连接类型。见[联网](docs/modules/NETWORK.md)。

自研存档属hosting／persistence，D2S v96不变，当前规则指纹为`d2x-character-admission-v19/native-wire113c/d2s96/common10`。整局角色锁、校验后原子替换／.bak、失败保留实例与租约；旧规则不静默迁移。F11保存、Ctrl+F11校验后原协议退局重入及`--load`／`--save`入口保留。技能选择／热键、扣费后的资源、弹药／书页沿原字段保存；派生总值、动作、弹体、召唤、门户及短时效果不写盘。限制见[存档](docs/modules/SAVES.md)。

私有一人实例支持ESC／失焦自动暂停，共享房间／原服继续推进。现有named pipe提供权威快照／有界历史、时钟覆盖、保存／重载、资源／经验／金币、MPQ物品／怪物准备等管理入口；正常玩法仍发送原online-*包。消息目录及stub不代表全部实现。接口见[调试管道](docs/development/DEBUG_PIPE.md)和[服务端协议](docs/modules/SERVER_PROTOCOL.md)。

## 当前运行包与有限冒烟

2026-10-09第一幕普通怪物继续收尾后，Windows Release完整构建及最终增量构建成功，更新`dist/current`。EXE SHA256：`7720E49EFD6905516928FA71BD7BE32369927C7D4B980632BE4D4A46DE1270F1`；网络DLL：`9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。本次最终构建无warning／error；此前诊断日志保留，不能把历史批次概括为无warning。新增证据在忽略目录`artifacts/act1-monsters-closeout-20261009`，首次批次在`artifacts/act1-monsters-20261008`；没有新增测试脚本、用例或程序。

普通难度临时92级女巫存档，原走跑完成营地→鲜血荒地，首次批次原UNIT_TILE进入洞穴，19类AI有代表动作／远程／复活／巢／蛛网／冷却与死亡证据。当前最终包再次完成57个普通身份准入，观察区域三组合及13类型上限后的原单体路径；实际盾牌骷髅BL回包动作18，人物原0D／19更新、整数生命100比例及原命中类型已收敛。保存后新EXE恢复92级与生命，短时状态为空。本机D2GS抽查沉沦魔A1／A2和硬毛老鼠A2原0x6C目标攻击，据PlrMsg／MonsterMsg修复自研误发人物0C及怪物生命旗标／比例；本次不改客户端。准确观察与未覆盖范围仅维护在[怪物模块](docs/modules/MONSTERS.md#有限运行证据)。

最终自研宿主failures=0、characterIssues为空；客户端ignoredPackets=0、unavailableUnits=0、mapErrors／effectLimitations为空，stderr为空。原服样本1级人物最终死亡，ignoredPackets=43（入局11），场景错误为空；不宣称完整原服无未知包或成功击杀。普通难度的这些原表行没有启用毒伤；噩梦／地狱毒伤、全部随机分支／取消／背压／多人、完整Vision／随机调度、完整原服回归和Linux未运行认证。上述是代表路径有限V2，不能把全部怪物或战斗整体标V3。

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

原地图、真实GUID和原图UI共用现有客户端。人物面板、技能提示／树、库存、NPC菜单、交易、HUD、聊天、祭坛／野外物件、声音和照明的规则／限制分别维护在[人物](docs/modules/CHARACTER.md)、[HUD](docs/gameplay/ui/CLASSIC_HUD.md)、[库存](docs/modules/INVENTORY.md)、[NPC／任务](docs/modules/NPC_QUEST.md)、[物件](docs/gameplay/world/OBJECTS.md)及[联网](docs/modules/NETWORK.md)。已有定义和协议请求不等于价格／赌博、佣兵、全部任务／物件或七职业视觉完成；晚入视野不重播历史施法／弹体，宠物头像HUD未接。

自研宿主手动丢弃物品／金币采用事务发布掉落事实，再编码原0x9C action=2，触发现有客户端的MPQ翻转动画及掉落音效；静态地面同步仍用action=0。该修正已随当前完整Release构建入包，未在怪物批次专项运行；怪物／箱桶新生掉落动画尚未接此事件。细节见[库存](docs/modules/INVENTORY.md)。

原MPQ位于`assets/mpq2`；先查本地reference和当前资源，来源／固定版本／许可见[资料来源](docs/resources/THIRD_PARTY.md)、[MPQ](docs/resources/MPQ.md)。保留原资源、reference、旧压缩包和mvp；资源、缓存、截图、存档、运行包及参考仓库不入源码提交。独立d2x_assets保留原资源／地图报告及save-info工具。

[构建运行](docs/development/BUILD_AND_RUN.md)维护启动与打包；[调试管道](docs/development/DEBUG_PIPE.md)维护既有诊断。默认只修改源码／文档，构建、打包、运行按有效用户授权；历史离线结果不认证当前联机产品。
