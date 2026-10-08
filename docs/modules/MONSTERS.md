# 怪物身份、服务端行为与原服表现

怪物由所连接的服务端分配；客户端不调用人口计划生成单位，不运行 AI、攻击／死亡奖励或复活结算器。第一幕接入范围见[怪物表现](../gameplay/world/MONSTERS.md)。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | type1 GUID／classId、位置／模式／动作／生命／状态及移除回包 |
| [monster_catalog.cpp](../../src/content/monsters/monster_catalog.cpp) | 真实 MonStats → MonStats2，身份、尺寸、组件、技能／原图定义 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 唯一敌我、状态与尸体目标资格；命中选择／锁定和请求发送共用 monsterTargetEligible |
| [remote_scene.cpp](../../src/presentation/remote/remote_scene.cpp) | 原动作／技能及状态转换成公共动画、显示路径、弹体与声音事件 |
| [actor_animation.cpp](../../src/presentation/actors/actor_animation.cpp) | 原 COF／DCC／AnimData／MonSeq、变换、动作时钟 |
| [sound_catalog.cpp](../../src/content/audio/sound_catalog.cpp)、[scene_audio.cpp](../../src/presentation/audio/scene_audio.cpp) | 唯一 MonSounds 定义解释与公共声音播放规则 |
| [population.cpp](../../src/world/population.cpp) | 资源工具报告及自研宿主人口准备共用；客户端不调用它出生怪物 |

真实身份和外观分别建模。未支持的敌对怪物允许用沉沦魔外观并保留真实身份；中立 NPC／友军／佣兵／宠物不能冒充敌人。是否敌对、可选尸体及技能目标资格由 RemoteCombat 核对 MPQ 和原服 alignment／状态，不靠外观推断；RemoteScene 不再另行解释敌我规则。onlineMonsterCorpse 统一原死亡模式和剥离 rank 标志后的生命刻度，供声音、弹体接触、光照和 NPC 交互读取。

## 自研服务端第一幕普通怪物

当前MPQ第一幕Levels的mon／nmon／umon池共57个身份，按19类AI接入普通阶级。首领、冠军、暗金、超级暗金及其仆从不套普通规则。先迁master各家族AI／行动，再用D2MOO核对参数、原模式、公共计算和缺口。

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

缺省35／15、SEIS种子、路径掩码、方向表及GH阈值是已核实原程序常量，参数不写成演示数值。ResurrectMode=xx不是动画，按MONSTERSPAWN_GetResurrectMode的越界模式规则使用NU，不因此拒绝骷髅／法师／小矮人。当前普通弹体没有启用NextHit；非零NextDelay的怪物玩家目标窗口尚未接。区域组合以人口计划选型后的区域随机流准备；完整原版全区域初始化／房间随机调度尚未复刻，不能宣称同种子外观和出生位置逐单位一致。Vision缓存、特殊AI强制察觉旗标及动态拥挤仍待完整接入。

## 所有权与公共函数

gameplay/monsters/decision提供动作纯值，melee／skirmish／ranged／shaman／special按族求值；server/ai/family_actions持目标、等待、标记和决策随机，只提交窄请求。monsters拥有实体、路线、状态、复活和巢出生；skills拥有动作／释放队列；missiles拥有EnemyProjectile快照和接触计划；combat／effects执行六通道、冷毒／资源及地面状态；death／loot／quests独立结算。复活和巢生单位不再次给XP／TC，巢子保留真实身份。

两端共享方向／原偏移表、SEIS尖刺目标、固定点速度和原路径掩码。距离／绕行／家族决策、伤害／暴击／GH、组件组合及生命比例纯计算供不同来源复用；客户端没有权威输入就不调用。组合状态按实例／区域／原类型由monsters拥有，hosting只准备初始纯值，不用全局静态缓存。分层证据见[参考设计](../architecture/REFERENCE_DESIGN.md)。

客户端改动仅为此前纯函数提取及原版修正：Wraith路径0x0804／小型图案、Velocity先固定点截断；本次收尾没有改客户端。hosting按MonsterMsg编码A1=10、A2=16、S1=13、S2=15、GH=6、BL=18；技能走原4C／4D，ClientSend控制73。人物命中／生命更新用PlrMsg的0D／19及0..100比例，怪物0C使用旗标19、原0..128比例减一和命中类型；两者均先截断整数生命。死亡仍走原死亡模式；0D／19不是GH动作，不宣称玩家恢复时钟已经完成。

GH、冻结、击退和死亡使未释放动作失效。可靠队列重试保留动作随机与首次释放位置／目标，接触计划只生成一次；已释放弹体可在来源死亡后继续，同区规则不跨区追踪。server-snapshot的rules列出准备参数、动作／技能，运行列另含盾牌、巢生计数、蛛网和中断。构建／运行事实见基线，逐字段接线不代表所有组合已认证。

## 生命周期与限制

原移动、攻击、受伤、死亡、复活及单位移除驱动表现。0x0C剥离暗金标志后解释生命刻度；原 hide／udead／corpseSel 分别控制尸体显示与选择。弹体出生与ClientSend按原程序决定，动作派生与原0x73同步去重，规则见攻击专题。换区／移除清理相应显示缓存。

怪物位置／死亡沿当前原包消费者投影；所有击杀、经验、掉落与任务结算由所连接的权威服务端决定，自研缺口见上述切片。旧家族AI／内部存档版本记录不代表当前实现。

完整随机精英名称／染色、死亡专用演出、客户端程序、状态速度和全部随机出生／战斗分支未认证。人口规则作为工具数据说明见[人口报告](../gameplay/world/POPULATION.md)，来源及许可见[资料来源](../resources/THIRD_PARTY.md)。

## 有限运行证据

2026-10-09首次普通怪物批次（c6db530），普通难度临时Hero女巫92级（基础生命931），使用现有调试管道准备单位／观察权威，正常移动、霜之新星、传送与UNIT_TILE仍发送原C2S。日志、JSON、截图和存档只留忽略目录`artifacts/act1-monsters-20261008`。

- `admission-final.json`：57个身份全部成功。首轮9个骷髅／法师／小矮人因xx复活动作被错误拒绝，按原MonsterSpawn规则修复后重新构建／打包，再运行本轮。
- `all-admitted.json`、`families-250.json`、`special-*`及事件分页：19类AI均有动作事实；初轮250帧生命927.08→739.29。两种鸟巢出生计数达到表中6／8，第一种达到上限后死亡；自然生成与巢子保留真实类型。未单独覆盖第二种耗尽死亡的最后动作。
- `frost-hit.json`：霜之新星原技能44击中普通怪物，观察到chilledUntil、GH中断与飞禽死亡／rewardComplete；这不认证每类格挡和全部命中随机分支。
- `den-entry-final.json`、`resurrect-10.json`、`second-death.json`：自然萨满1009复活本人族群沉沦魔1010，生命0→4，rewardComplete仍为true并恢复行动；洞穴剩余数62→63，再次死亡后按实际存活数下降。92级对低等级怪物的经验已受等级折减，不能用本例数值认证XP重复奖励的全部边界。
- `web-3.json`／`web-4.json`：SpiderLay143移动生成SpiderGoo146，地面接触使玩家获得原slowed状态24；蜘蛛state22进入并到期。玩家站在尚存地面效果中会持续刷新24，本例未单独观察离开地面后的到期窗口。
- `vampire-events-*`、`vampire-final.json`：吸血鬼实际发送169火球与333 FireHead并扣血；先前受伤后的生命恢复同时包含DamageRegen，未把它当作独立FireHead吸血数值认证。
- `reload-final.json`：保存后新进程恢复92级、四项已学技能与生命910.21，短时状态为空。最终宿主failures=0／characterIssues空；客户端ignoredPackets=0／unavailableUnits=0／mapErrors及effectLimitations空，stderr空。

后续收尾证据在`artifacts/act1-monsters-closeout-20261009`，继续使用现有管道，无新增测试脚本或程序：

- `palette-field-final.json`：营地原走跑到鲜血荒地；同区48沉沦魔使用三个组合、32硬毛老鼠一个、28僵尸三个。`shield-admission.json`中的骷髅1／3有真实盾牌，骷髅2本次选中无盾；新增类型各有三个区域组合。
- `shield-final-online.json`及权威快照：普通原Attack实际触发骷髅3编号517的BL，收到0x69动作18；盾牌为true，生命21、interruption=1。近战人物命中类型3已观测；本例先于最终人物包修正，不能将旧0C人物Hit记录称作最终包的线格式。
- `release-admission-all.json`／`release-all-authority.json`／`release-all-online.json`：最终包再次准入57个身份，同区13个类型有区域池（十二类三组合、一类单组合），另外44类走原前十二槽单体路径，实际覆盖13类型容量后的路径。50帧后人物生命600.91796875，原人物百分比64，客户端ignoredPackets=0／unavailableUnits=0／场景错误为空；该批调试单位共用可行走位置，不认证自然人口动态占位。
- `release-online.json`／`release-authority.json`：新EXE从存档恢复92级／生命812.52734375，短时状态为空；原走跑后生命802.67578125、客户端百分比86，观察三次本人0D／19更新。最终怪物0C旗标19及死亡生命0另留`final-observation.json`，人物命中不再发0C。
- `nest-web-active.json`：第二种鸟巢crownest2出生计数8后死亡，补齐前批未覆盖的最后动作。`web-active-final.json`：arach1原低生命阶段触发蛛网，Player状态24、地面146寿命200帧；击杀来源再推进250帧，`web-expired-final.json`仍在区域2、地面146数量0且Player状态为空。最后保存成功，`closeout-reload-final.json`的新进程恢复92级／生命837.4453125、短时状态为空；宿主failures=0／characterIssues空，stderr为空。
- `native-melee-ranged.json`：本机D2GS 1.13c普通新女巫MonNativeOct，在原鲜血荒地遭遇fallen1（hcIdx19）与quillrat1（63）。完整事件含沉沦魔A1九次／A2三十三次／S2一次、硬毛老鼠A2三次，均为0x6C目标动作。原表ClientSend决定是否另发73，不能把未看到73当成远程未执行。本例1级人物最终死亡，不能当作成功击杀或完整战斗验收；不比较两服随机分支次数。
- 原服样本的本人生命更新为0D／19；据PlrMsg和MonsterMsg修复自研人物误发0C、怪物0C旗标、两种生命比例及整数截断。样本末尾原服ignoredPackets=43（入局11），含既有0x47／48／53等未消费包；unavailableUnits=0、mapErrors／effectLimitations为空。不能将本次原服样本宣称为无未知包，也未因未消费包增加自研私有兼容。

普通难度原表未开启毒伤，不制造毒参数；噩梦／地狱六种毒行、完整Vision／随机流调度、全部分支／状态组合／取消与背压／多人和Linux尚未运行认证。当前代表路径及两个原服普通样本为有限V2，逐身份准入不等于全部规则达到V3。
