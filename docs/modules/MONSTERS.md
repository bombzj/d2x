# 怪物身份、服务端行为与原服表现

怪物由所连接的服务端分配；客户端不调用人口计划生成单位，不运行 AI、攻击／死亡奖励或复活结算器。第一幕接入范围见[怪物表现](../gameplay/world/MONSTERS.md)。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | type1 GUID／classId、位置／模式／动作／生命／状态及移除回包 |
| [monster_catalog.cpp](../../src/content/monsters/monster_catalog.cpp) | 真实 MonStats → MonStats2，身份、尺寸、组件、技能／原图定义 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 唯一敌我、状态与尸体目标资格；命中选择／锁定和请求发送共用 monsterTargetEligible |
| [remote_scene.cpp](../../src/presentation/remote/remote_scene.cpp) | 原动作／技能及状态转换成公共动画、显示路径、弹体与声音事件 |
| [monster_effects.cpp](../../src/presentation/remote/monster_effects.cpp) | 原电／冰强化回调、公开触发位及动画时钟；不执行伤害 |
| [actor_animation.cpp](../../src/presentation/actors/actor_animation.cpp) | 原 COF／DCC／AnimData／MonSeq、变换、动作时钟 |
| [sound_catalog.cpp](../../src/content/audio/sound_catalog.cpp)、[scene_audio.cpp](../../src/presentation/audio/scene_audio.cpp) | 唯一 MonSounds 定义解释与公共声音播放规则 |
| [population.cpp](../../src/world/population.cpp) | 资源工具报告及自研宿主人口准备共用；客户端不调用它出生怪物 |

真实身份和外观分别建模。未支持的敌对怪物允许用沉沦魔外观并保留真实身份；中立 NPC／友军／佣兵／宠物不能冒充敌人。是否敌对、可选尸体及技能目标资格由 RemoteCombat 核对 MPQ 和原服 alignment／状态，不靠外观推断；RemoteScene 不再另行解释敌我规则。onlineMonsterCorpse 统一原死亡模式和剥离电强化触发位后的生命刻度，供声音、弹体接触、光照和 NPC 交互读取。

<a id="自研服务端第一幕普通怪物"></a>

## 自研服务端第一幕怪物

当前MPQ第一幕Levels的mon／nmon／umon池共57个身份；连同固定预设／任务怪的andariel、bloodraven、griswold、smith、skmage_pois3及gargoyletrap，共63个可击杀敌对战斗类型。19类普通AI之外接Smith、Griswold、BloodRaven、Countess、Andariel、GargoyleTrap，共25类程序。普通、冠军、暗金、超级暗金、仆从和幕首领保留各自身份与规则。先迁master各家族AI／行动／词缀，再用当前MPQ及D2MOO核对。代表路径的有限V2见末节，其余分支仍为V1；源码接线不代表所有随机／难度组合已认证。

| AI家族 | 当前MPQ身份 | 权威程序 |
| --- | --- | --- |
| Skeleton | skeleton1–3、hellbovine | 接近、A1／A2、等待 |
| Zombie | zombie1–3 | 察觉／受击及埋骨之地追击、游荡、A1／A2 |
| Brute | brute1–3 | 生命比例速度、绕行、A1／A2 |
| Fallen | fallen1–4 | 领队号令、族群指令、恐惧逃跑、A1／A2、S2 |
| CorruptRogue | corruptrogue1–4 | 难度距离、走跑、近战 |
| Goatman | goatman1–3、goatman5 | 接近、近战与等待 |
| CorruptLancer | cr_lancer1–3 | 走跑／冲锋标记、近战 |
| Wraith | wraith1–2 | 原墙穿越掩码、接近、近战／元素及资源伤害 |
| BloodHawk | foulcrow1–2 | 飞行、冲锋、撤退与近战 |
| Fetish | fetish1 | 接战计数、按目标生命逃跑／再接战 |
| QuillRat | quillrat1–4 | A1近战、A2尖刺、SEIS额外针刺、撤退 |
| CorruptArcher | cr_archer1–4 | 箭矢、距离走跑、撤退失败继续原决策 |
| SkeletonBow | sk_archer1–3 | 箭矢、绕行、步数与目标距离 |
| SkeletonMage | skmage_fire1–2、skmage_ltng1–2 | 元素弹、撤退及失败射击、距离／绕行 |
| Bighead | bighead1–4 | 生命阈值、A1／A2、原火／毒等弹体 |
| FallenShaman | fallenshaman1–4 | 号令、本人族群尸体复活、类链火弹、近战 |
| FoulCrowNest | crownest1–2 | 静止Nest序列、出生次数／间隔、飞禽准入及耗尽死亡 |
| Arach | arach1 | 近战、生命阶段、SpiderLay移动轨迹／地面减速 |
| Vampire | vampire5 | 火球范围命中、FireHead命中恢复、生命阶段与撤退 |

vampire5三难度aip5仅开启位1；表中Firewall／Meteor槽的位2／4未开启，普通AI不执行。骷髅／法师的SkeletonRaise是复活动画，非普通AI主动技能；Countess相关槽不作为普通罗格程序调用。其他原行／精英须另准备条件，不能根据已有槽位宣称支持。

## MPQ准备契约

hosting准备不可变纯值，server不读表、资源或客户端副本。自然人口和monster-spawn使用同一准入。缺必需原动作／技能／碰撞数据明确暂缓，不把远程回落成近战。

| 数据 | 已准备／使用 |
| --- | --- |
| 人口 | Levels三难度池／区域等级；MonStats稀疏、概率、群组、族群与真实spawn key沿既有人口计划 |
| AI | AI、aidist／aidel及八项aip的三难度值；aidist=0按原35，玩家候选另受原55约束；原房间LOSDraw／NO_LOS_DRAW与初次察觉射线、察觉后追击标记；邻室激活及主／低Threat候选分开 |
| 数值 | Level／noRatio／MonLvl，生命／AC、独立A1／A2TH和伤害、三槽ElMode／Type／Pct／MinD／MaxD／Dur、六抗性、Crit、DamageRegen、ToBlock／NoShldBlock、经验及既有掉落 |
| 路径／动作 | Velocity／Run、家族百分比、MeleeRng、BaseId／flying／opendoors、SizeX／spawnCol、mGH／mBL／mKB／mWL、resurrectMode／corpseSel；AnimData释放、GH／BL／死亡／复活时长 |
| 技能／弹体 | MissA1／A2；Skill1–4／SkMode／SkLvl的实际执行槽、MonSeq事件、DifficultyLevels.MonsterSkillBonus／MonsterColdDiv；Missiles五段伤害／HitShift、SrcDamage／EType／长度、Range／LevRange、Vel／VelLev、Activate、CollideType／CollideKill／AlwaysExplode、ClientSend／CanSlow／ReturnFire／HitClass、原命中回调／子弹型号 |
| 组件／表现 | MonStats2十六组件选择数、TotalPieces及HitClass；区域选中类型先准备最多三个十六槽组合，新增类型在TotalPieces>2且区域类型数未满13时补入；无区域池使用原单体前十二槽路径。原AC位宽发送，实际SH盾牌影响格挡；近战用MonStats2.HitClass，弹体用Missiles.HitClass，元素／暴击标记在接触成功时计算。MonSounds／Light／调色／COF／DCC仍由原客户端解释 |

缺省35／15、SEIS种子、路径掩码、方向表及GH阈值是已核实原程序常量，参数不写成演示数值。ResurrectMode=xx不是动画，按MONSTERSPAWN_GetResurrectMode的越界模式规则使用NU，不因此拒绝骷髅／法师／小矮人。怪物弹体已接NextHit／NextDelay的玩家及伙伴接触窗口、NoMultiShot／NoUniqueMod和独立随机。区域组合以人口计划选型后的区域随机流准备；完整原版全区域初始化／房间随机调度尚未复刻，不能宣称同种子外观和出生位置逐单位一致。Vision缓存、特殊AI强制察觉旗标及动态拥挤仍待完整接入。

## 所有权与公共函数

gameplay/monsters/decision提供动作纯值，melee／skirmish／ranged／shaman／special按族求值；server/ai/family_actions持目标、等待、标记和决策随机，只提交窄请求。monsters拥有实体、路线、状态、复活和巢出生；skills拥有动作／释放队列；missiles拥有EnemyProjectile快照和接触计划；combat／effects执行六通道、冷毒／资源及地面状态；death／loot／quests独立结算。复活和巢生单位不再次给XP／TC，巢子保留真实身份。

两端共享方向／原偏移表、SEIS尖刺目标、固定点速度和原路径掩码。距离／绕行／家族决策、伤害／暴击／GH、组件组合及生命比例纯计算供不同来源复用；客户端没有权威输入就不调用。组合状态按实例／区域／原类型由monsters拥有，hosting只准备初始纯值，不用全局静态缓存。分层证据见[参考设计](../architecture/REFERENCE_DESIGN.md)。

客户端仅作有原版依据的修复与公共提取：Wraith路径0x0804／小型图案、Velocity固定点截断及下节原包／精英回调。hosting按MonsterMsg编码A1=10、A2=16、S1=13、S2=15、GH=6、BL=18；技能走原4C／4D，ClientSend控制73。人物命中／生命更新用PlrMsg的0D／19及0..100比例，怪物0C使用旗标19、原0..128比例减一和命中类型；两者均先截断整数生命。死亡仍走原死亡模式；0D／19不是GH动作，不宣称玩家恢复时钟已经完成。

GH、冻结、击退和死亡使未释放动作失效。可靠队列重试保留动作随机与首次释放位置／目标，接触计划只生成一次；已释放弹体可在来源死亡后继续，同区规则不跨区追踪。server-snapshot的rules列出准备参数、动作／技能，运行列另含盾牌、巢生计数、蛛网和中断。构建／运行事实见基线，逐字段接线不代表所有组合已认证。

## 精英、固定怪物与首领

SuperUniques读取十三条第一幕记录：Bishibosh、Bonebreak、Coldcrow、Rakanishu、Treehead WoodFist、Griswold、The Countess、Pitspawn Fouldog、Flamespike the Crawler、Boneash、The Smith、The Cow King、Corpsefire。固定词缀、额外词缀、组数、名称、变换和TC由当前表准备；Flamespike的表存在不等于当前地图存在刷新点，不自造位置。普通池之外的中立NPC、牲畜和不可击杀墙面发射器不进入敌对人口，后者属于物件机关范围。

hosting按每个出生身份滚独立词缀，仆从继承已准备领队数值；不把已滚暗金规则按类型缓存。MonUMod的权重、排斥、类型限制、难度常数与MonLvl、DifficultyLevels、Skills共同准备生命／等级／经验、物理增伤、命中、抗性、速度、元素、法力燃烧、冠军变体及光环／诅咒。runtime只接纯值，不读MPQ。实际经验／掉落携带真实rank及rewardModifiers；首领任务资格、首杀TC、组队／人数规则另属任务／奖励领域，尚未完整接入。

| 程序 | 权威行为及数据 |
| --- | --- |
| Andariel | 四项MPQ AI概率；普通近战、AndyPoisonBolt、AndrialSpray原MonSeq九次释放。公共andarielSprayRay只计算原整数方向／起点；客户端保留subtile中心，服务端执行命中和毒伤 |
| BloodRaven | 原home边界、走跑／撤退／绕行、普通弓箭、Quick Strike独立序列；召唤zombie2采用原位置随机、难度数量上限，巢子NOXP／NOTC。地狱MonProp的knock从表准备，chance空白／0按原规则无条件应用（非地狱运行认证） |
| Griswold／Smith | 原近战／等待、Smith随生命变化速度；The Smith另保留固定SuperUnique身份／词缀 |
| Countess | 独立Countess AI，不改所有CorruptRogue；使用真实DS1 path nodes，逐点FirewallMaker／地面火，原700帧周期；没有路径节点不捏造火墙点 |
| GargoyleTrap | 原距离、轴向窗口、概率、射后恢复与等待；静止可击杀单位，原seq_gargoyletrap的event4释放。gargoyleTrapRay显式保留原Clt／Srv轴向差异 |
| 精英运行效果 | Extra Strong／Fast、冠军数值、抗性／石肤、元素附伤、Mana Burn、Spectral Hit持久元素stat、Multishot；范围Amplify Damage、友方属性光环、Holy Fire／Freeze／Shock敌对脉冲；低生命Teleport、PreventHeal复验、首次察觉音效与等待；Lightning GH延时／冷却、Cold／Fire死亡事件 |

monsters持home、DS1技能位置、谱系、元素stat和死亡连锁；gameplay/monsters/boss_decision是服务端策略纯函数，不迁客户端；skills持有技能独立动作与多次释放；missiles执行传播、原NoMultiShot／NoUniqueMod、NextHit与范围接触；effects/monster_enchantments持各单位光环／诅咒／精英事件时钟。准备、输出和接触失败时保留计划，重试不重新扣费／掷伤；死亡触发与光环使用不同发生编号。

原AC的五位rank旗标、superunique hcIdx、词缀和nameSeed通过原SCmd布局投影。玩家MonProp击退用原0F/action20，PlrMsg确认为KB；客户端原生模式表一并按Player.h纠正，原服与自研服共用。Andariel／Gargoyle方向表及电强化八条充能路径共用纯函数；Clt参数／时序／轴向差异显式保留。

血鸟与安达利尔死亡连锁采用QuestsFX的范围、延时和undead筛选；这不是任务奖励或首杀完成。状态、动作、出生单位、词缀和短时效果不写人物D2S，字符存档语义／指纹不变。A1无MissA1的技能使用原event1（安达利尔毒弹／伯爵夫人火墙），远程A1使用event2；Nest／Gargoyle及对应序列沿原事件。管理怪物准入及新增身份快照见[调试管道](../development/DEBUG_PIPE.md)。

## 生命周期与限制

原移动、攻击、受伤、死亡、复活及单位移除驱动表现。0x0C剥离电强化触发位后解释生命刻度；原 hide／udead／corpseSel 分别控制尸体显示与选择。弹体出生与ClientSend按原程序决定，动作派生与原0x73同步去重，规则见攻击专题。换区／移除清理相应显示缓存。

怪物位置／死亡沿当前原包消费者投影；所有击杀、经验、掉落与任务结算由所连接的权威服务端决定，自研缺口见上述切片。旧家族AI／内部存档版本记录不代表当前实现。

原电／冰强化按1.13c静态依据接入同一客户端：GH动画帧2、死亡动画帧4及非GH生命更新，电强化须有公开emission标志。0C与69/action6生命高位不是阶级；69 GH的方向槽实际为生命字节。server/effects持MONUMOD延时／十帧冷却，RemoteMonsterEffects持显示时钟；可靠命中事实驱动受击，普通快照不重复启动GH／BL，新进入视野仍恢复当前模式，ClientSend为空时不强发73。当前194／195的速度、寿命没有等级增量，因此显示不推算隐藏等级；不同MPQ启用增量则明确不可用。

完整随机精英名称／染色、首领专用死亡演出、全部状态速度及随机出生／战斗分支未完整认证。任务首杀TC／奖励、原人数／组队及完整随机调度仍有独立缺口。人口准备与出生计划见[人口规则](../gameplay/world/POPULATION.md)，来源及许可见[资料来源](../resources/THIRD_PARTY.md)。

## 有限运行证据

最新运行包摘要和验证范围见[基线](../../BASELINE.md#当前运行包与有限冒烟)。只用现有EXE、原服与调试管道；资源、日志、截图、角色及参考代码保留在忽略目录。

| 历史证据 | 覆盖与边界 |
| --- | --- |
| `artifacts/act1-monsters-20261008` | 57普通身份准入、19类AI代表动作、萨满复活／再次死亡、巢生、蛛网、吸血鬼远程及保存重入；普通难度92级女巫，不等于逐分支验收 |
| `artifacts/act1-monsters-closeout-20261009` | 最终普通包57身份准入、区域三组合／13类型容量、盾牌骷髅BL、人物0D／19、第二鸟巢耗尽及蛛网来源死亡后到期；保存重入无短时状态 |
| 同目录 `native-melee-ranged.json` | D2GS 1.13c fallen1 A1／A2／S2及quillrat1 A2；1级人物死亡，ignoredPackets=43（入局11），不能称成功击杀或完整原服回归 |

本次最终包新增证据在`artifacts/act1-all-monsters-closeout-20261009`：

| 路径 | 实际观察／边界 |
| --- | --- |
| 入场／准入 | 最终EXE正常营地→鲜血荒地；`final-admission.json`及`final-spawn-snapshot.json`：六种额外战斗类型和全部十三固定超级暗金成功；只证明该区域普通难度准入，不证明自然刷新位置 |
| 首领代表动作 | `final-reaction-boss.json`：Andariel技能201喷毒／164毒弹、BloodRaven技能167、GargoyleTrap技能172；权威生命变化及客户端原动作。未认证全部随机分支、血鸟召唤／死亡连锁；调试Countess没有DS1路径节点，不以准入认证其火墙 |
| 电／冰强化 | `final-reaction-client.json`、`final-reaction-server.json`、`final-reaction-cold.json`：Rakanishu一次GH的69生命81及后续0C触发209，无快照重复GH；八条195。Coldcrow死亡第五步时三十二条194；`final-cold.png`可见电弧及冰环，客户端原死动画自行生成64方向。后续命中观察冷却清触发，不推造隐藏等级 |
| 正常玩家战斗 | `final-reaction-pve.json`：原3C确认右手44、原C2S施放；brute2生命24→20.859375及chilledUntil，伤害和状态由权威执行。没有只靠管理击杀认证普通攻击 |
| 保存／重入 | `final-before-save.json`／`final-reentry-saved.json`：人物92级、生命931／法力217保存恢复；原入局ProtocolReady，短时状态不恢复。最终诊断和stderr见基线 |

这些为有限V2；准入、空诊断和静态核对不能把所有怪物或P3标为V3。噩梦／地狱、全部状态组合／取消与背压、多人、Linux及长期运行仍未认证。
