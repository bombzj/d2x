# 德鲁伊技能实现

更新：2026-10-16。30项职业技能及对应动物／藤蔓／灵体、隐藏技能、客户端依赖已接入新底座。本页分别登记定义、权威执行、客户端表现、原版依据和未闭合依赖，公共规范见[COMMON](COMMON.md)。

当前SC、狼熊武器、变形切换及Arctic持续程序由所属帧事件推进；Fury按Do13逐次分配／兑现、GUID后继及回绕重排，不预排整轮攻击。旧批量释放扫描已撤出，完整原全局交错与全部组合仍未对齐；见[内核对照](../../modules/SERVER_SYSTEMS.md#调度与动作的职责及迁移边界)。

PD2远端差异：Arctic Blast的CltDo111使用独立喷流程序；Fire Claws／Firestorm的110／119按当前Calc2／Calc1生成ChargedBolt路径，单位种子递推及77帧上限来自目标核心。火风暴、极地风暴及火爪已在本地原生PD2服务观察实际弹体，火爪同时核对目标死亡。当前PD2 Hunger的TargetCorpse=1、SrvDo165，只允许熊形消耗真实尸体，原服已返回hunger状态及corpse_nodraw；不能沿用原版对活怪物的近战用法。乌鸦、三种藤蔓、三种灵体、灵狼／狂狼／熊已逐项收到当前MPQ对应的原生召唤单位；飓风装甲已收到151状态及132／133吸收值。狼形139、熊形140与再次施放回人形均已核对；1级Shape Shifting的命中50、最大生命加成2分别出现在两种变形状态中，熊形另含当前表基础10。狼形三项攻击、Maul命中／状态及人形Gust传送已有本地原服事实；Twister已观察近身目标死亡，Tornado另行观察原弹体478持续飞行；熊形Shock Wave的511及人形Molten Boulder的453／452也已观察，召唤物同时作战，未据此独立认定眩晕或伤害。毒传播、全部阶段／装备与多人组合仍待覆盖。版本及原入口见[核心分析](../../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)，其余PD2缺口见[技能目录](README.md#pd2远端技能范围)。

| ID | 原Skills身份 | 原服务端入口／依赖 |
| --- | --- | --- |
| 221 | Raven | Do114；真实乌鸦、原攻击次数／盘旋AI及不可受击资格 |
| 222 | Plague Poppy | Do115；毒藤、地下状态、Vine Attack与目标毒资格 |
| 223 | Wearwolf | Do116；狼形态状态、属性／期限、动作及技能资格 |
| 224 | Shape Shifting | 原无状态列表的被动；由两种变形公式读取等级 |
| 225 | Firestorm | Do117；首枚普通构造与其后逐枚Charged初始化 |
| 226 | Oak Sage | Do119；真实灵体与Oak Sage Aura、同行／逃离AI |
| 227 | Summon Spirit Wolf | Do119；真实灵狼、协同属性、传送／同行与近战 |
| 228 | Wearbear | Do116；熊形态状态、属性／期限、动作及技能资格 |
| 229 | Molten Boulder | Do0；原emerge／滚石／结束子体及复合伤害 |
| 230 | Arctic Blast | St11／Do19；原序列18、INFERNO持续参数、费用与停止 |
| 231 | Cycle of Life | Do115；生命藤、原可用尸体选择／消费与主人恢复 |
| 232 | Feral Rage | St56／Do120；开始阶段预分配、成功后累积与有序属性写入 |
| 233 | Maul | St56／Do120；开始阶段预分配、层数／眩晕与增伤列表 |
| 234 | Eruption | Do28；原目标合法性、中心控制器与地裂子体 |
| 235 | Cyclone Armor | Do18；公共六AuraStat／五PassiveStat、原Event25吸收 |
| 236 | Heart of Wolverine | Do119；真实灵体与Wolverine Aura |
| 237 | Summon Fenris | Do119；真实狂狼、协同属性、吃尸狂暴及传送／同行 |
| 238 | Rabies | St57／Do121；开始准确率、动作阶段再分配、独立感染列表及传播弹体 |
| 239 | Fire Claws | St58／Do2；开始阶段武器填充后追加技能元素、动作阶段排出 |
| 240 | Twister | Do118；逐枚Charged初始化与眩晕弹体 |
| 241 | Vines | Do115；法力藤、原可用尸体选择／消费与主人恢复 |
| 242 | Hunger | Do122；武器／元素填充后物理比例及额外吸取 |
| 243 | Shock Wave | Do8；原扇形飞弹与独立眩晕长度 |
| 244 | Volcano | Do123；来源预抽种子、原落点资格及火山控制器 |
| 245 | Tornado | Do118；Charged初始化、原移动与周期范围接收 |
| 246 | Spirit of Barbs | Do119；真实灵体与Barbs Aura、原反伤事件 |
| 247 | Summon Grizzly | Do119；真实熊、协同属性、原AI与BearSmite序列 |
| 248 | Fury | St37／Do13；原连续攻击、回滚、目标与分配顺序 |
| 249 | Armageddon | Do124／ActiveState146；原技能参数种子、五次落点探测与控制弹体 |
| 250 | Hurricane | Do124／ActiveState145；原状态期限、一次每轮伤害掷值与逐目标接收 |

`content/skills/druid_data`从当前MPQ准备程序、限制状态、原生召唤身份与参数；`gameplay/skills/druid_summon`复用公共公式、召唤被动／属性和MonLvl层。动物、藤蔓、灵体的身份／位置／资源／技能与种子由真实 `monsters::Actor` 唯一持有；伙伴只保存控制与AI阶段。狼／狂狼／熊按当前PetType.group互斥，不能套用D2R的共存规则。乌鸦攻击次数、灵体光环和藤蔓尸体选择各有原生程序，不以普通怪物AI替代。

数值与时序以当前MPQ和目标1.13c为准。固定D2MOO的Do117把正数子体数量判为失败，原版D2Game `6FC6573A–6FC65769` 明确正数通过并先创建一枚、随后递减；采用同版本原分支。St56／57／58分别位于原版 `6FC64D70`／`6FC64C20`／`6FC64ED0`，Do114–124及ActiveState145／146已从原指针表定位。DLL版本身份见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)，原表与DLL不进入源码或分发。

## 执行与公共边界

- `content/skills/druid_data`只准备当前MPQ的Skills／States／PetType／MonStats／MonLvl及AnimData；`hosting/druid_summon_content`补齐真实单位动作、序列、组件、生命周期和原伤害来源。服务器不读取MPQ。
- `skills/druid`承接出生／持续起手，`druid_active`保存ActiveState145／146的回调续点，`druid_melee`保存St56／57／58及动作阶段的已分配接触；容量不足时复用已掷值与阶段，不重做伤害或费用。
- 变形配置共用`gameplay/actions/shape_timing`与`skills/shape`，按原玩家→怪物模式回退、UseAttackRate、原形态帧数和武器自身速度计算，保留真实玩家身份。狼／熊状态退出按原组内状态ID顺序撤销，狂怒／撞槌单独直分配列表，不误触变形互斥。
- `companions/native_summon`、`native_pet_move`和`monsters::prepareNativePet`供刺客／德鲁伊共用；AI阶段不拥有第二份单位位置、属性或随机流。动物、灵体、毒藤和资源藤各保留原分支。技能等级变化只重算PetType上限并从旧宠物起撤出，不重掷存活宠物的出生生命或重做被动列表。
- 灵体隐藏光环按当前MPQ的sumskill链接准备，SetSummonPassiveStats在出生事务内按继承次序选择右技能；共用`effects/unit_aura_selection`的列表／原周期事件，不延迟到AI或用装备光环替代。
- 藤蔓／狂狼找尸体走原圆形UnitFind范围和房间单位顺序，再检查原DEAD模式、deadCol、corpseSel、udead及敌我资格。CorpseCycler先标记不可选择、创建以主人为owner的恢复弹体，再发布A3；Fenris Rage保留独立吃尸／状态阶段。公共移动路径按真实单位查询目标位置，不以活体攻击资格拒绝尸体追踪；攻击／施法接收仍独立复验资格。
- `presentation/remote`只消费原状态与A3／73／动作包；补齐原Clt67–69、Hurricane90、Volcano91、Armageddon92、CorpseCycler93和Vine89／St49，Fenris St51／Do79，以及飞弹Clt47–51／60、Hit52–56。熔浆巨岩反弹／地面火焰已有；飞石数量、显式速度及构造Gfx步进使用共用助手，原程序与缺口见[怪物117项](../monsters/ACT3.md#逐项接入)；火山落石使用原11位定点Gfx轨迹。客户端公式目录包含cltcalc1／2／3，原CelFile=null的控制弹体保留程序与寿命但不绘制图像。未传输的客户端粒子种子仅用于本地表现，不消费权威战斗种子。

## 同版本差异与证据

目标为经典LoD 1.13c，固定D2MOO仅作为1.10f重建证据。除了Do117的正数计数分支，1.13c灵狼 `6FCCAF61–6FCCAF98`明确区分近战攻击与追击；未照抄重建代码中恒真的MonStats指针判断。灵狼候选的碰撞反向条件则由 `6FCCADDC–6FCCAE10`确认保留。正常传送仍分别为灵狼4帧／狂狼2帧。原版SetSummonPassiveStats `6FC6F970`的 `6FC6FE7F–6FC6FE8E`在继承技能的aura标记成立时调用右技能选择，早于单位事件登记；灵体采用同一顺序。Fury共用原近战范围筛选0x20000及GUID后继选择；该位不是物件／物品目标类别。资源藤回收弹体按剩余帧触发，当前生命／法力从唯一资源所有者合入公共单位属性视图，再按原整数百分比加回并封顶。

客户端A3入口 `6FB5EFB0`通过原cltprgfunc表派发。无目标单位的原GUID为FFFFFFFF，参数字节与全局X／Y保持原字段位置；不再因空目标丢弃火山／末日战场事件。Hurricane90／Volcano91／Armageddon92分别位于 `6FB79BE0`／`6FB794E0`／`6FB795F0`；飞弹Clt47／48／50为 `6FB6B470`／`6FB675C0`／`6FB6B2A0`，Gfx定点更新为 `6FB77FB0`。狂狼吃尸的CltSt51／Do79为 `6FB74990`／`6FB742B0`，按起手保存的尸体坐标在动作时构造cltmissilea；服务端尸体消费事务以真实接收尸体为上下文，狼的狂暴列表随后独立写入。表、反汇编、运行日志及截图放忽略目录，不进入源码或运行包。

## 未闭合边界

完整随机／事件交错、装备／难度／多人／背压矩阵仍需原版同条件核对。曾有后台进程退出而未捕获退出栈，原因尚未定位，不能由后续未复现推断已修复。公共准确性门槛见[总计划](../../architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)；程序入口或公共函数存在不表示这些边界已闭合。
