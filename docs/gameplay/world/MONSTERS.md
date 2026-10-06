# 怪物行为与实现范围

联机第一幕采用原D2GS的全部怪物AI／攻击／复活／召唤／掉落／任务权威，57个原表池身份与固定首领的客户端动作、序列和弹体接入见[联网模块](../../modules/NETWORK.md#第一幕怪物联机接入)。下方AI执行器和历史单机冒烟记录不代表客户端联机结算，也不能计作本轮原服验证。

本页维护已实现的怪物家族行为、参考依据及剩余差异；生成头、身份、能力与活动词缀的代码边界见 [怪物模块](../../modules/MONSTERS.md)。旧阶段证据不认证之后的源码，交付状态见 [项目基线](../../../BASELINE.md)。

怪物身份、区域名单、分组和掉落继续以运行时 MPQ 为准；类型替身只用于尚未实现的敌对外观。当前第一幕 `Levels.txt` 的 `mon1–mon10` 与 `nmon1–nmon10` 分别有 55 个唯一身份，名单相同，均已有外观与基础 AI 分支；这不代表完整行为已完成，当前缺口见下方复审。普通级别怪物共用 `MonStats`／`MonLvl` 的三难度生命、A1／A2、命中、防御、暴击、再生和抗性解析；某行没有 A1 近战列时仍读取其余字段。共享数值只需集中核对，不逐个怪物重复验算。精英、固定首领与 Boss 单列；本轮只审阅源码与原表。通用地图寻路、目标调度和画面帧时钟仍是项目适配，验收不声称逐帧等同原引擎。

阶段记录中的“存读档”均是 v80 及更早版本的历史会话快照冒烟。当前 D2S v96 角色存档不保存怪物、尸体、弹体或怪物 AI 状态；载入后从城镇开始新的一局。冻结死亡现按原 States 的保留／碎裂／隐藏／禁选标志走公共入口；冰尖柱直接致死与冻结期间死亡均隐藏普通尸体，召唤、复活和见尸反应不能使用碎尸，收益仍正常结算。原碎冰／融化图与声音已接源码，精确客户端组合边界及未构建／运行／提交状态见 [冻结死亡](../skills/COMMON.md#冻结死亡与尸体资格)。

亚马逊诱饵／女武神以原dopplezon／valkyrie身份注册 `AmazonPet`，不使用敌对替身；属性从当前原技能／MonStats／MonLvl派生，女武神装备取原MonEquip并独立归宠物。诱饵固定、到期或跨区清理；女武神使用公共跟随／近战／旅行链，读取原装备动作和距离，均无经验／掉落及可用尸体。原States角色伪装组件已接入，精确客户端变换、路径和AI调度仍有适配边界，见[亚马逊](../skills/AMAZON.md#被动与魔法技能整页)。

## Act 1 复审与下一步

2026-10-01：复审依据当前源码、当前 `assets/mpq2` 的 Levels／MonStats／MonStats2／DifficultyLevels／MonPreset／SuperUniques，以及固定本地 D2MOO `5596f5c`。以下九项表格保留修复前的审阅证据；当前状态以本节各阶段交接为准，不能继续把旧源码行号视为现状。

### 安达利尔战斗与掉落

2026-10-01：andariel 注册真实 AN 原形，保留 MonStats 原 boss／primeevil 等级、MonLvl 比例、生命、近战、再生／暴击、抗性和体积；不装随机精英强化、不用区域等级替换 boss 等级。当前普通难度为 12 级、1024 HP、火抗 −50、毒抗 80。Fn034 按原 aip1–4 顺序决策：贴身按喷毒概率选 Skill1 或 A1；非近战先掷 5 帧等待，再掷攻击概率和喷毒／毒弹，失败持续走近。公共受击／GH、冷效果、毒抗、佣兵和召唤物 primeevil 伤害倍率、击杀收益沿现有消费者。

技能动作：AndrialSpray（164）使用 seq_andarielspray 的 SC 18 帧、4–12 帧九个 event2；起手保存目标，原 SrvDo088 八方向／九事件偏移表逐枚发 andarielspray（32）。AndyPoisonBolt（201）以原 A1 共用动作事件发 andypoisonbolt（203），不能要求普通弓手 event2；最终冒烟发现这一错误后修复。Missiles 原速度／寿命分别为 15／40 帧、20／50 帧，毒速率 32–64 定点、HitShift=0、毒长度 400／800 帧；原 Sk*lvl 及分段字段在内容层解析，命中共用毒抗、毒长度抵抗、强毒覆盖和持续伤害，不把毒当即时魔法伤害。SC 保留原事件帧范围，受击打断清起手目标；A1／WL／GH／DT／DD、毒弹 DCC 与 MonSounds 由 MPQ 原资源加载。

掉落和任务：死亡沿原 Boss 身份取 MonStats TCQuestId／TCQuestCP 对应的首杀 Andarielq，否则 Andariel；复用 TC／品质／物品生成和经验事务。未完成任务的玩家击杀额外按 A1Q6 原七种代码池掉两颗碎裂宝石和一颗普通宝石，不使用随机首领 TC；后续不重复任务宝石。死亡在邻房 35 范围的同阵营怪物预约 1–50 帧死亡，依据 QUESTSFX_Andariel。A1Q6、NPC 成功对白资格、凯恩代救及当前回城门复用已有任务链；不新增存档字段。

依据本地 D2MOO 5596f5c：AiThink::Fn034、SkillMonst::SrvSt46／SrvDo088、D2Common Monsters::11053／11055、MonsterUnique::UMod28_Event、QUESTSFX::Andariel／MainHandler、A1Q6::Callback08／Timer_StatusCycler，公共 MonsterMode／SUnitDmg 及 MonStats TC 消费者。原表和原资源始终来自当前 assets/mpq2，不提交 reference 或抽取素材。

普通难度包内证据（seed=210，临时女巫）：自然 andariel 非替身、combat 有效，原形截图已查看；A1／SC 决策实际发生，角色中毒约 14.92 秒、6.0546875 HP/秒；远距另观察到 32／203 两种毒弹和约 26.28 秒／4.00390625 HP/秒中毒。常规承伤 1024→1023；首杀日志 Andarielq、65 经验、额外 skc/skc/sku、deferred=null，A1Q6=3 和回城门开启；保留任务进度重建后日志 Andariel、无任务宝石。两个正常实例退出码 0，不读写角色存档、无新测试程序，仅普通难度。贴身弹体立即命中消失，因此弹体计数不构成九次释放逐帧认证；九事件来源已原表／源码核对。

仍未完成：原路径整数方向量化目前按连续坐标角度映射原八方向，房间／寻路和弹体创建伤害快照仍沿项目适配。客户端 andycontrol0、火柱／落石／火墙死亡演出、原空间声音和精确事件节拍没有完整本地 D2Client 证据，未自造图块或演出；当前 DT／DD 和死亡声音不代表完整原结尾。回城门沿既有即时创建，不是 A1Q6 原任务定时器延迟。未穷举九事件朝向、抗毒装备／低生命全部分支、佣兵／召唤物战斗、死亡清场／任务门延迟及多人任务首杀规则；第二幕仍未实现。不能将本批核心战斗／掉落接入称为完整原引擎复刻，构建／包与提交状态见 [构建](../../development/BUILD_AND_RUN.md)。

### 专属首领与毒系骨灰

2026-10-01：Boneash 的 skmage_pois3 开放真实 SK 原组件／色表／声音、SkeletonMage 决策及 MissA1=skmage1；毒弹 SrcDamage=128 从 MonStats 的 A1 毒属性取得速率与时长，不自造毒系弹体或套火／电伤害。Boneash／Smith／Griswold／Countess 使用已有 SuperUnique 固定词缀、准确直属随从、生命与收益初始化。新增 Smith（5P）、Griswold（GZ）、BloodRaven（CR）原形索引和各自 AnimData／COF；Griswold、BloodRaven 保留 boss 原表等级，仅这两个身份开放已核实的战斗解析，其他 Boss 继续受限。普通 corruptrogue3 不进入女伯爵特殊状态。

铁匠按 Fn098：贴身 A1，非近战以 75+(100−生命百分比)/2 的加法速率持续走近；原表空白的火伤字段不补造。格瑞斯华尔德按 Fn090：贴身 80% A1、非近战 50% 走近，否则等待 10 帧；固定 Mod7 共用成功攻击的伤害加深。女伯爵按 SpecialState13：保存出生锚点，当前房间索引不一致或离家超过 40 时返回，远处异房目标在出生点等待；四个原 DS1 MapAI 节点按顺序以 A1 出手施放，末次施放超过 700 帧重开循环；近战 aip3+10，非近战 aip1 跑近及 aip2 等待。CountessFirewall 由原技能关联 maker=653、fire=331，创建两侧垂直 maker 和中心火焰；maker 速度／7 帧与子火焰 1000 帧、HitShift=1、逐帧定点火伤、原 DCC 均来自 MPQ。

血鸟按 Fn059：保留出生锚点、45／50 格警觉和回家约束、远距接近、每次决策累加 3 的召唤概率及 8+2×难度尝试上限；原随机轴／符号在目标附近选择 Nest 位置。S1 序列 20 帧、第 15 帧召真实 zombie2，复用局部出生检查且召唤单位无经验／掉落；Quick Strike 为原 7 帧 A1 序列、第 6 帧发 raven1，不压缩播放整个 A1 图。射击／绕行／退避沿原概率顺序，走跑及绕行速率由公共动作执行器消费。血鸟和女伯爵尸体禁选；血鸟死亡按 QUESTSFX 在邻房 35 范围的敌对亡灵预约 25–124 帧死亡，埋骨之地任务死亡入口和卡夏奖励资格仍共用现有正式任务链。

任务联动：铁匠不以死亡直接完成交易工具，仍须 8 级、原 Malus 交互、拾取 hdm、交还恰西；格瑞斯华尔德的诅咒／死亡不替代原凯恩囚笼救援。女伯爵使用原固定 TC（包含原符文子表），首次任务完成为原 371 任务箱启动 towerchestspawner 的 400 帧／第 150 帧开启／8 帧金币间隔／范围 5 调度；三轮魔法 TC 和两瓶生命／两瓶法力沿 ObjMode，金币量沿等级规则。参数在内容层类型化，同帧重复命令不重复发金币，任务箱状态只留本局，未增加 D2S 字段。

依据本地 D2MOO 5596f5c：AiThink::Fn064／Fn098／Fn090／SpecialState13／sub_6FCE5520／Fn059，MonsterUnique::SpawnSuperUnique，MonsterSpawn 的 BloodRaven 初始化，SkillMonst::SrvSt49／SrvDo091／SrvSt50／SrvDo092，SkillSor::SrvDo024，MissMode::SrvDo05／06／18，QUESTSFX::Bloodraven／MainHandler，A1Q5::Callback08／SpawnTowerChestMissiles，ObjMode::sub_6FC75EB0 及 Items 的原药剂选择；原数值与图始终读取当前 MPQ，不提交参考表或资源。

普通难度运行证据（seed=210，临时女巫）：五个自然身份均为非替身且有有效 combat；骨灰毒伤 2.9296875 HP/秒、剩余约 7.32 秒；铁匠 154/404 HP 追击速率 106%；格瑞斯华尔德施加 skill66／rank2／state9 伤害加深；女伯爵 aiPhase=4、22 个原火焰、击杀正常 TC、塔楼 stage4，任务箱同帧物品 15→15；铁槌正式拾取后 A1Q3=4。血鸟序列时长 A1=.52／QuickStrike=.28／Nest=.8 秒，真实僵尸召唤、死亡后 A1Q2=3 且召唤僵尸 HP=0；原形截图已查看。最终序列帧、禁选状态及出生范围收尾后复验正常；最后绕行速率修订编译和运行退出码 0，但未观察到该分支，不能记为实机通过。未新增测试脚本／用例／专用程序、未读写角色存档；运行包和提交状态见 [构建](../../development/BUILD_AND_RUN.md)。

适配与未覆盖：原房间 D2Common_10095 的子房间判定以现有 RoomLayout 替代；MapAI／碰撞／maker 连续坐标路径和消费顺序不是原 DRLG／路径逐种子认证。Nest 搜索、血鸟完整技能起手空间许可／客户端声音与死亡闪电演出、火墙随机起帧／衰减／混色、塔楼不可见控制单位／生成器音效和金币自由格搜索仍沿现有执行器适配。未穷举 AI 概率、全体目标、塔楼重访时调度、完整恰西交还／灌注或卡夏领奖，不扩展其他幕／安达利尔。下方旧表的“替身／未解析”状态以本节为准。

### 已有基型固定金怪

2026-10-01：仅开放 Bishibosh、Bonebreak、Coldcrow、Rakanishu、Treehead WoodFist、Pitspawn Fouldog、Corpsefire、The Cow King 的完整共用实例初始化；其他固定首领／Boss 不因此套普通基型公式。SuperUniques.Class／Mod1–3／MinGrp–MaxGrp／各难度 TC 与名称仍读当前 MPQ。首领保留 SuperUnique 身份，原普通生命先掷骰再应用独特生命百分比、等级 +3／经验五倍／停再生及固定词缀；普通不额外抽随机词缀，噩梦追加一项、地狱两项，沿 MonUMod 原过滤与权重、不重复固定项。直属随从复用 bUnique=false 的强化和收益，不继承主人事件。Stacks=0 的重复同区预设只放一次，重访保留已有区域实例。

Bishibosh 复用巫师原两次射击／近战／命令，再接暗金回调：附近普通沉沦魔或巫师尸体不要求直属／同变体，排除 Unique／Champion／SuperUnique、碎尸和不可选尸体；整数平方坐标距离与出手复验一致，保留遍历末个合格目标。MonStats2.ResurrectMode 在内容层类型化，巫师 NU 立即恢复，沉沦魔仍用原 S1；未虚构缺失的巫师 S1。普通巫师仍限原身份指定的准确直属沉沦魔。依据本地 D2MOO 5596f5c 的 MonsterUnique::D2GAME_SpawnSuperUnique_6FC6F690／SpawnMinions、AiThink::Fn013／TargetCallback_FallenShaman、SkillMonst::SrvDo097_Resurrect、MonsterSpawn::GetResurrectMode；墓穴 Bonebreak 使用既有 DrlgMaze 原 147–150 特殊房间，关卡为 18，不是洞窟第二层 13。

固定首领用原 TBL 名称／金色血条／词缀描述；生命和死亡动画共用 SuperUniques.Utrans 三难度映射。当前 MPQ 的全局 randtransforms.dat 为 30 张映射，固定字段范围包含 31，按两张保留槽减 2 读取全局图，而不是普通 TransLvl+2 的物种 palshift.dat。原图长度与透明索引校验保留；本地 reference 没有完整客户端消费者，索引／混色仍为资源结构适配，不能声称逐像素认证。AutoPos 字段已读取，但仍沿当前 DS1 点位及局部调整，不是原房间自动选位；去重范围当前为区域规划器。原房间随机流、动态占位、完整模式事件／弹体创建快照及声音边界继续沿公共实现。

普通难度包内冒烟：seed=210，临时女巫通过正式传送接近自然预设；八个首领均有真实基型、原家族 AI、有效 combat／enchantment、固定词缀和准确直属随从。随从数量按上列顺序为 2／5／4／8／2／4／5／6。常规 1 HP 承伤成立；Bonebreak／Pitspawn／牛王击杀固定 TC 正常，deferred=null，直属随从经验正常。Bishibosh 复活无主普通巫师至 7 HP；Rakanishu 受击同帧生成 8 枚 chargedbolt，Coldcrow 死亡 4 帧后 64 枚新星。牛王原图截图已查看。修复了冒烟发现的巫师 S1 缺图和 Utrans 读错局部色表；最后复活模式装载收尾另做普通复验。后续验证按用户要求只做普通难度，未穷举全部战斗／概率分支、火死亡伤害／诅咒／幽灵一击和客户端精确效果。未新增测试程序，临时角色不读写存档，D2S v96 不变。

牛王本体与普通 Cow King TC 已接，正式牛场入口、牛王击杀资格与额外奖励仍暂缓：D2MOO A1Q4_CreateCowPortal 要求资料片 A5Q6 通关奖励标志、营地、A1Q4 CUSTOM6／7；当前项目没有第五幕通关流程，不伪造标志或以开发 travel 代替正式入口。本批没有改变这些任务或 D2S 字段。Windows Release 构建、固定运行包和授权提交状态见 [构建](../../development/BUILD_AND_RUN.md)。下方阶段记录中的“固定首领未开放”不再适用于本节八项。

### 第二阶段基础接入与暂缓

2026-10-01：自然 Champion／Unique 出生接入既有原表强化，保留人口已经选定的类别，不在实例化时再次掷勇士概率。每个实例先用真实身份普通数值掷基础生命，再按 MonUMod 原生命百分比／勇士变体比例强化既有生命；不按强化后的整数区间重掷。所有本批主人初始化后，按 ownerSpawnKey 查找准确主人，直属随从继承原 bUnique=false 的生命、+3 等级、经验五倍及强壮／快速／元素／法力燃烧数值，保留再生且不继承主人事件。勇士同伴独立初始化，不当作金怪随从；普通 Party 直属单位受强化时使用 Minion 收益类别。公共承伤、命中、再生、家族 AI、经验与 TC 读取实例 enchantment，自然精英不再因没有初始化而缺有效属性。未知或未实现基型仍保持既有边界，未开放 SuperUnique／Boss。

Champion／Ghostly／Fanatic／Possessed／Berserk 的既有原生命、等级／经验、伤害／命中、防御、移动和冷伤变体共用选择入口；名称种子仍按 UMod1 保存。核对当前 MPQ 发现原难度字段是 ChampionDamageBonus／MonsterCEDamagePercent，修正旧缩写字段导致加成读成零的共用问题。依据 D2MOO MonsterUnique::sub_6FC6E940／sub_6FC6EC10／sub_6FC6EE90、UMod1/2/4/5/6/16/36–39 和 SpawnMinions；原数据仍运行时读取 MPQ，不把 reference 表当数据源。

特殊能力源码接入：精英池力量／祝福瞄准／狂热／审判／圣火／神圣冲击／神圣冰冻共用定义和执行器；客户端精确渲染及同等级来源仍有边界，见 [光环](../skills/PALADIN.md#通用光环逐项实施)。本轮单项接通自然多重射击、传送、伤害加深、闪电受击和火／冰死亡事件，不解除旧技能总开关。多重射击使用整数目标／两枚额外弹体和原 NoMultiShot／mummy 排除。传送补 Bighead 近战门槛、整数距离、完整战斗单位占位和 Prevent Heal；房间找位／随机流仍为适配，MonsterRegion::sub_6FC66260 的本地反编译分支存在疑点，未宣称精确移植。伤害加深按 MonsterUnique::CastAmplifyDamage／Skills::sub_6FD14770 以触发目标为中心，已计算伤害后、扣血前执行；补诅咒抵抗和 Attract 保护，不误套普通死灵回调的免疫折算。闪电强化按原真正 GH 延迟两帧、非 GH 硬／软命中即时回调及十帧间隔；不恢复毒伤等所有掉血均触发的旧逻辑。火／冰死亡延迟四帧，火爆炸原整数掷骰后除四、单次物理／火焰通道，原 0x581 玩家过滤和 0x0805 径向阻挡；冷新星速度取原 frostnova。直属随从不继承主人事件，数值强化保留。怪物基础元素／词缀完整单次命中快照、原事件调度／特殊弹体属性仍需收尾；仅接入口不等于第二阶段完成。

显示适配已接（用户授权 MPQ 推断）：当前 uniqueprefix／uniquesuffix 的 Name 经原 TBL 翻译，独立 nameSeed 以原 D2Seed 按 Prefix→Suffix 顺序抽取并以空格组合；不消耗战斗随机流、不每次绘制改变。勇士使用原 ChampionFormatX 和 Champion／champghostlyX／champfanaticX／champpossessedX／champberserkX 标签，金怪血条下显示原词缀文本，随从显示原 Minion。随机金怪／勇士动画缓存区分精英原调色映射；优先 MonStats2.Utrans 本难度值，否则从实际 palshift.dat 第三映射起选择不同于普通物种、透明索引正确且内容不重复的映射。缺有效映射保留物种原色，生命／死亡动画同色。姓名完整组合、Utrans 索引含义及精英随机选色并非已验证经典客户端算法，属于授权适配，不再因缺客户端代码关闭显示。幽灵透明度使用原图 alpha=160，精确原混色尚未核实。

公开规则补正：[Arreat Summit Monster Bonuses](https://classic.battle.net/diablo2exp/monsters/bonus.shtml) 说明勇士组 2–4 只，除一只允许在五种勇士中选择外，其余固定 Champion；生成计划以 championVariantAllowed 标志落实。附体拥有 100% 诅咒抵抗，不影响后续 Battle Cry／Cloak of Shadows 的非诅咒入口；随机金怪狂热只取同伴档物理伤害，玩家本人保持全额。单次直接通道结算、当前帧闪电分派和传送占位／射线已接；原完整模式／弹体创建快照、房间随机流及客户端动画／声音仍按授权采用当前可用执行器，不代表零售版逐帧认证。固定首领／Boss 未开放，仍属后续阶段。

本轮 D2MOO 收尾：基础元素、词缀和状态直接附伤先准备到同一 DamageRequest，统一减伤／反伤前快照／死亡／GH，不再物理致死后丢失后续直接元素通道。同元素的词缀与状态属性合并范围后掷伤；冷长度相加，并在生命结算前处理冷状态。远程来源元素／毒速率／冷长度按原 SrcDamage 比例。非 GH 闪电回调当帧生成，若处于弹体遍历则暂存并在该次遍历后追加，避免容器引用失效且不推迟到下一帧；GH 仍预约两帧后。传送按 MonsterSpawn::MonTeleport 补起手原 0x0C01 畅通射线和 0x3C01 落点，A1 出手再次检查占位。幽灵一击按 MonsterUnique::ApplyElementalDamage 的 SetStat 替换元素范围、保留其他已设元素并累加冷／毒长度，更新移到已接受攻击动作，不每次命中重选；原其他非 GH 模式转换及弹体创建后的独立属性更新仍待接。远程伤害仍在命中时准备而不是完整原创建快照，毒／法力副作用也仍有独立生命周期，不能把直接通道合并称为完整伤害结构等价。

D2MOO 依据范围：2026-10-01 只读查询 GitHub 上游 master 的完整仓库树，提交仍是本地 5596f5cb6c5251a0a07c6637d26458b06099d516，D2Client 路径数量为零。该项目提供随机精英生成／强化／事件、直属随从、经验与姓名种子及四种姓名片段抽取函数；没有完整随机姓名组合、精英选色及原客户端播放消费者。不能由此推断其他项目都不实现精英，也不能只凭片段函数自造顺序／选色规则。

交付与启动修复：当前 MPQ 审判公式有外层双引号，导致光环导入抛 Unsupported monster aura formula: "-min(ln34,150)"；求值前剥离这一 TXT 引号，已有 d2x_assets item cap 内容导入成功，未启动游戏。最新姓名／配色／标签／勇士组／词缀版本已完成 Windows Release 最终 EXE 链接并更新固定 dist/current；按用户授权以可用适配交付第二阶段人工验收，而非声称完整原引擎等价。没有编写／运行测试、启动游戏或提交 Git，原资源及用户存档保留，D2S v96 不变。无关精灵召唤不继续扩展。

### 第一阶段修复与交接

2026-10-01 收尾：第一阶段的已确认共用偏差与普通家族分支已按本地 D2MOO 逐入口补正，当前进入用户验收；本轮不进入第二阶段。这里的完成指阶段清单中的规则接入，不是完整原引擎、地图路径或所有变体的逐帧认证。

本批接入普通家族的共用移动生命周期，已接受的目标、坐标、绕行和退避动作期间不重选目标或重掷概率；死亡、真实 GH、冻结、目标失效及路径失败清理动作。骷髅法师使用目标停止距离，弓手区分 20% 绕行与带目标距离的半径动作；女盗贼弓手先尝试 aip8 走近，再判断远距必跑。僵尸移除永久追击及自加 10 帧等待，追击使用 RN／175%；Brute、腐乌鸦、法师、女盗贼、Bighead、小矮人与退避／绕行共用基础 75 加 AI 属性、ColdEffect、蛛网、词缀及限时状态速度，位移与动画读取同一结果。

普通家族起手共用真实体积的 MeleeRng+1，普通近战出伤增加原 +3；默认目标／Fallen 见尸使用无体积整数距离，Archer／SkeletonBow／SkeletonMage／Bighead 的有视线选敌阈值使用目标 FullUnitSize 整数距离。普通巫师直属尸体按 AiUtil::sub_6FCF1DC0 使用巫师 FullUnitSize 距离与 aip4² 阈值，并保留邻房遍历末个合格目标；不得混用暗金巫师的平方坐标回调。巫师按 Fn013 保留当前目标及有视线备选目标的两次独立射击机会。A1/A2 使用身份冷效果与 15–175 速率边界，动作进行中刷新速率时按旧／新百分比同步总时长、剩余时长和未出手时间，不重置已播放进度；原版怪物 SC/S 技能模式保留原动作时序。MonsterColdDivisor 移入 applyChill，冰封球等弹体不重复除。受击显示、AI 反应和真正 GH 分离；读取原 HitClass.txt／Weapons.hit class 并随武器攻击快照传递，按低于 1 HP、生命比例、随机门槛、冻结、不可打断、眩晕与 mGH 判定恢复，不再凭缺图补造 .12 秒 GH。Units 的 GH 专用公式为原 AnimData 速率乘 (50+120×FHR/(120+FHR))%，不受 ColdEffect／other_animrate 影响；此前 .24 秒 Brute GH 记录已被当前原 .48 秒结果取代。软命中不打断正在执行的移动，回击在动作完成后按家族规则选择，不把普通法师误套为弓手的强制射击。

腐乌鸦恢复自身 3／4 格游走及 0／-50 速度加值，冲锋／退避沿自身参数；自身游走按 AiTactics::WalkCloseToUnit 先选轴、再偏移、再两次符号掷骰，仅请求一次路径，不失败重掷四次。退避按原整数坐标符号方向一次请求，不用切线备选或逐次缩短距离。蜘蛛补接敌决策计数、受击追击记忆与低生命 4／8／12 动作；小矮人循环只在动作结束后决策时推进。吸血鬼保留阶段、有视线技能目标、<=30 概率边界及 Run/Velocity 退避属性，退避失败在同一次决策中继续原分支，不额外重掷。Fallen 首领喊叫同时给自身和直属随从命令，等待／移动中不重新喊叫，见尸逃离后的首击记忆保留。

巢从施法开始帧计时，AI 许可与技能出生分离：Fn043 原调用明确传两次 X，因此许可使用 (X,X+3) 的大小 2、0x3c01 掩码；不擅改该 reference 调用。通过许可才增加一次尝试并开始技能，开始时保存 MonStats.spawnx/spawny 的实际坐标，技能事件在保存坐标周围半径 3 的周界搜索实际出生点，不再使用自定义九点偏移。AI 失败使用 20–29 帧重试，技能出生失败不另改 AI 时钟／重试，达到上限后设置 NOTC；经验与 TC 资格分离。依据 MonsterSpawn::sub_6FC68350、Monsters::MONSTERS_GetSpawnMode_XY、SkillMonst::SrvSt49/SrvDo091 及 SpawnNormalMonster 的原注释分支。

人口 elite 随从现记录准确 ownerSpawnKey；Fallen／Shaman 命令与普通巫师复活按直属关系选择，不按组号扩大。已有主人强化的 Minion 可按强化等级进入原普通 TC，不再在读取 enchantment 前统一暂缓。自然精英尚未初始化，因此其随从仍明确暂缓收益，不以记录直属键宣称强化已完成。

Windows Release 已编译，未新增测试脚本、用例或专用程序。现有 debug 管道临时角色覆盖女盗贼 85% 走近及软命中后连续移动、法师 85% 接近后 A1、Brute .5 HP 软命中保持动作；本轮修正后 12 HP 命中 GH 为 .48 秒。巢新许可链在 (25.5,25.5) 正常孵出六只原幼鸟、达到 NOTC，击杀给 4 经验、无掉落且 deferred=null；旧 (32.5,25.5) 许可失败、计数 0，说明独立许可确实生效。地狱一级冰弹对 2625 HP 法师不产生 GH，冷时长约 1.5 秒；A1 中途由 100% 切为本身份 75%，总时长 .787692→1.050256 秒，保持进度。临时实例均已退出，未读写用户角色存档。包内最终启动及交付入口见 [构建与运行](../../development/BUILD_AND_RUN.md)。D2S v96、角色保存语义和原 MPQ 不变；本轮授权提交源码与文档，产物和 reference 不入 Git。

仍待人工验收与全局边界：未穷举所有变体／概率分支；房间目标获取、A*、动态拥挤、绕行路径类型 5/6 的几何适配及房间随机流消费仍非完整原引擎。巢周界搜索依据本地 SpawnNormalMonster 的反编译注释，复用当前单位占位检查，未宣称与零售版逐种子等价。当前用动作结束及受击反应标记承接本阶段 AI 3/19 和命令，不导入原引擎完整事件容器；上述共用世界边界不以第一阶段完成宣称解决。自然 Champion／Unique／SuperUnique／Boss 的战斗属性缺失及普通承伤拒绝路径仍未开放，属于第二至五阶段，不能通过删除级别限制套普通公式。自然精英强化、姓名／调色、完整直属收益及固定首领／专属 Boss 继续按下方顺序实施。

普通与高难度名单均为 55 个身份；`umon1–10` 也有 55 个身份，以 `foulcrow1/2` 替换巢的两个身份。`monsterImplementation` 已覆盖这三组名单及巢孵出的鸟，没有发现名单内仍用替身的普通身份。三难度基础数值解析的 `L-HP/L-DM/L-TH/L-AC`、`noRatio`、抗性、`El1–3` 动作与难度列，以及 AI 参数难度后缀已有共用入口；本轮未发现需要逐个变体复制数值公式的依据。缺口集中在动作执行和身份初始化。

### 修复前的共用偏差

| 优先顺序 | 当前源码与影响 | 原依据／后续修正 |
| --- | --- | --- |
| 1 | `skeleton_mage_ai.cpp:12` 把 `aip2` 当作行走预算；`skeleton_bow_ai.cpp:16/23` 把绕行 3 与 `aip4` 当作直线推进预算，`monster_ai.cpp:437` 按实际位移递减。会提前结束或改变接近动作 | Fn064 的 `WalkToTargetUnitWithSteps(aip2)` 是目标停止距离；Fn037 的近距 20% 分支是 `sub_6FCD0E80(...,3)` 绕行，远距是 `WalkInRadiusToTarget(aip4,aip5)`。分别接目标动作、绕行动作与带目标距离的半径坐标动作，不能统一成走若干格 |
| 2 | 共用 `Enemy.approach` 只接 CorruptRogue／CorruptLancer／Skeleton／Goatman／Wraith。Spider、Archer、Bighead 等仍在推进过程中调用决策；Fetish 的循环计数也可能按每帧递增。目标仍会在这些移动期间重新选择 | Fn026／035／004／030 和 MonsterMode 的 WL／RN 完成回调按动作完成触发下一次决策。扩展共用动作生命周期，保持每个家族的目标、模式、停止条件、阶段和独立概率 |
| 3 | 其他家族的 AI 近战起手仍是中心欧氏距离 `attackRange`，而命中已经用整数体积距离；非近战阈值大多仍来自 `delta.length()`。同一个 `MeleeRng+1` 又同时用于起手和命中，遗漏原普通怪物近战的额外范围加值 | AiUtil 的 `bCombat` 调用 `UNITS_IsInMeleeRange(...,0)`；MonsterMode 普通近战调用 `SUNITDMG_GetResultFlags(...,0,0)`，怪物的 `nRange=3`，Units 判定为 `MeleeRng+nRangeBonus+1`。分离起手／普通近战命中范围；按每个 reference 调用选择 NoUnitSize、FullUnitSize 或单位距离，不以欧氏距离统一替代 |
| 4 | 绕行、退避和多数家族仍走 `monsterWalkSpeed_` 加 `.42f` 冰冷倍率。Brute／BloodHawk 用乘法叠速度，Zombie 的旧分支为 `75×4/3=100`；原速度属性应加到基础 75。例如 Brute 最低生命档当前为 120%，原为 135%；Zombie 原追击为 175% | Fn007／005／003／030／035／064／004 的 `SetVelocity`，以及 Units 的移动百分比。统一用真实 Velocity 与加法速度属性，纳入本难度 ColdEffect、蛛网、词缀及状态。法师接近／退避原加值为 10／25，Bighead 退避为 50，Fetish 接近／退避为 50，Archer 走近／退避为 10／100。位移和动画共用同一结果 |
| 5 | `monster_melee.cpp:28` 令所有冰冷攻击时长翻倍，不使用身份及难度 ColdEffect，也没有按原攻击速率上下限处理。通用 `applyChill` 不使用已加载的 MonsterColdDivisor；只有冰封球的独立入口执行该除数 | `SUNITDMG_ApplyColdState` 把 ColdEffect 加入 velocitypercent／attackrate／other_animrate，并按原 MonsterColdDivisor 缩短怪物冰冷时间。当前 MPQ 普通／噩梦／地狱除数为 1／2／4；不同身份 ColdEffect 不同。移入共用状态入口，避免冰封球重复除，保留整数帧及最低一帧规则；SC 等模式按 Units 的对应分支处理 |
| 6 | `unit.cpp:186` 对每次非毒有效伤害都写入 hitFlash，缺少原软命中／GH 的分流；无 GH 资源还回退到 `.12f`。五类共用接近 AI 与刺猬会因此停步／取消动作，其他家族却可能继续攻击或行走 | SUnitDmg 的 `sub_6FCC1870` 区分冻结、纯毒、低于 1 HP、HitClass、生命比例、随机门槛和 MonStats2.mGH；ExecuteMissileDamage 再区分 GH 与 AI 状态 19，且检查不可打断状态。分离受击表现、实际恢复动作与 AI 反应，集中应用全部家族 |
| 7 | 自然 Champion／Unique／SuperUnique 没有 enchantment；`resolvedMonsterCombat` 与 `monsterAi_` 因级别直接返回空，boss 也明确排除。已有原形的固定首领仍回退旧生命／伤害和通用追击；更严重的是 `unit.cpp:88` 令其 stats.resolved=false，`resolveIncoming` 拒绝需要减免计算的常规承伤。这是静态可达路径，未实机复现 | MonsterUnique 的自然生成、固定 Mod1–3、高难度追加词缀和等级初始化。把已有祭坛强化能力接到自然生成／固定首领入口，保留真实家族 AI；boss 走其原等级及专属初始化，不能仅删除限制后套区域等级公式。开放精英前须先补其有效战斗属性 |
| 8 | `world/population.cpp::elite` 的随从没有记录原 ownerSpawnKey。自然精英随从虽读取普通基础数值，尚未应用主人强化／等级；经验按普通 Minion 分支，`monster_loot.cpp:58` 无条件暂缓 Minion 掉落，连有 enchantment 的随从也在使用其加值前返回 | MonsterUnique 的 SpawnMinions／UMod 初始化。补准确直属归属、继承数值、等级／经验与原 TC 选择；修复 Minion 掉落前置暂缓。不能按普通组号把全部同伴都强化 |
| 9 | 巢的间隔从成功出鸟时重新开始，失败统一等 20 帧；原函数在施法开始记帧，并有 20–29 帧重试。达到上限后的巢未设置原 NOTC，仍可能按非空原 TC 掉落 | Fn043 的帧戳、尝试计数、随机重试与上限后 UNITFLAG_NOTC。区分孵化 AI 的时刻、技能出鸟事件、失败及掉落资格；不能仅看 aip1/aip3 已读就认为行为完整 |

### 修复前的普通家族分支

| 家族／第一幕身份 | 除共用缺口外的具体剩余项 |
| --- | --- |
| CorruptArcher：`cr_archer1–4` | 当前仅在距离不超过 aip5 时尝试 aip8 走近；Fn035 在远距必跑前先尝试走近，当前 MPQ 四只的三难度 aip8 都为 12。补正确分支顺序、WithSteps 停止距离、退避跑动及受击强制射击；不是仅高难度才有走近 |
| SkeletonBow：`sk_archer1–3`；SkeletonMage：`skmage_fire1–2/skmage_ltng1–2` | 补原绕行／半径动作／目标动作；法师撤退失败射击、速度加值及受击反应。为后续毒系骨灰共用同一框架 |
| Zombie：`zombie1–3`；Brute：`brute1–3` | 原 RN／速度属性、一次目标动作、受击特殊 AI 状态与绕行；现 Zombie 永久追击布尔值和自加 10 帧未警觉等待仍是简化适配 |
| QuillRat：`quillrat1–4` | Fn014 的警觉范围外是 WalkCloseToUnit，自身附近游走；当前落入通用向敌人接近。补射击失败且退避失败后的游走，以及原受击／命令反应 |
| BloodHawk：`foulcrow1–2`（普通／精英池及巢子单位） | 当前把原 WalkCloseToUnit 随机游走改成绕目标走或接近；补原 3／4 半径游走、0／-50 加值、冲锋／退避的自身 aip4/5、结束和受击反应 |
| Arach：`arach1`；Fetish：`fetish1`；Vampire：`vampire5` | 蜘蛛缺原接敌计数周期、受击后追击记忆和低生命阶段的 4／8／12 动作区别；小矮人攻击循环须按原决策次数而非模拟帧；吸血鬼补动作期间阶段保持、受击强制分支及原冷却计数。当前 vampire5 三难度 aip5 均为 1，不因 Skill2/3 列存在而开放普通个体火墙／陨石 |
| Fallen：`fallen1–4`；FallenShaman：`fallenshaman1–4`；Nest：`crownest1–2` | 见尸／同伴命令的原直属关系、受击与移动生命周期、巫师尸体搜索距离／目标调度、巢帧戳与 NOTC。既有复活、火弹、喊叫、子鸟身份仍复用，不重写数值 |

### 超出普通怪物的实施顺序

1. 先修上述共用动作、距离、状态／速度和直属随从入口，再收尾普通家族分支。不得通过新增随机等待来掩盖调度问题。
2. 接自然勇士（含 Ghostly／Fanatic／Possessed／Berserk）、随机金怪及其直属随从：复用已存在的词缀数值／事件／光环能力，补生成时初始化、原随机姓名、精英调色与完整收益；祭坛实现只证明能力入口存在。
3. 接下表可复用现有基型的固定首领，统一读取 SuperUniques.Class／Mod1–3／MinGrp–MaxGrp／TC 及高难度追加规则，不逐只复制强化公式。
4. 开放骨灰的新基型，再接 Smith／Griswold／Countess／BloodRaven 的专属 AI 和技能／命令。现有任务死亡触发不等同这些怪物玩法已完成。
5. 接 Andariel 的 boss 数值／抗性、毒喷／毒弹、近战与阶段决策、动作／音频、原首杀及后续掉落和任务联动；牛场入口、牛王击杀资格及奖励一并按原任务规则核对。

| 固定首领 | 原 Class／固定 Mod1–3（0 省略） | 剩余实现 |
| --- | --- | --- |
| Bishibosh | `fallenshaman1`／8,9 | 原基型已有；补固定强化、直属随从及原技能／命令差异核对 |
| Bonebreak | `skeleton1`／5,8 | 原基型已有；补固定强化、随从和收益 |
| Coldcrow | `cr_archer1`／18 | 原基型已有；补固定强化并依赖 Archer 收尾 |
| Rakanishu | `fallen2`／17,6 | 原基型已有；补固定强化、闪电事件和随从 |
| Treehead WoodFist | `brute2`／5,6 | 原基型已有；补固定强化并依赖 Brute 收尾 |
| Pitspawn Fouldog | `bighead2`／7,18 | 原基型已有；补固定强化与近战／闪电／退避规则 |
| Corpsefire | `zombie1`／27 | 原基型已有；补固定强化并依赖 Zombie 收尾 |
| The Cow King | `hellbovine`／8,17 | 原基型已有；补固定强化、随从、牛场任务资格与入口 |
| Boneash | `skmage_pois3`／8,5,18 | 毒系真实基型／SkeletonMage／skmage1 与固定强化已接，普通毒伤冒烟通过，见专属首领节 |
| The Smith | `smith`／5 | 原形、Fn098 A1／低生命加速、固定强化与铁槌任务已接；原空火伤不补造 |
| Griswold | `griswold`／7 | 原形、Fn090、固定诅咒和原表 boss 属性已接；不代替凯恩囚笼救援 |
| The Countess | `corruptrogue3`／9 | SpecialState13、出生锚点／房间返回／MapAI 火墙、固定强化与 TC／任务箱已接；普通 corruptrogue3 不获得该特殊 AI |

另有 `bloodraven`（BloodRaven AI、`MissA1=raven1`、Nest／Quick Strike）与 `andariel`（Andariel AI、AndrialSpray／AndyPoisonBolt），当前均为敌对替身且专属 AI 未解析。血鸟还需原召唤／技能目标和死亡演出，安达利尔需独立 boss 链；对应本地 Fn059／034。Griswold／Smith 对应 Fn090／098，Countess 对应 SpecialState13，固定首领初始化依据 MonsterUnique。

`Flamespike the Crawler` 仍在当前 SuperUniques／Act 1 MonPreset，原 Class=`quillrat4`、固定 Mod=9,7；本轮仅确认表记录，尚未逐 DS1 核实当前 LoD 1.13c 是否实际放置，不纳入已证实的自然出场清单。`gargoyletrap`、`trap-horzmissile`、`trap-vertmissile` 也在 Act 1 MonPreset，分别依赖 GargoyleTrap／Trap-RightArrow／Trap-LeftArrow；列为环境陷阱阶段，须先核实实际 DS1、原可攻击／不可攻击标志与技能弹体，不能统一变成普通敌人。其他 place_group／任务条件放置仍按人口文档暂缓。

本轮计划限 Act 1。其他幕怪物、全球特殊事件、友方宠物／其他幕佣兵不因这次清单自动开放。所有数值、技能、概率与资源仍从原 MPQ 读取；本轮没有推导新的怪物参数。

## 移动动作与接近决策

Dark Spearwoman 的短步停顿来自 `updateMonsters` 每个 25 Hz 模拟帧重新调用 CorruptLancer 接近／跑动掷骰；普通难度当前 MPQ `cr_lancer1` 的 `aip1=60`、`aip3=9`，接近失败就进入 9 帧等待。此前仅对 CorruptRogue 保留追击，未覆盖长枪女盗贼、Goatman 与 Wraith 的同类调度问题。

当前源码在 `Enemy.approach` 保存一次已接受的移动动作，`monster_wander.*` 共用开始／结束入口，`monster_ai.cpp` 在动作期间只推进移动，不重选目标或重掷接近／跑动概率。各 AI 的原接近、攻击及等待参数保持独立；新动作已满足到达条件时直接结束，不制造额外一帧短步。目标失效／超出现有视距、受击／眩晕／冻结、路径失败、攻击／传送、死亡和复活均清理动作。状态仅在本局存在，D2S v96 与保存语义不变。

| AI 家族 | 原动作与到达条件 | 原速度百分比 |
| --- | --- | --- |
| CorruptLancer | 远距离必跑并记住冲锋后首击；近距离按自身 `aip1/4` 选择走／跑。跑的 StepNum 取原 `MeleeRng`，走取 3 | 走 75；跑 75 + 100 |
| CorruptRogue | 走调用带标志的目标动作，StepNum 为 1；跑的 StepNum 为 3 | 走 75；跑 75 + 自身 `aip4` |
| Skeleton（含牛）／Goatman | 各自接近概率成功后提交目标行走，StepNum 为 1 | 75 |
| Wraith | 保留 `WalkInRadiusToTarget(..., 12, 0)` 的整数坐标目标；走到该目标后重新决策 | 75 |

`WithSteps` 不是“走几格便停”。D2MOO `PATH_SetStepNum` 存储参数减 1，`PathMisc::sub_6FD5DB70` 按 `D2Common_10399` 的整数距离和双方体积判断目标到达；`MonsterMode` 在 WL／RN 动作结束后才转 Neutral 并触发 AI。原表 `cr_lancer1–3.MeleeRng=2`；内容层把该字段及 255 的原武器类哨兵转为类型化范围。当前第一阶段已扩展动作生命周期至普通家族，起手用 `MeleeRng+1`，普通出伤另加原 +3；移动及 WL／RN 动画共用 ColdEffect、蛛网、词缀和限时状态速度。几何路径仍为项目适配，不能以共享判定宣称原引擎逐帧等价。

依据当前 `assets/mpq2` 的 MonStats／MonStats2、固定本地 D2MOO `AiThink::Fn002/009/010/012_019/036`、`AiTactics::MoveToTarget/MoveInRadiusToTarget`、`AiUtil::sub_6FCF2110`、`Path::PATH_SetStepNum`、`PathMisc` 与 `Units::UNITS_IsInMeleeRange`。Diablerie 的通用 MonsterController 仅用于区分其简化协程模型，不用其随机延时替代原 AI 参数。参考代码与原表未纳入源码。

五类移动动作最初提交为 `3f230f1`；当前第一阶段运行包已包含该修订及普通家族扩展，编译和定向冒烟见上方交接。A*、目标获取、房间活跃与完整事件容器仍是项目适配，不因本批动作生命周期补齐宣称逐帧复刻。

## 怪物祭坛强化

2026-09-27：`content/monsters/monster_enchantment.*` 读取当前 MPQ 的 MonUMod／MonStats／MonStats2／MonType／MonLvl／DifficultyLevels／Skills／Missiles，`gameplay/monsters/monster_enchantments.cpp` 执行运行时行为。祭坛从玩家邻房范围选择最近、存活、正常站立或行走、普通级别且可死亡的敌对单位；排除 boss／primeevil、当前攻击／受击／冻结等不合格状态。无目标时按 ObjMode 消耗祭坛，不凭空创造敌人。

| 范围 | 已接通的效果 |
| --- | --- |
| 抽取与基础强化 | MonUMod 原 20% 勇士概率；普通／噩梦／地狱独特抽 1／2／3 个不同词缀；原权重、怪物类型排除和动作条件；现有生命掷骰的原百分比强化、补满、独特停再生、等级、经验及原 TC 档位／物品等级 |
| 数值词缀 | 特别强壮、特别快速、魔法抵抗、火焰／闪电／冰冷强化、法力燃烧、幽灵一击、皮肤硬化；按原顺序及两种免疫上限处理抗性，元素值读取强化后等级的 MonLvl；保留旧版法力燃烧定点倍率 |
| 动态词缀 | 诅咒成功攻击的原 75% 伤害加深；受击后 2 帧闪电及 10 帧间隔；死亡 4 帧后火焰爆炸／冰霜新星；多重射击遵守 NoMultiShot 与 mummy 特例；传送按原概率／低血量条件，在原 A1 作用帧移动并保留原治疗概率 |
| 灵气 | 力量、圣火、祝福瞄准、神圣冰冻、审判、狂热，以及达到原等级条件后的神圣冲击；原等级除数、范围、每 50 帧脉冲、攻击加成和状态；同种光环取更高等级；狂热区分持有者和同伴伤害加成 |
| 勇士变体 | Champion、Ghostly、Fanatic、Possessed、Berserk 的原生命、等级／经验、伤害、命中、防御、速度及冰冷加成 |
| 已有直属随从 | `MonStats.setboss`（及原 tentaclehead 例外）在普通 Party 生成时记录 ownerSpawnKey；祭坛初始化已有直属活体随从的生命、等级、经验，以及强壮／快速／元素／法力燃烧的 bUnique=false 数值；不按组号误强化普通同伴，不新增随从或给它们独特死亡词缀 |

强化身份、数值、光环与事件是通用类型数据，不依赖具体怪物 ID，保留其原外观／AI 或已授权敌对替身。此次入口为怪物祭坛；世界自然生成的精英、固定首领及 Boss 的完整强化仍须逐项接入，不能以祭坛实现替代它们的完成度。单位搜索、传送找位、AI 调度、视觉播放及随机流仍为项目适配。敌方伤害沿用当前玩家目标模型，佣兵／召唤单位的完整受击与光环覆盖未因此补齐；没有新增多人规则。原独特随机姓名／精英调色尚未接入，保留现有真实身份和级别标签。全部状态只在本局存在，D2S v96 不保存怪物或词缀。

依据本地固定 D2MOO `ObjMode::D2GAME_SHRINES_Monster`、`MonsterSpawn::sub_6FC69C00`、`MonsterUnique::sub_6FC6E940`／`sub_6FC6EC10`／`sub_6FC6EE90`／`D2GAME_SpawnMinions_6FC6F440`、各 UMod 初始化／事件，以及原 Skills／Missiles 伤害解析。没有把参考仓库或其资源纳入源码。

## 共用与专属边界

本地 `reference/d2moo` 的 `DATATBLS_CalculateMonsterStatsByLevel` 对所有怪物按同一公式和模式标志换算生命、护甲、经验、A1／A2／S1 伤害与命中；`MonsterMode.cpp` 同一路径还按 `El1–3` 与当前动作模式附加元素伤害。`AITHINK_GetAiTableRecord` 则按 `MonStats.AI` 选择 AI 函数；Skeleton、Zombie、Fallen、Brute 等是不同函数，同一 AI 类型的不同 `MonStats.Id` 共用行为代码，`aip1–8` 取各自难度的原值。故不能把所有行为合并为一种通用追击，也不应为每个变体复制一份 AI。

本项目共用解析放在 `content/monsters/monster_difficulty_combat.*`，生成、命中、受击、抗性、经验、掉落与存档走通用会话／模拟流程。`content/monsters/monster_animation.*` 按 token／动作／武器类读取 `AnimData.d2`，展示层的 `monster_audio.cpp` 按真实怪物身份读取 `MonSounds`／`Sounds`。`El1–3` 的动作、难度触发概率、伤害和持续时间也由该共用解析器读取；火、闪电、魔法、冰冷直接伤害、法力吸取及冰冷和毒素持续状态在 `gameplay/monsters/monster_element.cpp` 共用处理，其他元素类型待对应阶段。原技能／弹体与其施放动作仍需接入共用攻击流程；仅在 `gameplay/monsters/` 为不同 AI 家族实现决策和必要状态。怪物 ID 负责挑选原图形、声音、AI 家族及数据行；同一家族变体不重复实现数值或战斗规则。精英词缀、特殊死亡、复活、召唤和 Boss 阶段另设模块。

怪物配色的通用入口是 `resources/monster_palshift.cpp`：`MonStats.TransLvl` 选择 `palshift.dat` 中索引 `TransLvl + 2` 的 256 字节映射（从 0 起），依据 OpenDiablo2 `monster_stats_record.go` 的 `PaletteId` 说明。旧代码错误地加 3，普通沉沦魔因此使用下一变体的蓝绿色映射，活体、尸体及死亡图中的血迹都受影响；读取当前 MPQ 的 FA 调色表可见索引 2 是恒等映射，索引 3 改写了 58 个颜色索引。所有动作（含 DT／DD）现统一使用修正后的身份调色表；不再跳过死亡帧变色，也不再用额外 Blood 图覆盖尸体。此前变体截图和“原色”记录不能作为本次调色的验收依据；修正后的画面待用户查看。

合成器读取 COF 的逐层 Shadow／透明标志，仅用允许投影的原 DCC 像素生成半高斜向轮廓阴影；怪物还遵守 `MonStats2.Shadow`。当前 `CRDT1HS.cof` 的 S3（组件 10）为 Shadow=0、Transparent=1、DrawEffect=3；此类层按原 COF 顺序单独软加色，普通层显式切回 alpha 混合，调色映射保留索引 0 的透明含义。角色、怪物、NPC、物件共用 `Graphics::composite`／`sprite`，没有 Dark Hunter 图形特例。软加色采用本地 Diablerie `IO/D2Formats/COF.cs` → `Materials.SoftAdditive` 的参考实现（OneMinusDstColor, One）；它并不精确等价于原 1.13c 的 PL2 查色，其他 DrawEffect 暂未实现，不能声称全部 COF 混合已复刻。阴影参考 OpenDiablo2 `Animation.renderShadow`、`Composite` 与 Diablerie `COFRenderer`；投影比例和 alpha 同样为参考适配，环境变化未实现。黑边修正仅做原 COF／源码阅读，未运行画面验收。

| 范围 | 共用解析或执行 | 按怪物区别 |
| --- | --- | --- |
| 基础数值 | `MonStats`／`MonLvl` 的三难度等级、生命、A1／A2、命中、防御、暴击、再生、六抗；所有普通身份走同一解析器 | 每个 ID 只提供自己的 MPQ 数据行，不复制公式；缺列应保留已存在的字段 |
| 出生与死亡 | `Levels`／`MonStats` 群组、生命掷骰、经验、TC 掉落、通用存读档 | 等级区域、怪物身份、特殊召唤或死亡规则由原表指定 |
| 行为 | 目标、移动、攻击动作事件、伤害结算与 AI 参数读取共用 | 以 `MonStats.AI` 选择 Skeleton／Zombie／Brute 等 AI 家族，再用该 ID 的三难度 `aip1–8`；有技能／召唤／复活时接相应能力模块 |
| 形象与声音 | token、COF／DCC、`AnimData.d2`、`MonSounds`／`Sounds` 统一加载；`TransLvl` 选择运行时 MPQ `palshift.dat` 颜色映射 | 同一动作可因装备组件、调色级别、动作模式标志和原音效行呈现不同结果 |

`reference/d2moo` 对数值采用共享 `DATATBLS_CalculateMonsterStatsByLevel`，AI 由 `AITHINK_GetAiTableRecord` 分派到不同函数。因此逐个 ID 的工作是核对它是否需要尚未落地的 AI 分支、技能、动作或视觉参数，而不是重做一遍生命／抗性算法。当前仍未覆盖的通用元素攻击、怪物技能、精英修正等需要先补共用能力，再开放依赖它的 ID。

已启用原外观的 `fallen1–5`、`corruptrogue1–5`、`cr_lancer1–3`、`cr_archer1–4`、`sk_archer1–3`、`bighead1–4`、`hellbovine`、`skmage_fire1–2`、`skmage_ltng1–2`、`fetish1`、`vampire5`、`fallenshaman1–4`、`crownest1–2`、`foulcrow1–2`、`arach1`、`brute1–5`、`skeleton1–5`、`zombie1–5`、`goatman1–5`、`quillrat1–5` 与 `wraith1–3` 已核对共用路径：普通级别的生命、命中、防御、伤害、抗性、元素由 `GameSession::resolvedMonsterCombat` 按真实 ID 提供，攻击与死亡结算不按 ID 分支；动画按原 token／模式／调色读 MPQ，声音按各自 `MonSound` 读 MPQ。声音行存在性现对所有启用的敌对外观统一校验，原行未列出的具体音效留空。

## 分步边界

1. **普通基础数值：已完成。** 最初由 `content/monsters/monster_combat.*` 解析普通难度生命与 A1；当前已由共用三难度解析接管普通怪物。`Simulation` 按真实身份在生成时掷生命、攻击时读取当前模式的原伤害；缺 A1 的远程或特殊记录仍保留其余数值。精英／首领仍沿用旧适配值。敌人最大生命随存档保存并校验，血条使用该实例的最大生命。
2. **单个普通怪物外观：已完成。** `brute1` 使用 MPQ `YE` 的 NU/WL/A1/DT COF/DCC；原 `MonStats2` 只有躯干组件，四种动作资源均在本机 MPQ 中。该身份从沉沦魔类型替身切换为原外观，仍沿用公共近战决策。`brute2` 等不同变体继续为替身。
3. **骷髅 AI 决策：已完成基础分支。** `content/monsters/monster_ai_data.*` 从挂载 MPQ 的 `MonStats.AI/aip1–aip8` 按难度读取参数；`gameplay/monsters/skeleton_ai.*` 对原 `AI=Skeleton` 的普通骷髅使用接近几率、停顿帧数和近身攻击几率。追击和停顿状态写入 v24 存档并校验。逐帧 AI 调度仍待后续。
4. **Brute 受伤加速：已完成基础分支。** `gameplay/monsters/brute_ai.*` 按本地 D2MOO 的 `AITHINK_Fn007_Brute`，从当前生命百分比计算原 AI 的 40% 下限和最高 60% 行走速度加成；只对 MPQ `AI=Brute` 且已实现外观的普通怪物应用。近身盘绕与停顿仍待完成。
5. **A1 动作与出伤帧：已完成基础分支。** `resources/anim_data.*` 从挂载 MPQ 解码 `AnimData.d2`；`content/monsters/monster_animation.*` 按已实现外观的 token／武器类选取原 A1 速度、帧数和事件 1。`gameplay/monsters/monster_melee.cpp` 在原事件帧重新检查玩家距离／通路并结算命中；画面按相同动作时长推进，v25 存档保存动作剩余与待出伤时间。原表无有效记录时明确沿用旧即时攻击适配。
6. **Brute A2：已完成基础分支。** `brute1` 的第二攻击模式由运行时 MPQ 的 A2 COF/DCC 与 `AnimData.d2` 驱动；伤害／命中读取 `MonStats.A2MinD/A2MaxD/A2TH` 与 `MonLvl`，A1／A2 选择使用原 `AI=Brute` 的 `aip4`。攻击模式写入 v26 存档并在恢复时验证。只在 A2 资源、数值和命中资料齐全时选择该动作。
7. **普通骷髅 A2：已完成基础分支。** `skeleton1` 的 A2 使用运行时 MPQ 的 `SKA21HS` COF/DCC、`AnimData.d2` 动作事件及 `MonStats` A2 伤害／命中；`AI=Skeleton` 的 `aip4` 在攻击决策通过后选择 A1／A2。v27 存档保存模式并按当前资源验证。该分支只覆盖普通级别的已实现外观；数值现由三难度共用解析提供。
8. **普通僵尸 A2：已完成基础分支。** `zombie1` 的 A2 使用 MPQ 原 `ZMA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats` A2 伤害／命中；`AI=Zombie` 的 `aip4` 决定近身攻击模式。v28 存档保存模式并按当前资源验证；接近行为见第 10 项。
9. **调试定向刷怪与扣血：已完成。** 命名管道按当前 MPQ `MonStats.Id` 和可走坐标生成单个敌对怪物，沿用正式生命掷骰；扣血、死亡经验与掉落走正式结算。存档使用 Debug 来源区分自然生成，v29 恢复校验身份、位置与唯一键。未实现的敌对类型仍显式标记沉沦魔替身。
10. **僵尸接近／游走：已完成基础分支。** 普通 `AI=Zombie` 按 MPQ `aip1` 接近概率和 `aip2` 警觉距离开始追击，追击采用原规则指定的 100% 速度；未警觉时在自身附近三格内选择可走位置游走。追击和短暂决策等待保存于 v30；邪恶洞窟定向生成后，追击与远处游走、存读档均已冒烟。原 AI 状态 3／19、埋骨之地强制追击与逐帧行动调度仍待实现。
11. **普通群组等级与数值：已修正。** 普通怪物从 MPQ `PartyMin/Max` 生成的随从使用 Normal，保留原数量但不再误用精英 Minion 的通用 100 HP／6 点伤害；Minion 留给精英随从。读取 MPQ A1 数值时不因 `rangedtype` 一概跳过有 A1 字段的类型。血腥荒地种子 210 的 24 个 `fallen1` 现为 1–4 HP，`quillrat1` 为 2 HP；普通难度 `Levels.MonDen=520` 与两个高难度在该区域恰好相同，不能凭难度名称擅改数量。v31 存档按新等级与生命区间校验；噩梦／地狱的普通怪物基础战斗数值现由共用解析提供；精英修正仍待后续。
12. **沉沦魔追击／游走／攻击概率：已完成基础分支。** 普通 `AI=Fallen` 按 MPQ `aip2` 的距离门槛追击目标；门槛外参照原规则按 30% 几率在三格内游走，否则等待 10 帧。近身攻击按 `aip3` 选择，失败后按第 27 项的条件喊叫或短暂等待。普通难度 `fallen1` 的门槛 10、攻击概率 50% 均来自当前 MPQ。游走与等待共用独立怪物 AI 组件，等待／追击状态进入 v32 存档；见尸逃跑与 S2 喊叫分别见第 26、27 项。
13. **普通沉沦魔 A2：已完成基础分支。** `fallen1` 从 MPQ 原 `FAA2HTH` COF/DCC、`AnimData.d2` 动作事件和 `MonStats.A2MinD/A2MaxD/A2TH` 取得第二近战动作与数值；`AI=Fallen` 的 `aip4` 决定 A1／A2。原表 A2 为 15 帧、0.6 秒，命中事件在 0.32 秒；v33 存档可恢复出伤阶段，独立运行目录已截图。仅在原资源与数值齐全时启用。
14. **Brute 近身攻击机会：已完成基础分支。** `AI=Brute` 在近身时先按 MPQ `aip3` 掷攻击机会；掷骰失败则等待 15 帧，等待状态沿用现有存档字段。原 AI 在失败后还会再次掷骰，并可能执行侧移；侧移所需移动参数尚未核实，暂缓接入。`brute1` 普通难度原表攻击机会为 100%，因此这一步不会改变其近身攻击频率；其他 Brute 变体仍使用类型替身。v34 改变规则指纹，不读取旧档。
15. **`brute1` 普通级别三难度收尾：已接源码。** 原 `YE` NU/WL/A1/A2/GH/DT/DD 动作和 `AnimData.d2` 时序，`MonSounds` 与 `Sounds` 的脚步、待机、攻击、受击、死亡音频已按此身份接入；三难度生命、A1/A2 伤害／命中、防御、暴击、生命再生与抗性由上述共用解析读取。地狱原表物理抗性 50%、冰冷抗性 100%；没有原技能或元素攻击。Brute AI 在三难度的 `aip3` 均为 100%，因此失败后的绕行／停顿不会在此 ID 触发；A1/A2 按 `aip4`。自然生成、经验、掉落走既有身份链。独立打包与冒烟结果见开发基线。
16. **`zombie1` 普通级别三难度收尾：已完成。** 原 `ZM` NU/WL/A1/A2/GH/DT/DD 动作由 `MonStats2` 的模式标志和 `AnimData.d2` 加载，攻击、受击、死亡、脚步和待机音频从该身份的 `MonSounds`／`Sounds` 读取。三难度基础数值、50% 普通难度毒抗、经验与掉落走共享流程。原 Zombie AI 的 `aip1` 接近概率、`aip2` 警觉距离、`aip4` A1/A2 选择沿用现有实现；受击后进入追击，`Levels.LevelName=Burial Grounds` 的区域也强制追击。该 ID 没有原技能、元素攻击或弹体。地图寻路、目标调度与命中反应阈值仍是项目适配，不能声称原版逐帧一致。整包和现场见开发基线。
17. **`skeleton1` 普通级别三难度收尾：已完成。** 原 `SK` NU/WL/A1/A2/GH/DT/DD 动作由 `MonStats2`、COF/DCC 和 `AnimData.d2` 驱动；`MonSounds` 对该 ID 只列出受击、死亡和脚步，没有攻击或待机音频，因此不补造声音。普通 Skeleton AI 的接近概率、停顿帧数、近身攻击概率和 A1/A2 选择分别使用 `aip1–4`；掷骰复用怪物共享随机流。三难度基础数值、经验与掉落走公共身份链。原 `Skill1=SkeletonRaise` 与 `MonStats2.ResurrectSkill` 描述被其他单位复活时的动作，不由普通 Skeleton AI 主动施放；复活者与尸体交互留给相应怪物阶段。整包和现场见开发基线。
18. **`zombie2` 普通级别变体：已接源码。** 与 `zombie1` 共用 `AI=Zombie`、`ZM` 原动作和原音效行，三难度各自读取自己的生命、攻击、抗性、AI 参数、经验和 TC；原行没有主动技能与元素攻击。`MonStats.TransLvl=1` 经 `ZM/COF/palshift.dat` 的第 4 组颜色映射呈现，`zombie1` 的 0 使用第 3 组；按从 0 起的 `TransLvl + 2` 选表。调色加载为所有已实现怪物的共享路径，`brute1` 的 `TransLvl=4` 同时按原表呈现。整包和现场见开发基线。
19. **`zombie3` 普通级别变体：已完成。** 与 `zombie1`／`zombie2` 共用 Zombie AI、原 `ZM` 七动作、音效和攻击／死亡链；`MonStats2` 的相关模式标志与组件也相同。三难度数值、AI 参数、经验和 TC 仍按 `zombie3` 自身 MPQ 行读取，`TransLvl=2` 从同一 `palshift.dat` 选第 5 组颜色映射。原行没有主动技能、元素攻击或弹体；整包和现场见开发基线。
20. **`zombie4` 普通级别变体：已完成。** 与前几种僵尸共用 Zombie AI、七动作、原声音及基础数值／死亡流程；`TransLvl=3` 选择同一 MPQ `palshift.dat` 的第 6 组颜色映射。它的 `El1Mode=A1`、`El1Type=cold` 在三难度分别从自身 MPQ 行取得触发概率、`MonLvl` 缩放的冰冷伤害及帧数；角色抗性加当前难度 `DifficultyLevels.ResistPenalty` 减免伤害和冰冷持续时间。冰冷状态按本地 D2MOO 的角色速度／攻击速度减半规则改变移动、普攻时长和画面速度，并以 v40 存档。该 ID 无主动技能或弹体。整包、截图、冰冷命中与存读档现场见开发基线。
21. **`zombie5` 普通级别变体：已完成。** 继续复用 Zombie AI、七动作、原声音和共享三难度数值／经验／TC；`TransLvl=4` 从运行时 MPQ `palshift.dat` 选择第 7 组颜色映射。原 `El1Mode=A1`／`El1Type=pois` 由通用元素解析器取得三难度概率、缩放伤害和持续帧数；毒素处理参照本地 D2MOO 将伤害乘 10 写成每帧生命回复负值，持续时间乘 2，相同目标只由更强毒素刷新；角色毒抗和难度惩罚影响毒伤，毒素最低留 1 HP。状态与伤害率存入 v41。原行无主动技能或弹体。完整六 MPQ 目录、原色和中毒截图、毒伤存读档、死亡经验和 NoDrop 现场见开发基线。
22. **`skeleton2` 普通级别变体：已完成。** 原 `MonStats2` 动作和组件与 `skeleton1` 一致，复用 Skeleton AI、`SK` 七动作、原声音与共享三难度战斗／死亡链。原表 `SkeletonRaise` 仍只描述被其他单位复活，普通骷髅不主动施放；该 ID 无元素或弹体。它的等级、伤害、抗性和 `aip1–4` 取自身 MPQ 行，`TransLvl=1` 使用原 `palshift.dat`。完整六 MPQ 目录截图、v42 存读档及击杀经验现场见开发基线。
23. **`skeleton3` 普通级别变体：已完成。** 与 `skeleton1–2` 共用 Skeleton AI、七动作、原音效、三难度战斗和经验／TC 流程；`MonStats2` 除 ID 外与 `skeleton1` 相同，原表也没有元素或弹体，`SkeletonRaise` 仍是被复活标记。自己的 `aip1–4`、数值、抗性按 MPQ 行读取，`TransLvl=2` 使用对应的原调色映射。六 MPQ 运行目录截图、v43 存读档和击杀经验见开发基线。
24. **`skeleton4` 普通级别变体：已完成。** 沿用 Skeleton AI、原 `SK` 七动作和 `MonSounds`，`MonStats2` 与前几只除 ID 外一致；`TransLvl=3` 使用对应的原调色映射。原 `El1Mode=A1`／`El1Type=fire` 在三难度的概率、缩放伤害均由共享解析器读取，经同一攻击命中及角色火抗流程执行；该元素没有持续时间或弹体。`SkeletonRaise` 仍是被复活标记。完整六 MPQ 目录截图、v44 存读档、攻击扣血与击杀经验见开发基线；本次现场未单独量出火焰份额。
25. **`skeleton5` 普通级别变体：已完成。** 继续复用 Skeleton AI、`SK` 七动作、原声音和共享三难度战斗／死亡链；`TransLvl=4` 选自身 MPQ 调色。原 `MonStats2.S2` 与前几只不同，但主动技能栏仅有被复活标记 `SkeletonRaise`，没有 S2 施法或弹体。`El1Mode=A2`／`El1Type=ltng` 的三难度概率及伤害来自原表，经共享元素路径在 A2 命中时附加闪电；普通难度 `aip4=33` 控制 A1/A2 选择。完整六 MPQ 截图、A2 命中、v45 存读档与经验现场见开发基线。
26. **Fallen 见尸逃跑：已完成基础分支。** 本地 D2MOO 的 `AITHINK_Fn006_Fallen` 在附近 15 格看见死亡动作中的尸体时，调用向远离目标 12 格逃离的共用动作并将速度提高 50%。当前会话用原 `AnimData.d2` 死亡时长识别已启用外观的尸体，按地图可走路径寻找同向逃离点；区域房间的“最近四具尸体”名单与原引擎调度仍是项目适配。逃离状态及路线写入 v46 存档，管道可读 `aiEscaping`。两只 `fallen1` 的完整六 MPQ 现场确认击杀后另一只逃跑、推进移动和恢复路线，截图见开发基线。
27. **Fallen 同伴命令与 S2 喊叫：已完成基础分支。** `AI=Fallen` 使用自身难度的 `aip1` 决定同组首领是否喊叫并命令随从追击；未接命令的个体在近战攻击几率失败后也能按原 30% 分支喊叫，接命令的个体攻击失败则停顿五帧。S2 动作的 `MonStats2.mS2`、COF／DCC、`AnimData.d2` 时长和 `MonSounds.Skill2` 均读取当前 MPQ，声音事件只对开始喊叫发送。命令与动作计时保存到 v47；完整六 MPQ 现场确认 `fallen1` 喊叫帧、动作结束后追击和存读档恢复。区域目标激活、随从归属和随机流仍是项目适配，自然组的首领若为 Shaman 则不会由 Fallen 分支发命令。
28. **`fallen2`（Carver）普通级别变体：已完成。** `MonStats2` 除 ID 外与 `fallen1` 一致，复用原 `FA` 八动作、`MonSounds.fallen`、共享 Fallen AI、三难度生命／伤害／抗性／经验／掉落；`aip1–4` 各难度仍按 `fallen2` 自身 MPQ 行读取。`TransLvl=1` 选择原 `FA/COF/palshift.dat` 颜色映射。原 `El1Mode=A1`／`El1Type=cold` 在普通难度无触发概率，噩梦与地狱概率分别为 10% 和 20%，由通用元素路径读取；没有主动技能或弹体。完整六 MPQ 包在黑色荒地和邪恶洞窟启动，截图确认开阔场景原色，v48 喊叫存读档与击杀获得 42 经验；现场未单独测高难度冰冷命中。
29. **`fallen3`（Devilkin）普通级别变体：已完成。** `MonStats2` 除 ID 外与 `fallen2` 一致，复用 `FA` 八动作、`MonSounds.fallen`、共享 Fallen AI、三难度生命／伤害／抗性／经验／掉落；`aip1–4` 用自己的 MPQ 行。`TransLvl=2` 使用原调色映射。`El1Mode=A1`／`El1Type=ltng` 普通难度无触发概率，噩梦／地狱均从原表读取 10%／20% 概率并走共享元素攻击；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v49 喊叫存读档与击杀获得 46 经验已现场确认；高难度闪电命中未单独测。
30. **`fallen4`（Dark One）普通级别变体：已完成。** `MonStats2` 除 ID 外与前几只 Fallen 一致，复用 `FA` 八动作、`MonSounds.fallen`、共享 Fallen AI 及三难度战斗／死亡链；`aip1–4` 使用自身 MPQ 行。`TransLvl=3` 使用对应的原调色映射。`El1Mode=A1`／`El1Type=pois` 普通难度无触发概率，噩梦／地狱的 10%／20% 概率进入共享毒素处理；没有主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v50 喊叫存读档与击杀获得 10 经验已现场确认；高难度毒素命中未单独测。
31. **`fallen5`（Warped Fallen）普通级别变体：已完成。** `MonStats2` 除 ID 外与前几只 Fallen 一致，复用 `FA` 八动作、`MonSounds.fallen`、共享 Fallen AI 和三难度战斗／死亡链；`aip1–4` 按自己的 MPQ 行取值。`TransLvl=4` 选择对应的原调色映射。`El1Mode=A1`／`El1Type=cold` 普通难度没有触发概率，噩梦／地狱均为原表 10%，进入共享冰冷处理；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v51 喊叫存读档与击杀获得 23 经验已现场确认；高难度冰冷命中未单独测。
32. **Brute 绕行及 `brute2` 普通级别变体：已完成基础分支。** 本地 D2MOO 的 `AITHINK_Fn007_Brute` 在 `aip3` 首次攻击掷骰失败后，再用相同概率掷骰；第二次成功调用 `sub_6FCD0E80(..., 4, 0)`。该函数随机选择路径类型 5／6（顺／逆时针绕目标）与距离 4，而不是单纯横移；本项目以地图可走路径到目标周围四格实现绕行，原路径算法及逐帧调度仍属适配。第二次也失败则等待 15 帧。此分支放在共用 Brute AI，`brute1` 的 `aip3=100` 不触发，`brute2` 的普通／噩梦 `aip3=75`、地狱 80 均来自各自 MPQ 行。`brute2` 的 `YE` 原动作、`MonSounds.brute`、`TransLvl=0`、三难度数值、经验和掉落复用共享管线；原行无主动技能、元素或弹体。完整六 MPQ 包截图、绕行位移、v52 存读档及击杀经验现场见开发基线。
33. **`brute3`（Yeti）普通级别变体：已完成。** `MonStats2` 除 ID 外与 `brute2` 一致，复用 `YE` 七动作、`MonSounds.brute`、共享 Brute AI 与三难度战斗／死亡链；自身 `aip3` 普通／噩梦为 80、地狱为 85，失败后的绕行分支走第 32 项的共享实现，`aip4` 选 A1／A2。`TransLvl=1` 使用原调色；没有主动技能、元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v53 存读档与击杀获得 47 经验已现场确认；不逐变体重复量测绕行数值。
34. **`brute4`（Crusher）普通级别变体：已完成。** `MonStats2` 除 ID 外与 `brute3` 一致，复用 `YE` 七动作、`MonSounds.brute`、共享 Brute AI 和三难度战斗／死亡链；`aip3` 普通／噩梦为 85、地狱为 90，`aip4` 选择 A1／A2。`TransLvl=2` 选原红黑调色；没有主动技能、元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v54 存读档与击杀获得 6 经验已现场确认；不逐变体重复量测绕行数值。
35. **`brute5`（Wailing Beast）普通级别变体：已完成。** `MonStats2` 除 ID 外与前几只 Brute 一致，复用 `YE` 七动作、`MonSounds.brute`、共享 Brute AI 及三难度战斗／死亡链；`aip3` 普通／噩梦为 90、地狱为 95，`aip4` 选择 A1／A2。`TransLvl=3` 使用原调色；`El1Mode=A1`／`El1Type=fire` 普通难度没有触发概率，噩梦／地狱均为原表 15%，进入共享火焰攻击。没有主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v55 存读档与击杀获得 9 经验已现场确认；高难度火焰命中未单独测。
36. **`corruptrogue1`（Dark Hunter）专属 AI：基础分支已接，2026-09-27 修订未实机验收。** D2MOO `AITHINK_Fn010_CorruptRogue` 在 20−3×难度格内按 `aip1/aip2` 决定接近或停顿，`aip5` 选步行／跑步，近战按 `aip3` 攻击。`RunToTargetUnitWithSteps(...,3)` 经 `PATH_SetStepNum` 和 `PathMisc` 转为目标停止距离，不是移动三格后重掷；追击现在维持到近战、目标离开、受击／冻结或路径失败。可穿弹体但挡行走的物体不会再被当作直走路径，寻路使用行走掩码。速度依据 `Monster.cpp` 初始 `velocitypercent=75`、`MonsterMode.cpp` 的 AI 速度属性列表和 `Units.cpp::UNITS_UpdateRunWalkAnimRateAndVelocity`：基础位置速度始终取 `Velocity<<8`，再乘 `(75+aip4+ColdEffect)/100`（冰冷时才加 ColdEffect，最低 25），不取 Run 乘 `(1+aip4/100)`。当前原表 Velocity=5、aip4=100；未冰冷时本项目坐标约 13.67 子格/秒。`Run` 用于动画变体比例；`MonsterTbls` 对索引小于 410 的原版怪物取基础 WL 动画速率的一半作为 RN 基率，变体再按 Velocity／Run 缩放。内容层导入这些速率，运动状态提供百分比，表现与位移共用冰冷换算。CorruptLancer／CorruptArcher 的已实现跑步入口同用原引擎 +100 速度属性；其他 AI 的绕行、逃离和加速分支仍保留既有适配。角色／怪物实时运动不进 D2S，路径、碰撞几何和动画相位仍非原版逐帧等价。原 AI 概率和三难度参数、NU/WL/RN/A1/GH/DT/DD 原图、命中、声音、经验和掉落均按真实身份读取 MPQ；当前无主动技能或元素弹体。此前 v56 的截图／存读档记录仅代表旧实现，不能证明本次修订。
37. **`corruptrogue2`（Vile Hunter）普通级别变体：已完成。** `MonStats2` 除 ID 外与 `corruptrogue1` 一致，复用 `CR` 的 NU/WL/RN/A1/GH/DT/DD 原动作、`MonSounds.corruptrogue`、共享接近／停顿／跑步／A1 决策与三难度战斗／死亡链。自身 `aip1–5` 和战斗数值按 MPQ 行读取；`TransLvl=1` 使用原蓝色调色。没有主动技能、元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v57 存读档与击杀获得 54 经验已现场确认；跑步数值已在 `corruptrogue1` 现场核对，不逐变体重复。
38. **`corruptrogue3`（Dark Stalker）普通级别变体：已完成。** `MonStats2` 除 ID 外与前两只近战 Rogue 一致，复用 `CR` 七动作、`MonSounds.corruptrogue`、共享接近／停顿／跑步／A1 决策和三难度战斗／死亡链；`aip1–5` 使用自身 MPQ 行，`TransLvl=2` 取原调色。原表 `Skill1=CountessFirewall`，但 D2MOO 的普通 `AITHINK_Fn010_CorruptRogue` 不读取该技能槽或进入施法模式，因此普通个体不主动施放；没有元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v58 存读档与击杀获得 53 经验已现场确认。
39. **`corruptrogue4`（Black Rogue）普通级别变体：已完成。** `MonStats2` 除 ID 外与前几只近战 Rogue 一致，复用 `CR` 七动作、`MonSounds.corruptrogue`、共享接近／停顿／跑步／A1 决策和三难度战斗／死亡链；`aip1–5` 取自身 MPQ 行，`TransLvl=3` 使用原红色调色。原表无主动技能、元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v59 存读档与击杀获得 30 经验已现场确认；跑步数值已在 `corruptrogue1` 现场核对，不逐变体重复。
40. **`corruptrogue5`（Flesh Hunter）普通级别变体：已完成。** `MonStats2` 除 ID 外与前几只近战 Rogue 一致，复用 `CR` 七动作、`MonSounds.corruptrogue`、共享接近／停顿／跑步／A1 决策与三难度战斗／死亡链；`aip1–5` 取自身 MPQ 行，`TransLvl=4` 使用原浅色调色。没有主动技能、元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v60 存读档与击杀获得 6 经验已现场确认；跑步数值已在 `corruptrogue1` 现场核对，不逐变体重复。
41. **`goatman1`（Moon Clan）普通级别与共享动作武器类：已完成基础分支。** 本地 D2MOO 的 `AITHINK_Fn012_019_Goatman_Swarm` 在非近战时按 `aip1` 接近、近身按 `aip3` 攻击，未通过则按 `aip2` 停顿；独立 `goatman_ai.*` 读取该 ID 的三难度参数。原表没有主动技能、元素攻击或弹体，A1 命中、战斗数值、死亡经验和掉落走共享路径。`MonStats2.BaseW=2hs` 供 NU/WL/A1/GH 动作，DT/DD 原 COF 使用 `hth`；`content/monsters/monster_animation.*` 现在逐动作从运行时 MPQ 选 `BaseW` 或已有徒手 COF，并让此前 25 种原形也使用相同加载路径。右手原组件从 `RHv` 选择首个变体，本 ID 为 `btx`。完整六 MPQ 包在邪恶洞窟启动，确认此前原形资源同时加载、原形截图、v61 存读档与击杀获得 54 经验。行为仍使用项目通用目标调度与路径适配。
42. **`goatman2`（Night Clan）普通级别变体：已完成。** 与 `goatman1` 共用 `GM` 的 NU/WL/A1/GH/DT/DD 动作、原 `MonSounds.goatman`、独立 Goatman AI 决策及三难度战斗／死亡链；`TransLvl=1` 读取原调色。自身 MPQ 行的 `El1Mode=A1`／`El1Type=ltng` 在普通难度无概率，噩梦／地狱分别为 15%／20%，走共用元素命中路径；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v62 存读档与击杀获得 72 经验已现场确认；高难度闪电命中未单独量测。
43. **`goatman3`（Blood Clan）普通级别变体：已完成。** 复用 `GM` 六动作、`MonSounds.goatman`、共享 Goatman AI、三难度战斗与死亡流程，`TransLvl=2` 读取原调色。自身 MPQ 行的 `El1Mode=A1`／`El1Type=fire` 在普通难度无概率，噩梦／地狱分别为 20%／25%，进入通用元素攻击；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v63 存读档与击杀获得 71 经验已现场确认；高难度火焰命中未单独量测。
44. **`goatman4`（Hell Clan）普通级别变体：已完成。** 复用 `GM` 六动作、`MonSounds.goatman`、共享 Goatman AI、三难度战斗和死亡链，`TransLvl=3` 读取原调色。自身 MPQ 行的 `El1Mode=A1`／`El1Type=fire` 在普通难度无概率，噩梦／地狱分别为 25%／30%，进入通用元素攻击；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v64 存读档与击杀获得 4 经验已现场确认；该经验受等级差缩减，高难度火焰命中未单独量测。
45. **`goatman5`（Death Clan）普通级别变体：已完成。** 复用 `GM` 六动作、`MonSounds.goatman`、共享 Goatman AI 及三难度战斗／死亡链，`TransLvl=4` 读取原调色；自身 MPQ 行没有主动技能、元素攻击或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v65 存读档与击杀获得 16 经验已现场确认。
46. **`quillrat1`（Quill Rat）远程家族与敌方弹体：已完成基础分支。** 血腥荒地首批真实远程普通怪物。`MonStats.AI=QuillRat` 与本地 D2MOO 的 `AITHINK_Fn014_QuillRat` 指定近身 A1、激活距离 `aip1`、A2 射击概率 `aip2`、后撤距离 `aip4`；专属决策放在 `quill_rat_ai.*`，通用房间激活／寻路仍是项目适配。原 `SI` 的 NU/WL/A1/A2/GH/DT/DD、`MonSounds.quillrat`、`AnimData.d2` A2 事件 2 都从 MPQ 读取；A2 的 13 帧、0.52 秒、0.2 秒发射点与 `MissA2=spike1` 对应。敌方弹体使用运行时 `Missiles.txt` 的 ID、速度、射程、原 DCC、基础伤害及源伤害比例，在命中玩家时复用 A2 命中、暴击、角色格挡、元素攻击和受击流程；发射者身份与飞行状态写入 v66 存档并校验。原 A2 毒素在普通难度无概率，噩梦／地狱 15%／20% 由通用元素路径处理，未单独量测。完整六 MPQ 包在邪恶洞窟启动，原形与弹体截图、飞行中存读档、生命 55→53、后撤位移和击杀经验 21 已现场确认。受击后按原 GH 动作结束触发一次 A2 回击，待回击状态随 v66 保存；当前攻击动作完成后额外等待 15 帧，避免下一帧连续重掷射击概率；原引擎逐帧行动调度与逃跑路径算法仍属项目适配。
47. **`quillrat2`（Spike Fiend）普通级别变体：已完成。** 复用 `SI` 七动作、`MonSounds.quillrat`、共享 Quill Rat AI／A2 事件 2 发射、敌方弹体命中与三难度战斗／死亡链；`TransLvl=1` 选择原蓝色调色。该 ID 的 `MissA2=spike2` 从自身 MPQ 行解析，`Missiles.txt` 原 ID 8、速度 13、射程 40 帧、基础伤害 1–3 和源伤害比例 128；无需为变体写一套弹体行为。原 A2 冰冷普通难度无概率，噩梦／地狱为 5%／15%，走通用元素攻击，未单独量测。完整六 MPQ 包在邪恶洞窟启动，原形与弹体截图、v67 飞行中存读档、生命 55→51 与击杀获得 49 经验已现场确认。
48. **`quillrat3`（Thorn Beast）普通级别变体：已完成。** 复用 `SI` 七动作、`MonSounds.quillrat`、共享 Quill Rat AI 与敌方弹体流程；`TransLvl=2` 使用原红紫调色，自己的 `aip1=20`、`aip2=55`、`aip4=1` 从 MPQ 读取。`MissA2=spike3` 对应运行时 `Missiles.txt` ID 9、速度 16、射程 40 帧、基础伤害 1–4 和源比例 128；无主动技能。原 A2 冰冷普通难度无概率，噩梦／地狱为 5%／15%，进入通用元素路径。完整六 MPQ 包在邪恶洞窟启动，原形与弹体截图、v68 飞行中存读档、生命 55→52 和击杀经验 48 已确认；高难度冰冷未单独量测。
49. **`quillrat4`（Razor Spine）普通级别变体：已完成。** 复用 `SI` 七动作、`MonSounds.quillrat`、共享 Quill Rat AI 与敌方弹体流程；`TransLvl=3` 使用原绿色调色，自己的 `aip1=23`、`aip2=65`、`aip4=1` 从 MPQ 读取。`MissA2=spike4` 对应运行时 `Missiles.txt` ID 10、速度 20、射程 40 帧、基础伤害 1–4 和源比例 128；无主动技能。原 A2 火焰普通难度无概率，噩梦／地狱为 5%／15%，进入通用元素路径。完整六 MPQ 包在邪恶洞窟启动，原形与弹体截图、v69 飞行中存读档、生命 55→52 和击杀经验 28 已确认；高难度火焰未单独量测。
50. **`quillrat5`（Jungle Urchin）普通级别变体：已完成。** 复用 `SI` 七动作、`MonSounds.quillrat`、共享 Quill Rat AI 与敌方弹体流程；`TransLvl=4` 使用原灰色调色，自己的 `aip1=25`、`aip2=75`、`aip4=1` 从 MPQ 读取。`MissA2=spike5` 对应运行时 `Missiles.txt` ID 11、速度 24、射程 40 帧、基础伤害 2–7 和源比例 128；无主动技能。原 A2 闪电普通难度无概率，噩梦／地狱为 5%／15%，进入通用元素路径。完整六 MPQ 包在邪恶洞窟启动，原形与弹体截图、v70 飞行中存读档、生命 55→45 和击杀经验 3 已确认；经验受等级差折减，高难度闪电未单独量测。
51. **`wraith1`（Ghost）Wraith 家族与法力吸取：已完成基础分支。** `MonStats.AI=Wraith` 对应本地 D2MOO 的 `AITHINK_Fn009_Wraith`；`aip1` 接近几率、`aip2` 停顿帧数、`aip3` 近身攻击几率按各难度从 MPQ 读取。决策放在 `wraith_ai.*`，其余命中、死亡、经验和存档共用。原 `WR` 的 NU/WL/A1/GH/DT/DD、`MonSounds.wraith`、`AnimData.d2` 命中帧均从 MPQ 读取；`El1Mode=A1`／`El1Type=mana` 的原触发概率和伤害区间通过共用元素命中减少玩家法力。完整六 MPQ 包在邪恶洞窟启动，原形截图、v71 攻击中存读档、角色生命 55→43、法力从 10 降至 6.16 和击杀经验 72 已确认。房间调度及寻路仍为项目适配。
52. **`wraith2`（Wraith）普通级别变体：已完成。** 复用 `WR` 六动作、`MonSounds.wraith`、共享 Wraith AI、A1 法力吸取及三难度战斗／死亡链；`TransLvl=1` 选择原黄色调色。自身 MPQ 行的 `aip1=60`、`aip2=10`、`aip3=75` 和 A1 法力吸取普通难度 45% 概率及伤害区间均动态读取；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v72 攻击中存读档、角色生命 55→49、法力从满值降至 6.10 和击杀经验 16 已确认。
53. **`wraith3`（Specter）普通级别变体：已完成。** 复用 `WR` 六动作、`MonSounds.wraith`、共享 Wraith AI、A1 法力吸取及三难度战斗／死亡链；`TransLvl=2` 选择原深色调色。自身 MPQ 行的 `aip1=70`、`aip2=8`、`aip3=80` 和 A1 法力吸取普通难度 50% 概率及伤害区间均动态读取；无主动技能或弹体。完整六 MPQ 包在邪恶洞窟启动，原形截图、v73 攻击中存读档、角色生命 55→42、法力从满值降至 5.10 和击杀经验 4 已确认；经验受等级差折减。
54. **`cr_lancer1–3`（长枪女盗贼）第一幕普通变体：已完成。** `MonStats.AI=CorruptLancer` 对应本地 D2MOO 的 `AITHINK_Fn036_CorruptLancer`；接近／攻击几率、停顿帧数、跑动几率与必跑距离从各 ID、各难度 `aip1–5` 读取。长距离跑动后下一次近身攻击的状态独立保存在 v74；决策在 `corrupt_lancer_ai.*`，共用命中、元素、经验、掉落和存档链。原 `CR` 的 NU/WL/RN/A1/GH/DT/DD、`BaseW=2ht`、右手 `pik`、`MonSounds.cr_lancer` 与 `AnimData.d2` 读取 MPQ。`cr_lancer1` 普通难度无元素；`cr_lancer2` 的毒素、`cr_lancer3` 的冰冷在普通难度无触发概率，高难度按原表通过共用元素路径。完整六 MPQ 目录在邪恶洞窟和原野冒烟，三只原形截图、v74 存读档、远距离冲锋状态恢复和击杀经验 36／63／61 已确认。第一幕以外变体仍用替身。
55. **`cr_archer1–4`（弓箭女盗贼）第一幕普通变体：已完成。** `MonStats.AI=CorruptArcher` 对应本地 D2MOO 的 `AITHINK_Fn035_CorruptArcher`；接近、射击、停顿、近身后撤、必跑距离和高难度走近距离从各 ID、各难度 `aip1–8` 读取。独立 AI 决策在 `corrupt_archer_ai.*`；后撤几何移至通用 `monster_wander.*`，与 Quill Rat 复用。共享敌方弹体从 `MissA1`／`MissA2` 按原动作模式解析 MPQ，保存模式并在恢复时核验发射者、原 Missile ID 和寿命；`cr_archer1–4` 使用 `cr_arrow1–4`。原 `CR` 的 NU/WL/RN/A1/GH/DT/DD、`BaseW=bow`、左手 `LHv`、`MonSounds.cr_archer` 和 `AnimData.d2` A1 事件 2 均从 MPQ 读取。后三只 A1 毒素／冰冷／闪电在普通难度无触发概率；高难度进入共用元素路径，未单独量测。完整六 MPQ 目录在邪恶洞窟冒烟，原形截图、v75 A1 飞行中存读档、生命 55→52、四只击杀经验 54／63／71／16 已确认；同一构建复核 `quillrat1` A2 飞行中存读档。
56. **`sk_archer1–3`（骷髅弓手）第一幕普通变体：已完成。** `MonStats.AI=SkeletonBow` 对应本地 D2MOO 的 `AITHINK_Fn037_SkeletonBow`；射击、停顿、接近几率与步数、目标距离来自各 ID、各难度 `aip1–5`。决策在 `skeleton_bow_ai.*`，A1 箭、命中、经验、掉落及保存的敌方弹体模式复用共享流程。原 `SK` NU/WL/A1/GH/DT/DD、`BaseW=bow`、`LHv=sbw`、`MonSounds.sk_archer`、`AnimData.d2` A1 事件 2 和各自 `MissA1=skbowarrow1–3` 均从 MPQ 读取。完整六 MPQ 目录在邪恶洞窟冒烟，三只原形及调色截图、v75 箭飞行中存读档、角色生命 52→49、击杀经验 62／61／35 与后两只原掉落已确认。第一幕以外变体仍保留替身。
57. **`bighead1–4`（巨大野兽）第一幕普通变体：已完成。** `MonStats.AI=Bighead` 对应本地 D2MOO 的 `AITHINK_Fn004_Bighead`；健康阈值、绕行、健康／受伤发射几率从各 ID、各难度 `aip1–4` 读取。家族决策在 `bighead_ai.*`；绕行和后撤几何提到 `monster_wander.*` 与既有 Brute／Quill Rat 复用。原 `BH` NU/WL/A1/A2/GH/DT/DD、`MonSounds.bighead`、`AnimData.d2` A2 事件 2、各自 `MissA2=bighead1–4` 和 A2 闪电数值由 MPQ 提供。共享 A2 弹体支持仅有元素伤害而无物理 A2 列的身份，存档校验接受原表元素弹体。完整六 MPQ 目录在邪恶洞窟冒烟，四只原形及调色、闪电截图、v75 飞行中与后撤中存读档、角色生命 55→49 和击杀经验 2／2／56／55 已确认。第一幕以外变体仍保留替身。
58. **`hellbovine`（地狱之牛）第一幕普通身份：已完成。** `MonStats.AI=Skeleton`，与普通骷髅共用按各 ID、各难度 `aip1–4` 的接近、停顿、近身 A1／A2 选择；原 `EC` NU/WL/A1/A2/GH/DT/DD、`MonStats2.RHv` 首个 `hal` 长柄武器组件、`MonSounds.hellbovine`、`AnimData.d2` 两种命中帧和战斗、经验、掉落均从 MPQ 读取。该身份 `TransLvl=0` 且原资源无 `palshift.dat`，直接使用 DCC 原色；非零等级仍要求原调色资源。完整六 MPQ 目录在邪恶洞窟冒烟，原形截图、v75 A2 出伤前存读档、生命 55→23 和击杀经验 15 已确认。牛场任务入口与精英另列阶段。
59. **`skmage_fire1–2`／`skmage_ltng1–2`（骷髅法师）第一幕普通变体：已完成。** `MonStats.AI=SkeletonMage` 对应本地 D2MOO 的 `AITHINK_Fn064_SkeletonMage`；射击、接近、过近撤退、绕行和停顿参数从各 ID、各难度 `aip1–8` 读取，决策在 `skeleton_mage_ai.*`。原 `SK` NU/WL/A1/GH/DT/DD、`MonStats2.S1v–S8v` 元素层、`MonSounds.sk_mage`、`AnimData.d2` A1 事件 2、`MissA1=skmage3/4` 和 `El1` 火／闪电伤害均从 MPQ 读取；无 A1 物理列时共享弹体不加回退物理伤害。普通 SkeletonMage AI 不读取 `Skill1=SkeletonRaise`，该槽用于其他复活者交互。完整六 MPQ 目录在邪恶洞窟冒烟，四只原形／元素层及弹体截图、v75 两枚弹体飞行中和绕行状态存读档、生命 55→36、击杀经验 38／9／38／9 与后三只原掉落已确认。第一幕以外变体仍保留替身。
60. **`fetish1`（小矮人）第一幕普通身份：已完成。** `MonStats.AI=Fetish` 对应本地 D2MOO 的 `AITHINK_Fn030_Fetish`；近身攻击几率、停顿帧、攻击循环次数和玩家低生命阈值读取该 ID、各难度 `aip1–4`。独立 `fetish_ai.*` 保存接近、连续攻击、撤退三阶段及循环计数；撤退和绕行复用通用地图路径。原 `FE` NU/WL/A1/GH/DT/DD、`BaseW=1hs`、`RHv=fbl` 首组件、`MonSounds.fetish` 与 `AnimData.d2` A1 命中帧从运行时 MPQ 读取。普通 Fetish AI 不调用该行的 `Skill1=SkeletonRaise`。完整六 MPQ 目录在邪恶洞窟冒烟，原形截图、近战后角色生命 55→24、撤退中 v76 存读档、击杀获得 14 经验；本次掉落为空。原引擎逐帧 AI 调度与逃跑路径仍是项目适配。
61. **`vampire5`（吸血鬼）第一幕普通身份：已完成。** `MonStats.AI=Vampire` 对应本地 D2MOO 的 `AITHINK_Fn028_Vampire`；近战／施法几率、活跃距离和技能标志按各难度 `aip1–5` 读取。普通难度技能标志为 1，分支只选 `Skill1=VampireFireball` 和 `Skill4=VampireMissile`，不施放原行另列的火墙／陨石。`content/monsters/monster_spell_data.*` 从运行时 `Skills.txt.srvmissile`、`Missiles.txt` 解析原 ID、速度、寿命、火焰伤害和 DCC；`VA` 的 `SC` COF／DCC、`AnimData.d2` 事件 2、`MonSounds.Skill1` 也从 MPQ 读取。`vampire_ai.*` 处理近战、施法、绕行与低于 33% 生命时后撤；通用敌方弹体保存施法槽、原 ID 和掷出的伤害，按角色火抗结算。完整六 MPQ 目录在邪恶洞窟冒烟，原形和两种施法截图、飞行中 v77 存读档、角色生命 55→47、低血量撤退中存读档与击杀经验 3 已确认；经验受等级差缩减，本次无掉落。原引擎逐帧调度、路径和多目标选择仍是项目适配，其他 Vampire 技能标志待对应怪物阶段。
62. **`fallenshaman1–4`（沉沦魔巫师）第一幕普通变体：已完成。** `MonStats.AI=FallenShaman` 对应本地 D2MOO 的 `AITHINK_Fn013_FallenShaman`；近身、同组命令、复活范围、火焰施法与绕行几率读取各身份、各难度 `aip1–5`。原 `FS` 动作、四级 `TransLvl` 调色、`MonSounds`、A1／A2 事件及 `MonSeq.txt` 的复活时序从运行时 MPQ 解析；`Skills.txt.srvmissilea` 和 `Missiles.txt` 提供 ShamanFire 原弹体及火焰伤害。A2 复活使用原尸体实体 ID，仅从同组选择 `MonStats.minion1` 指定的沉沦魔尸体，普通巫师不再复活巫师；目标按 `MonStats2.ResurrectMode=S1` 和 MPQ 动作时长播放复活动画，经验与掉落仍只结算一次；复活目标、火焰弹体和复活状态写入 v78 存档。完整六 MPQ 目录在邪恶洞窟冒烟，四种原形和火焰截图、角色生命 55→45、尸体复活后存读档及再次击杀经验 0／掉落空已确认。当前 MPQ 的 Fallen S1 COF 引用空 `S3v` 图层；加载时仅省略该缺失的原图层，v91 分发目录已成功初始化营地。复活行为与动画本轮按用户要求未做定向冒烟；逐帧调度与目标选择仍是项目适配。
63. **`crownest1–2`（腐乌鸦巢）及孵化的 `foulcrow1–2`：已完成。** 原 `MonStats2.inert=1` 表示巢固定不走动，仍按 `FoulCrowNest` AI 作为可击杀敌人生成。每只巢的孵化间隔和上限来自各自 `aip1/aip3`；`MonStats.spawn` 决定对应幼鸟，`Skills.txt.Nest` 的 `srvdofunc=91` 和 `MonSeq.txt` S1 事件 4 决定施放及出鸟时刻。原 `BN`／`BK` 图形、调色、声音、AnimData 动作和普通战斗数值从运行时 MPQ 读取；幼鸟的冲锋、攻击、撤退和绕行参考本地 D2MOO `BloodHawk` AI 并读取自身 `aip1–5`。同帧孵化避免重叠；召唤鸟保留巢的来源关系，死亡无经验／掉落，普通鸟仍按自身 MPQ 身份结算。完整六 MPQ 目录在邪恶洞窟冒烟，两巢各孵出对应原形；召唤鸟击杀经验 0、无掉落，普通鸟生命攻击使角色 55→50，击杀经验 22／29。孵化后及冲锋／绕行中 v79 存读档通过，原形截图在忽略目录。巢与鸟的其他幕变体预留。
64. **`arach1`（蜘蛛）第一幕普通身份：已完成。** `MonStats.AI=Arach` 对应本地 D2MOO `AITHINK_Fn026_Arach`；近战、绕行、接近、逃离距离与低生命阈值读取该行普通难度 `aip1–5`。原 `SP` NU/WL/A1/A2/GH/DT/DD、`MonSounds.arach`、`AnimData.d2` A1/A2 出招帧从运行时 MPQ 读取。`Skills.txt.SpiderLay` 的 `srvdofunc=23`、光环持续、减速时间与百分比，以及 `Missiles.txt.spidergoo` 的原 ID／图形／寿命／范围决定 A2 后沿移动路径留下的蛛网；角色踩中后临时减速。SpiderLay 限定在独立怪物模块，存档保存蜘蛛光环、蛛网弹体、角色减速来源和剩余时间。完整六 MPQ 目录在邪恶洞窟冒烟，原蜘蛛／蛛网截图、踩网 `-100%` 减速、离网后恢复、低血量与减速中的 v80 存读档、A1 后角色生命 55→46、击杀经验与原掉落均已确认。AI 的调度与几何路径仍是项目适配；其他幕蜘蛛变体、精英／首领及 Boss 另列。

当前数值计算依据本地 `reference/d2moo/source/D2Common/src/DataTbls/MonsterTbls.cpp` 的 `DATATBLS_CalculateMonsterStatsByLevel` 和 `reference/d2moo/source/D2Game/src/MONSTER/Monster.cpp` 的生命掷骰；AI 规则核对 `reference/d2moo/source/D2Game/src/AI/AiThink.cpp` 的 Skeleton、Zombie、Fallen 与 Brute 分支，动作帧核对 `reference/d2moo/source/D2Common/src/DataTbls/AnimTbls.cpp` 与 `Units.cpp`。区间、AI 参数和动作时序始终从用户挂载的 MPQ 读取，参考仓库不作为运行时数据源。当前目标选择、AI 调度和随机流仍是项目适配，不能视为原版逐帧复刻。
