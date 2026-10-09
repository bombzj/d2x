# 怪物公共规范与生命周期

更新：2026-10-09。本页维护怪物迁入规范、规则准备、运行态与共享计算边界。分类／按幕状态见[目录](README.md)，代码入口见[模块](../../modules/MONSTERS.md)，原函数结构证据见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)。

## 身份与完成口径

MonStats真实身份、MonStats2外观、AI程序、rank、SuperUnique记录及出生来源分别登记；同一家族支持不代表所有实际类型已准入，同一类型跨幕或跨难度也不自动完成。MonsterKind或技能槽存在只是映射／数据，不等于当前server已有对应运行程序。非敌对单位不得混入普通敌对人口；阵营与目标规则见[FACTIONS](../combat/FACTIONS.md)。

当前允许的替身仅为沉沦魔，真实monster／superUnique／spawnKey保留，用于缺失敌对类型；不允许中立NPC、友军、佣兵或宠物使用敌对替身。按[combat_content.cpp](../../../src/hosting/combat_content.cpp)当前条件，邪恶洞穴及第二至第五幕可准备这种替身；第一幕其他未支持类型保留暂缓。替身不代表真实外观、数值、AI或首领技能已实现。缺规则或无法放置的单位保留populationMissing／诊断，清场任务不能默认为已杀完。

源码接入、构建、代表路径运行与完整原版验收分别记录。已有有限V2只覆盖相应历史包和列出路径；管理准入不认证自然刷新点，管理击杀不认证正常战斗。任务链与首领AI分开计量，任务证据见各幕任务页。

## MPQ准备契约

hosting准备不可变纯值，server不读表、资源或客户端副本。自然人口和monster-spawn使用同一准入。下表描述已准入类型与实现族的准备字段，不代表其他幕全部类型已支持。缺必需原动作／技能／碰撞数据明确暂缓，不把远程回落成近战。

| 数据 | 已准备／使用 |
| --- | --- |
| 人口 | Levels三难度池／区域等级；MonStats稀疏、概率、群组、族群与真实spawn key沿既有人口计划 |
| AI | AI、aidist／aidel及八项aip的三难度值；aidist=0按原35，玩家候选另受原55约束；原房间LOSDraw／NO_LOS_DRAW与初次察觉射线、察觉后追击标记；邻室激活及主／低Threat候选分开 |
| 数值 | Level／noRatio／MonLvl，生命／AC、独立A1／A2TH和伤害、三槽ElMode／Type／Pct／MinD／MaxD／Dur、六抗性、Crit、DamageRegen、ToBlock／NoShldBlock、经验及既有掉落 |
| 路径／动作 | Velocity／Run、家族百分比、MeleeRng、BaseId／flying／opendoors、SizeX／spawnCol、mGH／mBL／mKB／mWL、resurrectMode／corpseSel；AnimData释放、GH／BL／死亡／复活时长 |
| 技能／弹体 | MissA1／A2；Skill1–4／SkMode／SkLvl的实际执行槽、MonSeq事件、DifficultyLevels.MonsterSkillBonus／MonsterColdDiv；Missiles五段伤害／HitShift、SrcDamage／EType／长度、Range／LevRange、Vel／VelLev、Activate、CollideType／CollideKill／AlwaysExplode、ClientSend／CanSlow／ReturnFire／HitClass、原命中回调／子弹型号 |
| 组件／表现 | MonStats2十六组件选择数、TotalPieces及HitClass；区域选中类型先准备最多三个十六槽组合，新增类型在TotalPieces>2且区域类型数未满13时补入；无区域池使用原单体前十二槽路径。原AC位宽发送，实际SH盾牌影响格挡；近战用MonStats2.HitClass，弹体用Missiles.HitClass，元素／暴击标记在接触成功时计算。MonSounds／Light／调色／COF／DCC仍由原客户端解释 |

缺省35／15、SEIS种子、路径掩码、方向表及GH阈值是已核实原程序常量，参数不写成演示数值。ResurrectMode=xx不是动画，按MONSTERSPAWN_GetResurrectMode的越界模式规则使用NU，不因此拒绝骷髅／法师／小矮人。怪物弹体已接NextHit／NextDelay的玩家及伙伴接触窗口、NoMultiShot／NoUniqueMod和独立随机。区域组合以人口计划选型后的区域随机流准备；完整原版全区域初始化／房间随机调度尚未复刻，不能宣称同种子外观和出生位置逐单位一致。Vision缓存、特殊AI强制察觉旗标及动态拥挤仍待完整接入。

## 状态所有权与公共计算

gameplay/monsters/decision提供动作纯值，melee／skirmish／ranged／shaman／special按族求值；server/ai/family_actions持目标、等待、标记和决策随机，只提交窄请求。monsters拥有实体、路线、状态、复活和巢出生；skills拥有动作／释放队列；missiles拥有EnemyProjectile快照和接触计划；combat／effects执行六通道、冷毒／资源及地面状态；death／loot／quests独立结算。复活和巢生单位不再次给XP／TC，巢子保留真实身份。

两端共享方向／原偏移表、SEIS尖刺目标、固定点速度和原路径掩码。距离／绕行／家族决策、伤害／暴击／GH、组件组合及生命比例纯计算供不同来源复用；客户端没有权威输入就不调用。组合状态按实例／区域／原类型由monsters拥有，hosting只准备初始纯值，不用全局静态缓存。分层证据见[参考设计](../../architecture/REFERENCE_DESIGN.md)。

## 动作、死亡与奖励

GH、冻结、击退和死亡使未释放动作失效。可靠队列重试保留动作随机与首次释放位置／目标，接触计划只生成一次；已释放弹体可在来源死亡后继续，同区规则不跨区追踪。server-snapshot的rules列出准备参数、动作／技能，运行列另含盾牌、巢生计数、蛛网和中断。构建／运行事实见基线，逐字段接线不代表所有组合已认证。

combat保存待命中计划，effects投影动态防御／抗性与状态；人物伤害通过transactions提交。词缀和状态增伤相加后应用，暴击在元素附伤后执行；队列与随机取值归权威实例，不恢复客户端战斗执行器。光环脉冲、死亡爆炸和每个弹体使用独立发生编号，重试不得重复结算。

death首次捕获活击杀者身份及经验，loot携带真实rank／固定TC／rewardModifiers；成功提交或经验封顶后才完成奖励。巢生与血乌召唤NOXP／NOTC，死亡连锁不凭空获得玩家归属。自然身份死亡由quests计算个人资格；任务清场继续消费实际人口死亡／复活，完整组队经验、NoDrop和首杀／任务掉落另有缺口，不能由AI代办。

运行态身份、动作、位置、时钟、词缀和短时效果不写人物D2S；伙伴持久身份的例外见[佣兵](../characters/HIRELINGS.md)与[存档](../../modules/SAVES.md)。视觉尸体和原模式解释归[PRESENTATION](PRESENTATION.md)，玩家尸体事务归[人物死亡](../characters/PLAYER_DEATH.md)。

## 迁入流程与台账

1. 先读master的对应家族AI、动作、词缀、召唤／复活及当前纯函数，只迁可用规则，不能直接恢复耦合旧GameSession的执行器。
2. 按当前MPQ登记MonStats／MonStats2、AI及三难度参数、A1／A2／技能槽、MonSeq事件、Missiles、精英／固定身份与人口条件；再查本地reference补职责和缺漏，参考参数不覆盖MPQ。
3. 共享确定性方向、速度、距离、时序和纯数值；原Clt／Srv参数、坐标中心、事件帧、环射步长与随机顺序有差异时保留。家族决策可在服务端复用，客户端无权威输入就不调用。
4. 分开内容准备、出生、AI决策、动作／释放、命中／状态、死亡／奖励与原包投影；保存区域／实体代次、中断标记、首次随机和事务结果，异步迟到及背压不重复执行。
5. 客户端只做有原版依据的修复／公共提取，与原D2GS共用同一消费者；禁止为自研宿主添加私有消息、敌对推断或伤害权限。
6. 更新所属幕的实际类型台账、精英／首领专用差异与证据。默认只修改，不构建／运行／打包，不编写测试脚本、用例或专用程序；需要保存语义变更时同步规则指纹与存档文档。

逐项条目至少包含真实ID／原名、所在区域／难度、rank／固定身份、AI／攻击／技能、原表与原函数来源、公共函数、客户端支持、自研执行、替身／暂缓及运行证据。全集以当前MPQ为准，未知行归[SPECIAL](SPECIAL.md)；分类入口齐备不代表全MonStats逐行审计已经完成。
