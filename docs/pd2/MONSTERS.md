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
| leoricMapBoss／998 | `0x35D820`比较998且有目标 | 独立计数、技能轮换、自身状态及远近攻击选择；不能只执行原ReanimatedHorde |
| GuardianOfFate／1064、GuardianOfFateClone／1065 | `0x35A470`比较1064／1065且有目标 | 使用AI参数记忆、位置随机化和动作概率增长；克隆与本体分支不同，未恢复完整执行 |
| ImperialPalaceBossJerhyn／1168 | `0x357180`比较1168 | Npc回调中拥有计数和独立动作，属于专用控制程序，不能由普通城镇NPC服务或敌对替身代替 |

## 召唤实体与伙伴

当前死灵内容准备为`src/content/pd2/summon_data.cpp`，德鲁伊／刺客继续使用各自领域准备；PetType使用原生idx，不能跨Expansion分隔行直接取TXT物理行号或Id。骷髅、法师、弓手、石魔的MonStats、MonProp、隐藏攻击及数量／生命公式使用当前表。

弓手复用受控远程角色的实现载体，但保留自身原生class、组件、飞弹、技能及属性，不把实际身份替换为法师。Desecrate死态实体无经验／掉落、部分出生失败保留已创建尸体及到期取消选择的规则归[技能负责条目](SKILLS.md#desecrate召唤和诅咒的数据边界)，不与自然人口生成混为一条路径。

佣兵服务、装备槽／资格及战斗程序是不同能力。CN七槽原请求及当前资格见[佣兵](../gameplay/characters/HIRELINGS.md)，服务回调见[城镇](TOWN.md)；它们不证明自有宿主具备全部PD2佣兵战斗差异。

自有宿主的PD2动作准备独立于原版，入口为`src/hosting/pd2/hirelings.cpp`。身份、等级、装备、雇佣／复活及公共Fn061选择继续复用；当前Hireling表的隐藏技能也准备为实际佣兵的SkillRules，不借主人施法。Mode使用佣兵动画，SQ从MonStats.Skill／SkNmode链接MonSeq；第四幕SC使用当前AnimData的事件2。DefaultChance为零不要求准备不存在的默认攻击钟；罗格默认RogueMissile从SrvMissileA取得飞弹，无武器时保持原生弓动作身份。光环、被动、诅咒、近战、施法及MonWhirlwind分别接所属公共程序。

当前核心`0x35A030`在加权选择前维护首槽光环，chance0不禁用该槽。`0x356A00`为Hireling Id25／27／29维护Battle Orders：自身或主人缺状态且距离小于30时施放；其余同队玩家／本人伙伴在每100帧的前5帧检查，不新增随机抽取。独立消费者为`companions/pd2/hireling.cpp`。原NodeIndex11资格与复杂伙伴目标组合仍待完整对照；不能将PlayerPets的inactive标记等同于NodeIndex。

罗格Strafe使用实际佣兵武器、原范围／射数公式及GUID后继选择；每次Do12后按Param6回卷公共原生动作时间线。第三幕Static Field以佣兵为实际施法者执行Do160与难度生命下限，随后施加原目标状态；第五幕战吼对实际佣兵及合资格目标生效。程序位于`skills/pd2/monster_skills.cpp`，复杂装备、极端速度及全部佣兵类型组合仍需分别核对。

伙伴换区后，旧房间可能已经停止AI事件。`companions::System::step`对离开主人活动房间／区域或主人传送版本变化的伙伴调用既有think／跟随程序并重新安排AI，保持原跟随距离、碰撞、失效和繁忙门槛。Iron Golem沿既有金属材料消费、装备来源及原生ironGolem保存／恢复；Revive保留原实体、家族动作、限时及所属关系，家庭AI与伙伴跟随分别调度，不能将复活等同于替换怪物类型。单个家族可运行不代表全部复活家族已认证。

## 新增怪物、首领与掉落

PD2自有宿主使用当前MonStats／Skills等定义并复用公共人口、AI、战斗及掉落系统。`src/content/pd2/town_data.cpp`调用公共掉落准备；没有独立的“全部PD2新增怪物已实现”保证。地图新增怪物及首领的出生、特殊AI、免疫／伤害、死亡程序和专属掉落需逐条定位。

Open Wounds对扩展首领935／936的镜像写入尚未接入；该编号限定当前版本，规则归[物品](ITEMS.md#open-wounds与crushing-blow)。Do163对攻击者1112的特殊防御修正限定该接触程序，不能推广成所有怪物攻击的统一减免。

## 待核实与未完成

新增身份的全部名称／class链接、上述改写AI的专属分支／参数初始化、其余起手／接触／持续技能、地图／首领程序、全部Revive家族、伙伴／光环与复杂装备组合仍未全部恢复或认证。数据行、资源原图、复用原AI和已接专用程序分别描述；中立单位不可用敌对替身填补未知行为。怪物弹幕和普通家族已接入不等于全部550条新增记录或全部首领完成。
