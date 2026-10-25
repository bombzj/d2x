# PD2怪物与伙伴差异

本页维护非玩家实体的身份、属性及生命周期差异。版本见[版本与证据](BUILDS.md)；召唤／攻击程序的算法归[技能](SKILLS.md)，区域来源归[地图](MAPS.md)，装备属性归[物品](ITEMS.md)。定义准备和AI执行分别归[怪物模块](../modules/MONSTERS.md)。

## 数据范围与新增身份

当前MonStats有1283个编译记录；本地原版表有734个。按Id名称与原版匹配，550条当前记录的名称不在原版表内；该数量不是550种新增敌人，其中包括召唤物、地图变种、事件控制器、NPC和首领子体，也不能由记录差推导全部出生来源。当前表没有增加原版之外的AI名称，但核心改写了AI程序表，不能仅凭AI列相同就复用未修改的原版行为。

现有同名记录也有差异：valkyrie从NecroPet改为ShadowMaster，ubermephisto／uberdiablo／uberbaal分别从UberMephisto／UberDiablo／UberBaal改为Mephisto／Diablo／BaalCrab；后面三者在PD2核心的对应回调中另有专属分支，不代表已退回普通首领。

基础生命、等级、伤害、防御、抗性、速度、组件和人口参数继续读取当前MonStats／MonStats2／MonLvl及区域输入，不为每个地图变种复制原版参数。真实身份、原图、AI程序、技能程序、出生／死亡及掉落是不同的接入条件；只有数据记录不能作为完整怪物实现的证明。

## MonStats与预设身份

已确认：目标`monstats.bin`有1283条、每条424字节，名称链接采用编译记录顺序；TXT的hcIdx不是该表的编译字段。row1198的BIN首WORD ID为1198，同名fallen1Nakrul的TXT hcIdx为1199；末row1282的BIN ID为1282，TXT hcIdx为1284。

预设名称链接及PD2客户端的classId／descfunc23文案按编译记录身份匹配，并保留同名首次解析；SuperUniques的hcIdx校验单独保留。不能把所有身份都改成行号，也不能把hcIdx缺号当成原图缺失。其他怪物消费者仍须逐项核对其字段所用身份。

| 消费者 | 当前入口／边界 |
| --- | --- |
| 预设名称与单位类型 | `src/world/retail/preset_scan.cpp`；名称取首次链接，数值类保留每个编译记录；段外DS1怪物标记保留直接原生编号，详见[地图](MAPS.md#ds1怪物标记与编译身份) |
| 物品交互／怪物属性文案 | `src/client/remote_inventory.cpp`、`src/content/items/item_descriptions.cpp`；原版保持既有身份匹配，详见[物品表现](../gameplay/items/PRESENTATION.md) |
| 怪物目录、类型与专用来源 | `src/content/monsters/{monster_catalog,monster_type_data,monster_identity}.cpp`按跳过分隔行的编译顺序；模式／生命表、出生／AI类、掉落准备与声音使用相同身份，不能用TXT物理行号或hcIdx代替 |
| Desecrate与召唤池 | `src/content/pd2/corpse_data.cpp`按名称取首次记录后转换编译编号；德鲁伊变形、Hydra、刺客及专用AI来源也使用该转换；SuperUniques仍有独立索引 |
| 客户端区域与渲染 | `src/client/remote_{town,combat,ui_npc}.cpp`及`src/presentation/remote/remote_scene{,_units}.cpp`使用相同编译身份；怪物大小、目标资格、动画／变换、灯光和姓名不再在1198之后错位 |

扩展怪物不一定在原版BaseId／NextInClass链中。D2MOO `DATATBLS_LoadMonStatsTxt`在表加载后按数值记录遍历链，名称链接忽略大小写并取首次记录；未遍历到来源记录时保留nChainId初值0，仅在编译模式记诊断。当前monstats.bin中imp1／imp5／impThrone／imp5bastion的偏移0x4A／0x4B初值均为0；BIN初值不是运行时计算后的链编号。`compiledMonsterChainIndex`复现该加载规则：imp5从原链得到4，未在原链中的impThrone／imp5bastion得到0，重复Id也按实际数值记录判断是否抵达。Imp及UnHolyBolt准备现消费该结果，不因未在链中而拒绝，也不继续查询空Id。

CatapultSpotter的专用四类关系继续独立校验，不能把其程序也直接换成上述链编号。替换技能缺少飞弹字段时返回未支持，不把空引用升级为整图加载失败；敌对实体随后可沿现有保留身份的替身路径准备，未因此恢复该专用AI。入口为`src/hosting/monster_actions_content.cpp`。

## 编译字段宽度与MonProp

MonStats2.MeleeRng按原BYTE编译。当前spiritofbarbs的TXT值999对应BIN值231，不能作为大于一字节的近战范围。消费者为`src/content/monsters/monster_catalog.cpp`，规则准备保留原宽度；255另走原武器类别后备分支。

当前fenris的TXT虽然列出inc_splash_radius，配套`monprop.bin`三个难度的该属性ID均为FFFFFFFF，原生跳过该槽。宿主也跳过，不能仅因属性名称存在而补成478。MonProp及召唤定义的具体边界见[召唤与诅咒](SKILLS.md#desecrate召唤和诅咒的数据边界)。

骷髅MonProp溅射使用真实单位接触事件；触发通知保留正在进行的攻击动作，不能重新播放施法／攻击造成短暂消失。算法、手位和伤害来源归[Do163](SKILLS.md#do163实际手位分配伤害与独立防御)，通用原包表现归[怪物表现](../gameplay/monsters/PRESENTATION.md)。

MonStats技能名称按原NAMETOWORD忽略大小写链接，失败编译为-1；D2MOO `MONSTERS_Initialize`不授予该槽。当前putriddefiler1dungeon（编译类1051）的Skill4=MonLowRes在Skills已无定义；配套monstats.bin的+0x170八槽为`335,92,588,-1,-1,-1,-1,-1`。准备已跳过这个原生无效槽，AI动作按链接后的Skills名称选择；表中存在但公式未准备的技能仍报出具体名称，不默默省略。

## 怪物技能程序准备

`src/hosting/pd2/monster_actions.cpp`按Skills的srvstfunc／srvdofunc、MonStats八个技能槽及真实动画准备PD2动作，不再要求新增技能沿用原版技能名称。服务端较大差异归`src/server/systems/skills/pd2/monster_skills.cpp`；原版动作准备和调度保持公共入口。普通ENDANIM后的NU等待复用[公共动作与AI时钟](../gameplay/monsters/COMMON.md#动作死亡与奖励)，使用当前PD2表的aidel；动作锁导致的唤醒推迟不视作独立AI等待，专用程序显式登记的未来等待仍保留，完整组合待实机核对。

| 程序 | 当前消费者与边界 |
| --- | --- |
| 无起手的Do8／10／17／22／24／26／28／73／80／118／151／159／166／169／175／182／183／187／188 | 复用已有PD2玩家／非玩家施法者的activatePd2、真实公式、飞弹扇形／分裂／环形和公共飞弹出生；不将武器接触或有起手程序归入这条路径 |
| 无起手的Do0且有srvmissile | 沿NativeMissile默认发射入口，保留原怪物攻击加成复制；空Do0不视为攻击程序 |
| 无起手的Do30且存在真实目标状态，排除dark_pact | 沿MonsterCurse的原状态／属性列表、目标筛选和发布生命周期；MonWeaken、MonLowerRes、MonAmplifyDamage等不再因名称不同而落入缺失飞弹分支。Dark Pact另走专用程序，不能当普通诅咒 |
| St53／Do95 | 沿已有怪物Inferno的实际事件帧、InfernoAnim／InfernoLen和持续发射，不按MegademonInferno等旧名称决定资格 |
| 无起手Do18／150／162、St31／Do67 | 分别准备真实状态防御技能、怪物Smite、Joust与Charge；状态、等级、模式和动画读当前表。Smite沿公共接触的A2属性与Calc2眩晕；Joust保留实际怪物模式属性，移动／接触／增益分阶段续办；坐标Charge允许空单位目标，释放时沿公共近距离搜索。已构建，尚未完成实机对照，Charge路径及跨帧时序仍受公共动作边界约束 |

准备只保留正基础技能等级，加原难度MonsterSkillBonus；施放和释放仍从真实单位属性采样等级。怪物技能程序加入实例的只读程序目录，不添加人物技能树节点，也不授予玩家技能。现有人物定义仅补必要的PD2程序元数据。重复序列事件重建每次回调的求值输入，背压等待同一释放，不重复提交已经出生的飞弹。

通用程序准备保留原家族的动作契约：Izual／UberIzual和BaalCrab／BaalCrabClone默认事件1，其他Act II及后续专用家族沿既有事件2，不把所有A2都视作近战事件1；Baal Inferno仍由Fn135固定使用S1／事件2，Horror Arctic Blast沿事件1。ArcaneTower的Lightning继续由专用准备选择MissA1、序列事件4及普通A1的独立时钟，Healing Vortex保留友军目标资格，不被通用攻击飞弹覆盖。

怪物专用技能也保留Skills的AuraState／AuraTargetState编译链接，独立于玩家技能可用性。`src/content/skills/description_data.cpp`保留对应名称输入，`formula_data.cpp`在未接入玩家程序的分支前链接状态；否则MonAmplifyDamage等虽有公式和施法动画，释放时却因目标状态为-1而拒绝。MonAmplifyDamage的真实状态为amplifydamage、auralencalc=ln34，Param3=300、Param4=0；持续时间继续接受当前诅咒抗性规则，不另造固定时长。

技能槽声明不等于AI调用。当前willowisp1Maus的aip7(H)=0，核心`0x356D70`委托原Fn025；原SC攻击仍使用MissC=willowisplightningbolt，不能因为Skill1列出Chain Lightning就改为施放该技能。其非零门槛分支另有召唤、计数和技能选择，尚未恢复。

RadamentBossMeteor的srvmissilea／b／c文本为radamentmeteorcenter，Missiles[984]名称为Radamentmeteorcenter；目标skills.bin的570号记录（572字节／条）偏移0x46的四个WORD为`-1,984,984,984`。这是忽略大小写的真实名称链接，不能误判为缺资源。`preparePd2SkillDefinition`和PD2怪物飞弹准备现使用同一原生链接目录；真正没有可用链接的程序仍不进入通用怪物弹幕目录。实际怪物准备返回未支持并保留敌对替身诊断，不创造飞弹，不让单项缺失中断整个区域。程序已接入不代表对应首领的AI、阶段、全部技能和掉落均完成。

## AI程序与原版回退

当前核心RVA `0x35FA50`通过全局`0x5DBE60`改写配套D2Game `0x10E788`的16字节AI表项。解析链为CRT初始化`0x16C780`→getter`0x31DAB0`→版本表`0x4A7D5C`首项；AI编号和字段另与D2MOO `AITHINK_GetAiTableRecord`核对。它登记28个Think回调及2个参数初始化回调；此写入清单只表示差异入口，不表示全部内部语义或D2X消费者已恢复。

| AI编号／原入口 | 字段 | 当前核心RVA |
| --- | --- | --- |
| 6／006_Fallen | Think +8 | `0x35A180` |
| 7／007_Brute | Think +8 | `0x357730` |
| 22／022_GreaterMummy | Think +8 | `0x35DFE0` |
| 25／025_Willowisp | Think +8 | `0x356D70` |
| 32／032_Npc | Think +8 | `0x357180` |
| 35／035_CorruptArcher | Think +8 | `0x357330` |
| 45／045_Sarcophagus | Think +8 | `0x357320` |
| 47／047_FlyingScimitar | Think +8 | `0x356FC0` |
| 49／049_ZakarumPriest | Think +8 | `0x357640` |
| 50／050_Mephisto | Think +8 | `0x3579E0` |
| 51／051_Diablo | Think +8 | `0x357B90` |
| 53／053_Summoner | Think +8 | `0x358350` |
| 61／061_Hireable | Think +8 | `0x35A030` |
| 71／071_Regurgitator | Think +8 | `0x35A470` |
| 72／072_DoomKnight | Think +8 | `0x35A6F0` |
| 74／074_OblivionKnight | Think +8 | `0x35AE60` |
| 87／087_TrapMelee | Think +8 | `0x35B1B0` |
| 89／089_Megademon | Think +8 | `0x357550` |
| 93／093_ArcaneTower | Think +8 | `0x35C8F0` |
| 99／099_TrappedSoul | Think +8 | `0x35BA20` |
| 102／102_BladeCreeper | Think +8 | `0x35E610` |
| 105／105_ShadowWarrior | 参数初始化 +4 | `0x35F310` |
| 105／105_ShadowWarrior | Think +8 | `0x35EB40` |
| 106／106_143_ShadowMaster | 参数初始化 +4 | `0x35F3F0` |
| 114／114_ReanimatedHorde | Think +8 | `0x35D820` |
| 115／115_SiegeBeast | Think +8 | `0x35E120` |
| 120／120_Overseer | Think +8 | `0x35DA60` |
| 133／133_Ancient | Think +8 | `0x35C980` |
| 135／135_BaalCrab | Think +8 | `0x357D40` |
| 141／141_BaalMinion | Think +8 | `0x357EE0` |

AI字段与专用身份须分别核对。当前`0x357550`的Megademon分支检查MonStats偏移0x7E，并使用偏移0x84；按当前记录布局分别是aip7(H)与aip8(H)，不随游戏难度选择。megademonboss两个字段均1350。`0x357640`的cantorboss分支使用aip7(H)=66作生命门槛，之后转入额外技能／移动；不能套用原ZakarumPriest的参数解释或把1350钳成百分比。

FlyingScimitar的普通Fn047已沿D2MOO恢复到公共AI：aip1为靠近概率、aip2攻击概率、aip3等待帧、aip4绕行概率；两次靠近概率判断独立抽样，分别走半径8接近与原Flags7后备，近战失败后才判断半径2绕行。PD2`0x356FC0`在aip7(H)=0时委托原Fn047，因此普通flyingscimitarArcane／Caldeum等变种可以使用该入口；act2hireTraitorBoss的非零分支包含轮换光环、计数与专用攻击，尚未恢复，宿主保留未支持／敌对替身边界；不能据普通家族可运行宣称该首领完成。

已定位的额外身份分支如下。这些是当前核心的静态分支及未实现边界，不能按同名原AI视为完成；完整算法仍需恢复调用约定和被调辅助器。

| 当前编译身份 | 当前核心入口／条件 | 差异与消费者边界 |
| --- | --- | --- |
| ubermephisto／704、uberdiablo／705、uberbaal／709 | `0x3579E0`、`0x357B90`、`0x357D40`按MonStats首WORD比较704／705／709 | 在委托原首领AI前另有仆从计数、随机选择和出生；专属数量／位置／所属关系未接入，普通首领AI不包含这层行为 |
| leoricMapBoss／998 | `0x35D820`比较998且有目标 | `server/systems/ai/pd2/leoric.cpp`已接专用决策和真实属性446写回；五槽动作准备见上表。无目标仍委托原ReanimatedHorde；消费者已构建，完整场景尚未实机对照，不能计为P3完成 |
| GuardianOfFate／1064、GuardianOfFateClone／1065 | `0x35A470`比较1064／1065且有目标 | 已接专用CRT决策、FIFO寻位和Blink；909的SrvHit6出生消费者已接，完整场景／生命周期未闭合 |
| ImperialPalaceBossJerhyn／1168 | `0x357180`比较1168 | Npc回调中拥有计数和独立动作，属于专用控制程序，不能由普通城镇NPC服务或敌对替身代替 |

Uber三包装器的已核定边界为：704／3579E0要求目标存在，先用36DB70抽一次，357860计数后以有符号`(60-count)*2`比较且要求count<40；705／357B90要求目标存在，先抽小于30，再计数并要求第二类count<15；709／357D40不加该目标门槛，先抽小于20，再计数并要求第三类count<20。357860只遍历本房NearRoom单位链，三类分别是725–730、712、731–732，不按主人GUID计数；它还处理非首领12模式单位的后继，不能由统计存活仆从替代。选池额外消费CRT，落点两轴使用36DB70；半径按MonStats+0x52的难度字节减AITick+0x14再限3–20。池容器、坐标查询ABI、死亡后继、出生及归属339CB0仍待闭合，现有原首领AI未接这些附加程序；不据概率／数量已知就放开事件。

Leoric的当前指令链`0x35D820→0x36DB70`每次专用思考只抽一次PD2单位随机并取模100，不能使用普通SEED或CRT。读取总属性446／mon_cooldown1；小于1时把同一结果模10加15写回基础属性，调用第一技能槽；否则先减一。随后小于20且无状态14时对自身调用第三槽；小于60时距离大于15调用第四槽，小于30且距离小于10时先登记30帧等待再向采样坐标调用第二槽，其余接触范围调用第五槽。剩余分支在接触范围普通A1，否则走向目标并使用MonStats+0x51的等待。`0x39F500`五个栈参数为技能、单位目标、X、Y、模式；第二槽明确传空单位，不能从失真的伪代码把X当0。当前五槽为LeoricSummonNova／MonCharge／Bone Armor／Joust／MonSmite，技能ID／等级及动画不在AI内写死；冷却保留在单位原StatList而非只存AI私有计数。

GuardianOfFate的`0x35A470`使用CRT流，三项AI记忆为延后Blink标记、召唤概率累加及已召唤计数；本体最多六次，克隆不走召唤分支。延后Blink先清标记并各抽X／Y偏移[-8,7]，寻位成功即施放并返回；失败后才继续本轮百分比。召唤未命中时本体累加`6-计数`，普通分支小于80施放VoidBolt、80–94再次尝试Blink，其余按难度aidel等待。当前MPQ确认1064／1065分别为本体／克隆，AI字段均为Regurgitator；前三槽是Blink357、VoidBolt493、GuardianOfFateSummon550，克隆没有第三槽。`server/systems/ai/pd2/guardian.cpp`已在真实身份／有目标时接此分支，无目标仍走原家族；CRT借用现有宿主pd2ProgramRandom，三项记忆沿公共AI切换清零，待执行动作保留坐标和决定，不因容量重试重掷。

这两个身份的Blink与本体召唤使用MonStats的A1、CS原图；`data/global/monsters/cs/cof/csa1hth.cof`只有动作事件1，不能套Regurgitator普通技能默认的事件2。`hosting/pd2/monster_actions.cpp`已对1064／1065的A1选择事件1，修复克隆因动作时钟准备失败而落入沉沦魔替身的问题。复查Blood Moor准备结果已无Guardian克隆替身；管理出生分别保留真实本体／克隆代码及三项／两项技能。此项验证不包含完整AI随机轨迹、Orb命中出生及首领场景。

寻位`0x387C60`不是原版空位搜索的简单包装：它从网格(10,10)开始，经`0x387C10`限制到半开范围[10-r,10+r)，r=min(传入范围,10)；坐标间距固定4，候选为输入坐标加网格偏移乘4。方向常量`0x44DFA0／0x44ECC0`按(-1,0)、(0,1)、(1,0)、(0,-1)排列；容器经38B5E0在offset+size处写入并增加size，38B3E0读offset处、38B3C0递增offset，已确认先进先出；邻居入队立即标记访问，因此是按上述固定方向的广度优先搜索。Guardian传范围4、尺寸2、掩码0x101及不偏移结果；`0x387BB0`每个候选先更新房间再调用原2×2矩形查询，区别于原生size2的五格十字。专用AI已按此恢复；Blink的同区消费者、区域阻挡及剩余边界见[传送落点](SKILLS.md#pd2传送落点与怪物blink)。

当前召唤依赖550→908GuardianOfFateSummonOrb→909GuardianOfFateSummonHit。909的SrvHit6在安装器3A6D8C挂到3A5360，经39F0A0及当前版本表4A710C绑定配套Game47CF0的预设出生；sHitPar1／2／3为1065／1／67239936，即克隆身份、NU模式及0x4020000的无经验／无掉落标志。三项在当前TXT与BIN一致，不能从错误列名HitPar读空值。hosting按当前SrvHit6依赖准备真实原型；`missiles/pd2/hit.cpp`记录有序出生操作，`monsters/pd2/missile_spawn.cpp`复用原房间搜索、普通预设首次-1及可重试类的4环后备、NativeBirthPublication。半径零也消耗方向／符号抽样，后备继承首次的房间随机；不先探测再重复出生。克隆不自动建立主人／仆从归属，因为该原回调没有这一步。

当前SrvHit6接普通、无附属碰撞物件的NU／S1出生；859→999 Leoric守卫及864→1001攻城兽火山的sHitPar2均为8，按实际S1资源准备birthTicks并沿公共出生等待／模式事实，缺动画明确暂缓。979额外的完整目标位及INITSEEDSET已接，其他未核实模式、特殊预设／暗金ID或部分目标位仍拒绝；当前五个直接调用者见[技能](SKILLS.md#刀刃反射)。出生基础调用对照D2MOO MonsterRegion／MonsterSpawn。Guardian决策、Orb和克隆出生的先前构建及管理准入已观察本体357／493／550及克隆357／493技能，完整战斗场景未验；新补的跨区Blink及出生标志已构建，完整跨区／出生场景尚待验证。Hell集合的空初始化证据及外部配置边界见[传送门控](SKILLS.md#pd2传送落点与怪物blink)；完整休眠重建、全局随机交错及场景死亡／奖励／释放仍未闭合，不据此宣布P3场景完成。

## 召唤实体与伙伴

当前死灵内容准备为`src/content/pd2/summon_data.cpp`，德鲁伊／刺客继续使用各自领域准备；PetType使用原生idx，不能跨Expansion分隔行直接取TXT物理行号或Id。骷髅、法师、弓手、石魔的MonStats、MonProp、隐藏攻击及数量／生命公式使用当前表。

弓手复用受控远程角色的实现载体，但保留自身原生class、组件、飞弹、技能及属性，不把实际身份替换为法师。Desecrate死态实体无经验／掉落、部分出生失败保留已创建尸体及到期取消选择的规则归[技能负责条目](SKILLS.md#desecrate召唤和诅咒的数据边界)，不与自然人口生成混为一条路径。

佣兵服务、装备槽／资格及战斗程序是不同能力。CN七槽原请求及当前资格见[佣兵](../gameplay/characters/HIRELINGS.md)，服务回调见[城镇](TOWN.md)；它们不证明自有宿主具备全部PD2佣兵战斗差异。

自有宿主的PD2动作准备独立于原版，入口为`src/hosting/pd2/hirelings.cpp`。身份、等级、装备、雇佣／复活及公共Fn061选择继续复用；当前Hireling表的隐藏技能也准备为实际佣兵的SkillRules，不借主人施法。Mode使用佣兵动画，SQ从MonStats.Skill／SkNmode链接MonSeq；第四幕SC使用当前AnimData的事件2。DefaultChance为零不要求准备不存在的默认攻击钟；罗格默认RogueMissile从SrvMissileA取得飞弹，无武器时保持原生弓动作身份。光环、被动、诅咒、近战、施法及MonWhirlwind分别接所属公共程序。

当前核心`0x35A030`在加权选择前维护首槽光环，chance0不禁用该槽。`0x356A00`为Hireling Id25／27／29维护Battle Orders：自身或主人缺状态且距离小于30时施放；其余同队玩家／本人伙伴在每100帧的前5帧检查，不新增随机抽取。独立消费者为`companions/pd2/hireling.cpp`。原NodeIndex11资格与复杂伙伴目标组合仍待完整对照；不能将PlayerPets的inactive标记等同于NodeIndex。

罗格Strafe使用实际佣兵武器、原范围／射数公式及GUID后继选择；每次Do12后按Param6回卷公共原生动作时间线。第三幕Static Field以佣兵为实际施法者执行Do160与难度生命下限，随后施加原目标状态；第五幕战吼对实际佣兵及合资格目标生效。程序位于`skills/pd2/monster_skills.cpp`，复杂装备、极端速度及全部佣兵类型组合仍需分别核对。

伙伴换区后，旧房间可能已经停止AI事件。`companions::System::step`对离开主人活动房间／区域或主人传送版本变化的伙伴调用既有think／跟随程序并重新安排AI，保持原跟随距离、碰撞、失效和繁忙门槛。Iron Golem沿既有金属材料消费、装备来源及原生ironGolem保存／恢复；Revive保留原实体、家族动作、限时及所属关系，家庭AI与伙伴跟随分别调度，不能将复活等同于替换怪物类型。单个家族可运行不代表全部复活家族已认证。 铁魔材料的持续耐久／数量／充能已接原物品时钟及实体／主人材料原子提交，具体规则见[材料再生](ITEMS.md#物品充能恢复)。刷新装备来源时递归比较Item子列表，避免只比较根base而漏掉参数层变化；原版消费者共用该来源更新修正。新代码已通过双端构建，召唤、换区、死亡、离局／读档实测仍未完成。

地图状态198的出生与玩家换区回调已分别核定，现有伙伴不直接套用仅限UnitType0的389040；规则和未闭合重建范围见[地图来源](MAP_PROPERTIES.md#路由和单位属性)。连续影子／陷阱的施法事件重新登记及Raven空起手的ItemCheckStart已补源码，资格、延迟和未准备的召唤变体仍沿[物品触发](ITEMS.md#装备触发与装备自带技能)。铁魔材料已接真实耐久上限／破损装备资格及持续恢复时钟，仍需统一运行覆盖跨区、死亡和原生kf保存／恢复；全部Revive家族的等价认证独立保留。隐藏佣兵Bone Spear的extra_bonespears使用怪物自己的零初始化扩展属性加AC的MonProp与状态，损坏／缺失列表保留未知，不借用主人值。

## 新增怪物、首领与掉落

PD2自有宿主使用当前MonStats／Skills等定义并复用公共人口、AI、战斗及掉落系统。`src/content/pd2/town_data.cpp`调用公共掉落准备；没有独立的“全部PD2新增怪物已实现”保证。地图新增怪物及首领的出生、特殊AI、免疫／伤害、死亡程序和专属掉落需逐条定位。

当前880 torajanBossPoisonEgg的Skill1已从普通Trap射线分派修正为PD2毒环后孵化；子怪仍按MonStats.spawn准备，客户端同时消费毒环与序列暂停。当前DLL／原版回调、消费者和未验证边界见[PoisonEggSpawn双端委托](SKILLS.md#poisoneggspawn的双端委托)。此处只是技能链补缺，不代表Torajan场景控制器完成。

Open Wounds对扩展首领935／936的镜像写入尚未接入；该编号限定当前版本，规则归[物品](ITEMS.md#open-wounds与crushing-blow)。Do163对攻击者1112的特殊防御修正限定该接触程序，不能推广成所有怪物攻击的统一减免。

## 待核实与未完成

新增身份的全部名称／class链接、上述改写AI的专属分支／参数初始化、其余起手／接触／持续技能、地图／首领程序、全部Revive家族、伙伴／光环与复杂装备组合仍未全部恢复或认证。数据行、资源原图、复用原AI和已接专用程序分别描述；中立单位不可用敌对替身填补未知行为。怪物弹幕和普通家族已接入不等于全部550条新增记录或全部首领完成。
