# 怪物公共规范与生命周期

更新：2026-10-18。本页维护怪物迁入规范、规则准备、运行态与共享计算边界。分类／按幕状态见[目录](README.md)，代码入口见[模块](../../modules/MONSTERS.md)，原函数结构证据见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)。

## 身份与完成口径

MonStats真实身份、MonStats2外观、AI程序、rank、SuperUnique记录及出生来源分别登记；同一家族支持不代表所有实际类型已准入，同一类型跨幕或跨难度也不自动完成。MonsterKind或技能槽存在只是映射／数据，不等于当前server已有对应运行程序。非敌对单位不得混入普通敌对人口；阵营与目标规则见[FACTIONS](../combat/FACTIONS.md)。

当前允许的敌对怪物替身仅为沉沦魔，真实monster／superUnique／spawnKey保留；不允许中立NPC、友军、佣兵或宠物使用敌对替身。hosting为缺失的敌对人口原型准备替身，并回填真实类型的出生碰撞、附加占位、MonProp、Restore和不可计数标志。替身不代表真实外观、战斗数值、AI或首领技能已实现。清场读取实时人口coverage及活动／休眠单位，未生成房间或暂缓来源不能默认为已清空。

分别维护当前实现、原版依据、已知差异和待核实项，统一口径见[总计划](../../architecture/MULTIPLAYER.md#21-统一状态口径)。构建与自研运行只在当次交付中说明，不设验证等级；管理准入不证明自然刷新点，管理击杀不证明正常战斗。任务链与首领AI分开登记，任务规则及原版依据见各幕任务页。

## MPQ准备契约

hosting准备不可变纯值，server不读表、资源或客户端副本。自然人口、NPC与恢复共用prepareAdmissionActor及ActorPublication；管理出生仍有显式参数入口，不能作为自然分布证据。下表描述已准入类型与实现族的准备字段，不代表其他幕全部类型已支持。缺必需原动作／技能／碰撞数据明确暂缓，不把远程回落成近战。

| 数据 | 已准备／使用 |
| --- | --- |
| 人口 | Levels三难度池／区域等级；MonStats稀疏、概率、群组、族群与真实spawn key由server/population实时执行 |
| AI | AI、aidist／aidel及八项aip的三难度值；aidist=0按原35，玩家候选另受原55约束；原房间LOSDraw／NO_LOS_DRAW与初次察觉射线、察觉后追击标记；邻室激活及主／低Threat候选分开 |
| 数值 | Level／noRatio／MonLvl，生命／AC、独立A1／A2TH和伤害、三槽ElMode／Type／Pct／MinD／MaxD／Dur、六抗性、Crit、DamageRegen、ToBlock／NoShldBlock、经验及既有掉落 |
| 路径／动作 | Velocity／Run、家族百分比、MeleeRng、BaseId／flying／opendoors、SizeX／spawnCol、mGH／mBL／mKB／mWL、resurrectMode／corpseSel；AnimData释放、GH／BL／死亡／复活时长 |
| 技能／弹体 | MissA1／A2；Skill1–4／SkMode／SkLvl的实际执行槽、MonSeq事件、DifficultyLevels.MonsterSkillBonus／MonsterColdDiv；Missiles五段伤害／HitShift、SrcDamage／EType／长度、Range／LevRange、Vel／VelLev、Activate、CollideType／CollideKill／AlwaysExplode、ClientSend／CanSlow／ReturnFire／HitClass、原命中回调／子弹型号 |
| 组件／表现 | MonStats2十六组件选择数、TotalPieces及HitClass；区域选中类型先准备最多三个十六槽组合，新增类型在TotalPieces>2且区域类型数未满13时补入；无区域池使用原单体前十二槽路径。原AC位宽发送，实际SH盾牌影响格挡；近战用MonStats2.HitClass，弹体用Missiles.HitClass，元素／暴击标记在接触成功时计算。MonSounds／Light／调色／COF／DCC仍由原客户端解释 |

缺省35／15、SEIS种子、路径掩码、方向表及GH阈值是已核实原程序常量，参数不写成演示数值。ResurrectMode=xx不是动画，按MONSTERSPAWN_GetResurrectMode的越界模式规则使用NU，不因此拒绝骷髅／法师／小矮人。怪物弹体已接NextHit／NextDelay的玩家及伙伴接触窗口、NoMultiShot／NoUniqueMod和独立随机。区域组合以整局区域选型后的区域子流准备；完整原版全区域初始化／房间随机调度尚未复刻，不能宣称同种子外观和出生位置逐单位一致。Vision缓存、特殊AI强制察觉旗标及动态拥挤仍待完整接入。

生命的两个原表界限分别保存，不交换或强制令maxHP≥minHP；当前MPQ的MinionSpawner1普通难度即为反序。公共`rollMonsterLife`沿原`min + limited(max-min+1)`，有符号界限≤0时返回min且不抽随机；内容与准入不再先行拒绝该行。经验准备不将玩家99级上限用于怪物，按实际MonLvl行处理110级UberIzual等身份，人物等级与经验比例仍分别校验。SuicideMinion使用真实AI／死亡定时器，不为通过准入伪造攻击槽。

## 移动与目标依据

PATH_SetStepNum保存steps−1，PathMisc.sub_6FD5DB70以D2Common_10399单位距离判停止，不是路线点数量。本地原版1.13c D2Common.dll VA6FD5DBC7–6FD5DBD9调用6FDCFCD0后读取动态路径0x93字段比较，已只读核对。DLL SHA256为`59FA5928522F566F2BF99675571206AD70DF889C89D3D07FA87EDF5083E06E10`；源码不加载原DLL执行。

两幕绕行沿D2MOO AiTactics.sub_6FCD0E80及PathMisc.sub_6FDAA720：一次随机选type5／6，方向优先级转±2，逐格查原碰撞／大小，碰壁或反转停止，参数为路程上限；0不造一格绕行。RoomLayout.nearby与nearRooms共用DRLG同区域各轴间隙＜6 tile条件；跨区域链接及完整活动调度仍待接。

普通怪物Toward／Straight／WallFollow的算法统一放在d2x_navigation，由权威requestMove与客户端原67／68表现共同调用；失败路径选择属于服务端策略，客户端只执行实际收到的类型。参数、参考函数及仍使用通用AStar的入口见[共用寻路](../../modules/MAP.md#共用寻路)，完整路线及执行时序仍需分别核对。

普通家族已接受的WL／RN在`server/ai/system.cpp`统一沿路线继续，结束前不重选主目标、不执行暗金修饰器或重新抽取家族动作。该入口覆盖第一、二幕公共家族，以及鬼火、青蛙恶魔、萨卡兰姆、议会祭司、邪恶之母／幼体、手指法师、尸体吞食者、骑士、巨型恶魔、复活尸、冰冻恐惧、寄生虫、攻城兽、小恶魔、仆从／自爆仆从、督军、魅魔、衣卒尔、墨菲斯托、迪亚波罗、尼拉塞克及普通NPC移动；FetishShaman、NpcBarb不另维护一份路线继续判断。追踪目标位置未变时不重复重置定点路径；位置改变后的既有18帧重规划保留路径类型、搜索距离、速度与到达参数，WallFollow单位目标仍使用原40下界。失效目标、受击、冻结、碰撞、活动房间退出和显式AI重排继续走各自公共入口；诅咒替代AI保留独立路径。

移动中显式调用`AITACTICS_Idle`的家族保留原定时再决策：秃鹫Fn023为12帧／自身停顿，Baal／clone沿AiBaal为25帧，BaalMinion Fn141读取aip4；Talic／Korlic接近为10帧，Madawc逃离为25帧，其普通单位目标接近仍等待路线结束。Baboon、Regurgitator和Nihlathak在同次回调中移动后立即进入NU的分支仍保留原中止。依据为本地D2MOO AiThink对应函数、AiBaal、AiTactics及MonsterMode的WL／RN结束回调；本节为源码依据，完整动态目标／跨房间／多人及逐随机轨迹仍待核对。

server/ai.shootableTarget独立提供AiUtil回调6目标／距离：近房间、FullUnitSize距离≤48、可伤害资格和原missile barrier视线，Threat≤1另选。各家族只在原射击分支使用，不能覆盖主目标、接战判断、元素偏好及坐标技能落点；开始／释放仍复验来源、目标、区域、技能资格。完整强制目标／Vision缓存尚未实现。

## 状态所有权与公共计算

gameplay/monsters/decision提供动作纯值，melee／skirmish／ranged／shaman／special按族求值；server/ai/family_actions持目标、等待、标记和决策随机，只提交窄请求。monsters拥有实体、路线、状态、复活和巢出生；skills拥有动作／释放队列；missiles拥有EnemyProjectile快照和接触计划；combat／effects执行六通道、冷毒／资源及地面状态；death／loot／quests独立结算。复活和巢生单位不再次给XP／TC，巢子保留真实身份。

两端共享方向／原偏移表、SEIS尖刺目标、固定点速度和原路径掩码。距离／绕行／家族决策、伤害／暴击／GH、组件组合及生命比例纯计算供不同来源复用；客户端没有权威输入就不调用。组合状态按实例／区域／原类型由monsters拥有，hosting只准备初始纯值，不用全局静态缓存。分层证据见[参考设计](../../architecture/REFERENCE_DESIGN.md)。

## 出生与休眠的公共生命周期

hosting准备纯值，server/population持有唯一自然生成游标，monsters持有唯一Actor及休眠节点。自然出生、NPC和恢复复用prepareAdmissionActor与ActorPublication；既有NPC身份和休眠身份分别复验区域定义与源节点，不由调用方自报已分配。任务清场统一查询population::System::coverage，不维护另一份预规划出生账。

压缩／恢复入口在[monsters/inactive.cpp](../../../src/server/systems/monsters/inactive.cpp)，特殊恢复使用显式继续点，完成前不对客户端可见或受击。保存字段、落点／修饰器阶段、保留条件及特殊AI／MapAI等未闭合边界统一见[人口公共生命周期](POPULATION.md#公共出生压缩与特殊恢复)，此处不复制恢复台账。

## 动作、死亡与奖励

怪物动作结束与下一次AI决策使用两个时钟：`busyUntil`只表示动作锁，`neutralAt`记录待处理的ENDANIM。普通攻击、技能、GH／BL及复活结束进入NU时，`monsters::resumeAi`按当前难度的MonStats `aidel／aidel(N)／aidel(H)`安排思考；非正值沿原规则取15。等待从动作结束算起，不能把AI分支的默认1帧重试当成攻击间隔，也不能把站立等待拼进动画长度。原先已安排且晚于结束时点的AI事件保留，不额外叠加等待；移动结束仍沿即时决策路径。佣兵的轮询间隔与NU等待分别保存，避免把固定5帧轮询错当其原表1帧aidel。召唤物也接同一结束处理，重生怪物只由原家族AI消费该时钟。

依据为本地D2MOO `MonsterMode::sub_6FC63680`（通用结束）、`sub_6FC64310`（NU和未来AI事件保留）及`SUnit::sub_6FCBCE70`（事件与ENDANIM排程）。`SplEndGeneric`先按BaseId查原表，再为秃鹰S1、蝙蝠S3／S4等原有例外保留直接思考；Hydra／环境陷阱的专用调度不套普通NU等待。移动技能延长动作时同步延后结束时点，GH／BL替换被打断动作的结束时点，换区传送／重生清旧时钟；这些值不写D2S。

怪物ENDANIM使用公共`monsterAnimationTicks`：`max(2,ceil((frames-startFrame)×256/speed))`，不借用玩家普通动作的减一帧规则。服务端A1／A2、技能和恢复与自有客户端怪物动画共用此结束计算，包含使用玩家图形的女武神；命中仍在原事件帧，只由权威结算。当前MPQ既有导出中，`ZMA1HTH／ZMA2HTH`均16帧、速度256、事件帧9，zombie1三难度aidel为15／14／13；无速度加成、中断或额外AI等待时，源码计算的连续起手间隔为31／30／29个25Hz帧。它是静态推导，不是本次运行测量。

完整中途变速重排、击退结束转GH的特殊事件链及全怪物／全部词缀组合仍待核对，不能把通用时钟修正称为全MonStats逐帧复刻。

GH、冻结、击退和死亡使未释放动作失效。可靠队列重试保留动作随机与首次释放位置／目标，接触计划只生成一次；已释放弹体可在来源死亡后继续，同区规则不跨区追踪。server-snapshot的rules列出准备参数、动作／技能，运行列另含盾牌、巢生计数、蛛网和中断。具体实现与组合缺口归对应幕怪物专题，逐字段接线不代表所有组合已对齐原版。

combat保存待命中计划，effects投影动态防御／抗性与状态；人物伤害通过transactions提交。词缀和状态增伤相加后应用，暴击在元素附伤后执行；队列与随机取值归权威实例，不恢复客户端战斗执行器。光环脉冲、死亡爆炸和每个弹体使用独立发生编号，重试不得重复结算。

death首次捕获活击杀者身份及经验，loot携带真实rank／固定TC／rewardModifiers；成功提交或经验封顶后才完成奖励。巢生与血乌召唤NOXP／NOTC，死亡连锁不凭空获得玩家归属。自然身份死亡由quests计算个人资格；任务清场继续消费实际人口死亡／复活，完整组队经验、NoDrop和首杀／任务掉落另有缺口，不能由AI代办。

运行态身份、动作、位置、时钟、词缀和短时效果不写人物D2S；伙伴持久身份的例外见[佣兵](../characters/HIRELINGS.md)与[存档](../../modules/SAVES.md)。视觉尸体和原模式解释归[PRESENTATION](PRESENTATION.md)，玩家尸体事务归[人物死亡](../characters/PLAYER_DEATH.md)。

怪物停止路径保留原DynamicPath目标，开始单位动作把真实目标写入同一个路径目标，不增设第二份攻击目标。D2MOO Step::PATH_ResetMovement只清路径点／速度；MonsterMode.cpp的模式切换按请求设置PATH_SetTargetUnit。死亡、卸载和显式点目标继续按各自生命周期清目标。

BL按本机1.13c action18的单位分支编码为6A/type/GUID，事件与后续快照共用monsterBlock。不可见或不存在的目标使用合法查询类型及原无目标GUID(-1)，本机D2Common ordinal11129／PATH_SetTargetUnit接受查询返回的null；不把坐标装进身份字段。特殊攻击动作的快照也按MonsterMsg／SCmd选择完整6B／6C结构，保留目标及当前位置。

原版1.13c Join连接D2X宿主时于2026-10-13 07:40:04在D2Client RVA A4E27崩溃。日志117.894664秒的`69 86000000 12 C511 CF11 0000`为fallen1 GUID134／class19格挡；该错误消息来自D2X宿主。原客户端action18跳4E253，再由A604F按单位类型扩展单位表；异常ecx=4559，edx=6FDF3008，减查询表基址6FBBA608并除512恰为4549，即错误包的Y／X。坐标被当作身份导致越界查表，已直接对应异常寄存器，不能把本次异常归为已证实的掉落问题。该原版消费者反馈与静态控制链支持上节6A单位目标编码；完整原版Join战斗仍待核对。

## 迁入流程与台账

1. 先读master的对应家族AI、动作、词缀、召唤／复活及当前纯函数，只迁可用规则，不能直接恢复耦合旧GameSession的执行器。
2. 按当前MPQ登记MonStats／MonStats2、AI及三难度参数、A1／A2／技能槽、MonSeq事件、Missiles、精英／固定身份与人口条件；再查本地reference补职责和缺漏，参考参数不覆盖MPQ。
3. 共享确定性方向、速度、距离、时序和纯数值；原Clt／Srv参数、坐标中心、事件帧、环射步长与随机顺序有差异时保留。家族决策可在服务端复用，客户端无权威输入就不调用。
4. 分开内容准备、出生、AI决策、动作／释放、命中／状态、死亡／奖励与原包投影；保存区域／实体代次、中断标记、首次随机和事务结果，异步迟到及背压不重复执行。
5. 客户端只做有原版依据的修复／公共提取，与原D2GS共用同一消费者；禁止为自研宿主添加私有消息、敌对推断或伤害权限。
6. 更新所属幕的实际类型台账、精英／首领专用差异、原版依据与缺口。默认只修改，不构建／运行／打包，不编写测试脚本、用例或专用程序；需要保存语义变更时同步存档校验边界与文档。

逐项条目至少包含真实ID／原名、所在区域／难度、rank／固定身份、AI／攻击／技能、原表与原函数来源、公共函数、客户端支持、自研执行、替身／暂缓、已知差异及待核实项。原版参考结论须有独立定位信息，不依赖临时目录长期存在。全集以当前MPQ为准，未知行归[SPECIAL](SPECIAL.md)；分类入口齐备不代表全MonStats逐行审计已经完成。
