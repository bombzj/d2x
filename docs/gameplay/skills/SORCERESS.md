# 女巫技能

PD2专项内容：[职业技能](../../pd2/CLASS_SKILLS.md)。本页保留原版／公共规则及必要的版本边界。

更新：2026-11-10。自研服务端执行26项主动、4项被动，原服与自研共用原协议客户端。此页维护30项职责、女巫专属Clt／Srv差异、原版依据和缺口；公共规范与整体状态见[COMMON](COMMON.md)。

女巫持续状态现从公共原公式环境准备AuraLen／AuraStat／Passive槽及modifierlist元数据，使用独立状态位与原有符号移除事件。冰甲按原状态组先撤出再投递，附魔保持同级复用及后续非零写入；Blaze、Thunder Storm及Energy Shield采用对应原始列表生命周期。能量盾百分比／耗蓝因子读取calc1／calc2，魔法耗尽撤出状态并随同一人物候选发布事实，骨甲池写入／撤出也在资源观察边界执行。单位事件登记次序已独立保存；骨甲与能量盾按登记先后处理，能量盾耗尽在接收流程内撤出，冻结反应读取当前原calc整数帧。完整伤害前派发及所有跨职业组合仍需迁移／对齐。

## 30项公共职责核对

客户端RemoteCombat现按当前Levels.Teleport复验传送路径：0拒绝，2沿UNITS_TestCollisionByCoordinates的源／目标尺寸2与0x0804检查截短射线，其他非零模式不检查中途步行障碍。都瑞尔房间73的当前原表值为2；限制并非“击杀后才禁传送”，实际可否跨越取决于这次路径上的门／飞弹屏障。AI提前查询同一功能，受阻段改步行；怪物不计入该射线。原版依据和已覆盖的落点范围见下文。

按当前MPQ的Skills／Missiles函数号定位本地D2MOO。空SrvDo经通用 `D2GAME_SKILLS_Handler_6FD12BA0` 创建srvmissile，不是未实现；被动不走施法入口。C＝公共法力／等级公式，D＝公共技能元素伤害／协同／支配，L＝公共元素长度，P＝公共线性被动；通用取整与权限见[跨职业计算](COMMON.md#跨职业公共计算)。表中已接入口不代表全部参数或表现已认证。

| ID／技能 | 原SrvSt／SrvDo及执行证据 | 公共计算／定义 | 两端执行边界 |
| --- | --- | --- | --- |
| 36 Fire Bolt | 通用Handler；MissMode SrvDo1 | C、D、速度／碰撞几何、SC时钟 | 服务端单体命中；客户端原图飞行。d2decomped 的直射和技能协同与当前表一致；原 `DescDam5`（D2Client `35270`）在技能元素曲线上计入支配并按火元素着色。通用发射在施法者整数格创建弹体，原表 `firebolt` Range=50、Vel=20。 |
| 37 Warmth | D2Common RefreshSkill/passivecalc | P：skillRankBonus | 服务端attributes安装恢复贡献；客户端提示。D2Common `5257F`–`52637` 按有效等级求被动槽并写入状态列表；当前表为 `manarecoverybonus=ln12`（30 起、每级加 12）。d2decomped `mana_per_frame` 的 8.8 回蓝次序与现有公共规则一致，装备恢复加成和固定恢复各在原有阶段参与。 |
| 38 Charged Bolt | SkillSor SrvDo17／MissileInit_ChargedBolt | C、D、数量曲线、chargedBoltPath | 独立权威／视觉弹体，索引和77步上限相同。原版两端执行入口为 `43B60`／`74AC0`，数量按 `min(24,ln12)`。d2decomped 的双格游走、`目标X+索引` 种子与原回调一致；服务端用整数目标格取种，客户端先取总帧低字节再限 77。 |
| 39 Ice Bolt | 通用Handler；MissMode SrvDo1 | C、D、L、速度／碰撞、SC | 真实减速归服务端；客户端接状态。d2decomped 使用通用单体弹道。原表两端 `icebolt` Do1、无 NextDelay、基础寒冷 150 帧；原 D2Common `4E900` 求元素长度，`DescDam5` 使用冷元素色。发射格与通用原入口对齐。 |
| 40 Frozen Armor | SkillSor SrvDo18、EventFunc02 | C、等级防御／期限／冻结公式 | effects互斥安装、近战伤害事件；客户端状态覆盖层。D2Game `428F0` 验证防御状态入口；原表 `damagedinmelee` 绑定 Event2，回调要求物理伤害大于零，冻结取 `calc1`，持续期与冻结分别读取另外两甲的基础等级。d2decomped 的受伤后冻结方向一致，现有实现已区分物理伤害门槛。 |
| 41 Inferno | SkillSor SrvSt11／SrvDo19／DoInferno | C、D、线性射流长度、seq6步序／固定时钟 | 服务器起手及每次接受的续按扣费，逐帧Do19处理发射。原 SrvSt11 `44550` 的冷启动门槛为6点，沿通用Start费用入口 `9FE87` 扣除实际费用。原Do19 `44390 → 441C0` 首次设置Param1，后续以施法者为原点，按 `max(1,calc1)` 设置火焰寿命。客户端24选择两种火图。 |
| 42 Static Field | SkillSor SrvDo20／AuraCallback_StaticField | C、比例／下限／范围准备值 | 真实生命、难度下限和抗性仅服务端；客户端效果。D2Game `430D0` 先读最低伤害与比例，扩展版才加载难度门槛。d2decomped 的范围及当前生命比例与原表一致；D2X 已保留整数生命门槛、留 1 生命的中间截断及负抗性预折算，随后走公共伤害。 |
| 43 Telekinesis | SkillSor SrvSt12／SrvDo21 | C、D、距离／击退几何 | SrvSt12 `42360` 检查整数平方距离，Do21 `431C0` 按单位类型处理。d2decomped 的怪物伤害／击退与原表一致，D2X 使用 Param2=35。1.13c 的类型拒绝只给施法者附声，允许拾取的物品进入普通拾取反馈。 |
| 44 Frost Nova | SkillSor SrvDo22 | C、D、L、missileRingBurst／Direction | 原CltDo25视觉环；服务器弹体及冷伤。d2decomped 的 64 个整数目标偏移与原共用环吻合；SrvDo22 `43AA0` 和 CltDo25 `73CB0` 分别在飞弹速度上加表公式，当前技能两侧无速度加成。`frostnova` 使用 NextDelay=4 和 200 帧基础冰冷减速。 |
| 45 Ice Blast | 通用Handler；MissMode SrvDmg4 | C、D、L、速度／碰撞 | 服务端难度冻结长度；客户端接原状态／冰图。d2decomped 的通用飞弹入口与原表一致。原 D2Game `38E70` 将寒冷长度移入冻结字段并清除前者，长度读取冰尖柱基础等级协同；现有命中程序已按此转换。发射格同步改为施法者整数格。 |
| 46 Blaze | SkillSor SrvDo23／CreateBlazeMissile；MissMode SrvDo5／Dmg3 | C、D、期限；整数跨格规则 | 服务端状态移动留火／逐帧伤；客户端state setfunc3及CltDo5。原 SrvDo23 `42730` 安装表状态，移步回调以路径跨格标志、状态列表技能等级和非城镇为条件。d2decomped 按移动帧近似留火；D2X 的权威跨格入口保留原条件，客户端按可见移动格重建。原表 `blaze` Do5／Dmg3 和 `dm12` 持续期已接入。 |
| 47 Fire Ball | 通用Handler；MissMode SrvHit1 | C、D、速度／碰撞／范围几何 | 服务端范围目标与伤害；客户端CltHit1爆炸。D2Game `3B990` 在飞弹整数格执行半径 4 的范围命中，d2decomped 的单次掷伤记录与现有共用范围命中一致。D2Client `BC1A0` 用独立视觉半径 3 生成 `fireexplosion2`。发射格与原通用入口对齐。 |
| 48 Nova | SkillSor SrvDo22 | C、D、missileRingBurst／Direction | 原CltDo25视觉环；服务器雷伤。d2decomped 使用同一个 64 向整数环。原 `43B06` 以 Nova 表行速度加 `calc1` 后进入共用发射（当前加成为零），CltDo25 同理。原 `nova` Range=13、Vel=24、NextDelay=4，权威雷伤与视觉环分别执行。 |
| 49 Lightning | 通用Handler；MissMode SrvDo1 | C、D最小值门槛、seq12、速度／碰撞 | 服务端穿透；客户端CltDo8链段图。d2decomped 的普通定向弹道与原表一致。原 `lightningbolt` Do1、CollideType=3，碰撞不销毁，NextHit／NextDelay 为空。服务端按整数目标格直射；客户端 CltDo8 独立生成雷电轨迹。 |
| 50 Shiver Armor | SkillSor SrvDo18、EventFunc03 | C、D、L、等级防御／期限 | 服务端近战尝试反击；客户端状态／命中。原 Event3 `42B40` 不读取来袭物理伤害，掷出本技能冷伤并以 `0x4021`／HitClass `0xD` 派发。d2decomped 和 D2X 都由近战尝试触发，防御、期限和伤害协同读取当前表；D2X 经公共反击接收流程。 |
| 51 Fire Wall | SkillSor SrvDo24；MissMode SrvDo6／Dmg3 | C、D、missileWallDirection、跨格、速度 | 两侧maker生成各端执行，服务器真实火段伤害。原 SrvDo24 `437D0` 和 CltDo26 `74770` 在目标格创建垂直相反的两侧 maker，再建中心火；d2decomped 的留火及逐帧伤害与原函数方向一致。当前 `firewallmaker` 寿命为 `7+2*lvl`，共用整数方向，施法延迟读取 Skills。 |
| 52 Enchant | SkillSor SrvDo25 | C、D、线性期限／命中率 | 服务端友方资格、来源及成组事务；客户端原状态。原 SrvDo25 `42570`／`425EE` 确认目标不存在或阵营不一致时回退自身；d2decomped 的施加规则和 D2X 两种单位入口已保留回退。期限与附加火伤、命中率分别使用 `AuraLenCalc` 和非零 AuraStat 槽，支配按表伤害阶段求值。 |
| 53 Chain Lightning | SkillSor SrvDo26；MissMode SrvHit12 | C、D最小值门槛、seq12、跳数、GUID successor | 真实候选／再次命中归服务端；客户端CltHit16可见链段。原 SrvDo26 `43720` 与 CltDo27 `73E80` 写入 `calc1`（当前 `ln34 / 5`）；改为两端直接求表式。d2decomped 所记范围回退和排除刚命中者，由原 SrvHit12 `3D020` 与 CltHit16 `B67D0` 验证，NextDelay=4；权威首段按整数目标格发射。 |
| 54 Teleport | SkillSor SrvDo27、SUnit公共重定位及COLLISION_GetFreeCoordinates | C、Levels.Teleport、原尺寸／掩码与落点搜索 | 释放时取得真实房间并寻位，支持已准备且步行相连的同幕邻区；客户端原位置校正。d2decomped 对 `Levels.Teleport` 的 0／2 区分与本地 D2Game `43484`–`434C1` 一致；原 `E19C0` 使用公共落点搜索。当前客户端预检与服务端执行同一掩码，实际落点以服务端位置校正为准。 |
| 55 Glacial Spike | 通用Handler；MissMode SrvHit13 | C、D、L、范围／冻结等级曲线 | 服务端接触范围冷伤／冻结；客户端CltHit14冰图。d2decomped 的范围一次掷伤与原 SrvHit13 `3B3E0` 一致；当前半径和冻结长度分别由 `AuraRangeCalc`／`AuraLenCalc` 回退求得。CltHit14 `B0870` 的主冰图、三组碎片与旋转取样均已接入，两端按各自结果执行。 |
| 56 Meteor | SkillSor SrvDo28；MissMode SrvHit14 | C、D；地面火evaluateMissileDamageFixed；范围／期限 | 18个权威火偏移与客户端CltDo9／Hit18图层分开。d2decomped 的定点落地和独立 `meteorfire` 伤害与原 `42D90`／`3DEF0` 一致；原 CltHit18 `B6540` 使用 5 个爆炸偏移、18 个有序火偏移及 Param3／4 灯火寿命。客户端改为按原整数偏移、表计数与灯火寿命创建；服务器复用同一 18 点表。 |
| 57 Thunder Storm | SkillSor SrvSt13／SrvDo29 | C、D、linear期限、dm56递减周期 | 服务端定时选敌／落雷；客户端原A3瞬时图。原 SrvSt13 `42230` 重置上一目标为 -1，SrvDo29 `43FA0` 以 Param7 和原 GUID successor 选敌。d2decomped 的循环选敌与原回调一致；D2X 改用原单位 GUID，成功重施重置游标，目标移除后仍保留上次 GUID。落雷继续通过原技能脉冲通知客户端。 |
| 58 Energy Shield | SkillSor SrvDo23、EventFunc24 | C、吸收上限／TK协同、ln12期限、absorbManaShieldFixed | 服务端受击扣蓝／生命／状态事务；客户端原状态。d2decomped 的分通道吸收与原 Event24 `41FB0`／表 `FC3E8` 一致；原顺序为物理、火、电、冷、魔法和符合条件的三种吸取，逐项扣除 8.8 法力。当前接收流程保留原取整、方向叠层和耗尽撤盾。 |
| 59 Blizzard | SkillSor SrvDo28；MissMode SrvDo10／CreateMissileWithCollisionCheck | C、D、missileEmissionDue／blizzardOffset | 独立随机、客户端反向偏移／四变体，实际碎片碰撞归服务端。d2decomped 的 Calc1 半径及 Calc2 间隔与原 SrvDo28／Do10 一致。原 CltDo13 `BBC10`／`B8DF0` 先取变体再判周期；重种调用 D2Common `36740` 同时写入低字和高字 666。两端修正为完整重种，保留客户端反向偏移和独立碰撞。 |
| 60 Chilling Armor | SkillSor SrvDo18、EventFunc01 | C、D、L、防御／期限、速度 | 服务端ReturnFire资格／反击弹体；客户端原状态／动作。原 Event1 `434E0` 从来袭弹体取攻击者与 `ReturnFire`，在装甲持有者格向攻击者发射；反击弹体自身 ReturnFire=0。d2decomped 的反击目标与原入口一致。客户端改取 `CltMissileA`，优先取已同步的状态等级，按整数格重建反击。 |
| 61 Fire Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus，D中支配阶段 | 服务端有效等级加伤，客户端已知值提示。d2decomped 标注火支配属性 329；原 D2Common `4F888` 读取同一槽，`50532` 在协同之后按元素加成。当前 `passive_fire_mastery=ln12`（30 起、每级 7），生效等级和装备贡献沿公共属性流进入服务端与面板。 |
| 62 Hydra | SkillSor SrvSt14／SrvDo144；MonsterAI Hydra | C、主人原Passive／Aura／Calc／SumSkill、PetMax、原整数期限；自身337等级与飞弹关联62伤害公式 | 原SrvSt14 `42AD0` 验证目标房间，成功起手扣费。Do144 `43D40` 按三个偏移依次出生并独立处理失败，至少一头成功才启动40帧冷却。d2decomped 以同格近似三头。D2X 使用原偏移、出生模式、AI期限与火焰精通快照。 |
| 63 Lightning Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus，D中支配阶段 | 服务端有效等级加伤，客户端提示。原 D2Common `4F891` 查询属性 330，与 d2decomped 一致；属性从当前 `passive_ltng_mastery=ln12`（50 起、每级 12）和装备取得，应用于协同之后的雷伤，客户端面板使用同一加成。 |
| 64 Frozen Orb | 通用Handler；MissMode SrvDo15／16／SrvHit29 | C、D、L、remaining调度／环形／转向／速度 | 独立Clt19／20／Hit30程序，服务端子弹重新求伤害。原 SrvHit29 `3C850` 在当前计数非零时返回继续，归零才按步长 4 环射；d2decomped 的环偏移一致，但省略部分接触分支。当前 Do15／16、Clt19／20 与 CltHit30 `B6010` 分别执行散射、转向和期末环，子弹独立求冷伤。 |
| 65 Cold Mastery | D2Common RefreshSkill/passivecalc | P：skillRankBonus | 服务端抗性穿透且不破免疫，客户端提示；不是冷伤百分比。当前 `passive_cold_pierce` 为属性 335，`ln12` 从 20 起每级加 5。原 D2Game `102B34`／`102B8C`／`102BB8` 的冷伤、冰冷和冻结长度行共用该槽；d2decomped 的免疫条件与现有规则一致，普通穿透不破怪物免疫。 |

## 公共层及原版证据

迁入起点为master的 `session_skills`、`projectile_launch`、`frozen_orb`、`blizzard`、`arc`、`glacial_spike`、`meteor`、`firewall`、`elemental_runtime`、`applied_effects`、`reactions`、`shield`及伙伴代码。当前MPQ决定参数、图形与身份；原分派查 [D2Skills.cpp](../../../reference/d2moo/source/D2Common/src/D2Skills.cpp)、[Units/Missile.cpp](../../../reference/d2moo/source/D2Common/src/Units/Missile.cpp)、[SkillSor.cpp](../../../reference/d2moo/source/D2Game/src/SKILLS/SkillSor.cpp) 和 [MissMode.cpp](../../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp)。固定版本与DLL摘要见[资料来源](../../resources/THIRD_PARTY.md)；模块导入／序列及数值取整证据只维护在[参考设计](../../architecture/REFERENCE_DESIGN.md#女巫公共层的原版核对)。

`resolveSkill`供服务端求值与客户端提示，`rank_bonus`处理四被动。弹体复用 `projectile_path`／combat几何；具体专项函数已列在逐项表，不维护第二份家族清单。客户端只消费已知值，不重新安装支配、护盾或伤害。

### 时序与弹体差异

- 原版共享施法延迟见[公共规范](COMMON.md#原版施法延迟)。当前`assets/mpq2`的Frozen Orb delay=25帧（1秒）、Meteor=30帧、Blizzard=45帧，Teleport为空／0。冰封球在动作帧走客户端 CltDo29（`73D70`），不等冷却回包。已起手时即使目标在释放前死亡，原版通用弹体仍取其坐标；本地短锁上限12帧，服务端`skilldelay`按原表完整时长执行。
- `prepareCastAnimationTiming`／`normalCastTiming`／`playerCastSequenceTiming`两端共用SC／seq12时钟，seq6固定速率。本人FCR优先取收到的属性；必要时从完整装备／套装／状态列表汇总，缺输入保持未知，远端未知FCR仍使用基础显示速率。
- 冰封球用原GetRemainingFrames的TotalFrames−CurrentFrame差值判散射与转向，保留Clt／Srv参数及零帧发射。0x73带剩余寿命时，客户端时钟已有已过帧前缀，不能重复扣除。子弹出生时服务端重新求当前支配／协同；撞墙不补自然到期环射。
- `advanceMissileVelocity`共用五帧加速／限速，`missileChangedCell`共用跨格条件；两端固定点与cells/second表示仍各自适配。Blaze留火、火墙maker、Meteor落地及Blizzard周期持有各端创建队列，尚未共用完整调度／路径状态。
- Blizzard共用整数落点，但客户端按remaining／globalX重种并保留反向偏移／图像变体，服务端使用自己的随机与真实碰撞。Meteor服务端18个火偏移与客户端分层图像分别执行，独立地面火使用Missiles伤害曲线。

本地1.13c D2Client静态补证：Inferno CltDo24经RVA74D50／74930选择两种火图；Blizzard CltMissile13格点生成与序列导入证据见参考设计。Fire Wall CltDo26经74770生成两侧maker及中心火；Meteor／Blizzard CltDo28经73DF0→A1540→AFF10在目标点创建中心弹体，这些Clt程序不以ClientSend阻止本地创建。

客户端Meteor落地火继承原弹体拥有者，按当前Missiles.CltSrcTown及拥有者房间对应的Levels.Town缩短剩余寿命；PD2专项说明见[职业技能：陨石城镇寿命](../../pd2/CLASS_SKILLS.md#职业技能陨石城镇寿命)。观察者进入城镇或房间加载长帧不再整批删除已有弹体；HellMeteor落点的城镇限制也取实际落点房间。未知拥有者／房间不推断城镇身份。CltSrcTown是客户端显示规则，服务端SrcTown另行裁决伤害弹体；静态地面图像目前只接剩余寿命上限，完整原动画收尾与实机回归仍待核对。原生入口证据见[核心分析](../../resources/PD2_CN_DLL_ANALYSIS.md#角色技能树与客户端程序)。

### 领域和原包行为

起手与释放分别复验活人、区域代次、技能选择及目标。Inferno冷启动建立20帧的状态12并清零技能参数，热续按刷新至当前帧加6并立即进入Do19。每个成功St11按原表费用扣一次，等级1为144/256点。Do19推进射流而不额外扣费。松键包0x12关闭状态位，列表到期才结束服务端续射，对应原移除回调 `42290` 的SKSRVDOFUNC写入。客户端首次续按等待原seq6动作帧。Hydra不跟随主人跨区。Enchant跨玩家使用最多两份CharacterEdit成组提交，友方单位状态由effects维护，晚入局可以查询当前状态。

原包表现分工为4C／4D派生普通投射、连锁、Inferno及Hydra动作，A3呈现Thunder Storm，A7／A9呈现单位状态，67/action20呈现原击退，11／2C呈现叠层／附加音效；私有资源用1F，位置校正用15，命中用0C。ClientSend与地面程序重建遵循本节后述限制，不因这些映射补发重复弹体。

`actions`持有玩家／怪物待释放动作，`skills`解释施放、冷却、释放及射流；`missiles`持有权威飞行／子弹，`combat/spell_damage`处理逐目标伤害／NextDelay，`effects`持有装甲反击、吸收及周期，`companions`持有Hydra归属及玩家宠物AI，单位属性／生命／模式归monsters。Hydra沿怪物动作入口请求337。心灵传动复用inventory／objects／travel各自资格和事务，客户端开仓库／传送点等待原77／63。1.13c `433CB` 对不支持的类型仅附声0x13，与D2MOO中的重发分支有版本差异。普通拾取在物品资格拒绝时附声0x13，容量放不下时附声0x17，两者均通过原地面移除及Add重发同一GUID。拒绝反馈与费用同批提交，声音按原附声目标只送施法者。

Teleport普通施放、聚气与物品触发共用`travel::castTeleport`。请求保留原50子格轴向范围，不以点击处已可步行或当前区域矩形作为起手资格。释放时按来源Levels.Teleport裁决：0拒绝，2使用UNITS_TestCollisionByCoordinates的两端尺寸2缩短射线及0x0804掩码，其他非零值不检查穿越路径。目的地只从原本房间／邻接房间解析，先取得真实地形，再以玩家尺寸2、掩码0x1c09调用公共nativeFreeCoordinates；先原点，失败后按原周界顺序、曼哈顿距离及首个同距候选搜索，外半径50不包含边界，失败不使用原点后备。不要求从源点步行到达，也不另加怪物／玩家避让半径。

实际落点、法力／聚气及可靠位置事实共用人物事务，输出背压不提前扣费或移动。相连同幕邻区复验目的区域代次及碰撞，TravelFact和位置事实一起接受后提交区域／房间归属；成功清空旧路线、定点步进及移动交互目标，并更新传送历史位置和代次。客户端只提交原目标、等待原15位置包，不预判落点。原版1.13c D2Game SrvDo表RVA1074A8第27项指向43450；43484–434C1确认Levels.Teleport及0x0804分支，E19C0／E1A44–E1A83确认公共重定位的尺寸查询、0x1c09、关闭失败后备及路径重置入口。D2Common ordinal10509／RVA4D840传入最大距离50、步长1并调用4D140。DLL摘要见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)；当前MPQ Skills[54].SrvDoFunc=27、Levels.Teleport仍动态准备。上述为源码／原表／原DLL静态核对，尚未进行本项实机回归。

**联网落点。** 零售 Skills[54] 的 CltStFunc／CltDoFunc 均为空。原 1.13c D2Client 的 11 字节 S2C 0x15 表项在 RVA DDF5C，AF640 处理器按类型和 GUID 找单位，把包内坐标交给 A7E20 重定位。D2X 可先显示施法，收到本人重定位后才切换脚点并清除旧路径。先到的佣兵重定位只作用于佣兵。

原版 Host 蛇殿两次手动施法可直接对包。旧记录 `artifacts/ai-campaign-rerun-20261102-124242/native-02-point-prefix/client.jsonl` 中，C2S 0x0C 的 seq 5091／5208 分别对应本人 S2C 0x15 的 5097／5212，收发相隔约 198／216 ms。这是两次实际施法的间隔，包含出手和收发时间。故意断流或高延迟下的原客户端逐帧表现尚未对照。

未释放动作随移动／死亡／换区取消；已释放弹体可在施法者死亡后完成，玩家离开或区域代次变化时清理。持续Inferno不能将每帧续射的结束刻误投影为站立，原0D只在动作真实结束后发出。受控Hydra不发敌怪经验／掉落；三头生命周期、337弹体及强化沿各自领域。

hosting独立prepareHydraRules从当前MPQ准备三头／三难度基础、初始技能槽、生命及模式表、普通／L-AC比例、LoD区域等级和PetType；服务端不读取原表。原单位初始化后先赋Hydra自身技能，再安装主人原Passive／Aura／Calc1／SumSkill，因此火焰精通为召唤时的基础贡献。共用prepareSummonStatSources，关闭SetSummonBaseStats和召唤抵抗；初始生命按生命表投掷并保留raw单位。当前三头生命表为零；maxhp的fMin／MinAccr／ValShift不代表缺项查询最小生命，keepzero关闭时SetStat(0)移除字段，缺项仍返回零。实体通过modeAlive保持活动，死亡模式清除该标记；参与者查询、动作入口、死亡／到期、拥有者清理和复制开始区分原生命与存活，不人为安装256。

原AI用自身单位种子以60%请求337。头部以S2出生，动作结束进入中立，SummonPet安排出生后25帧的首次AI。期限由Param1／Param2按原signed32计算，AI回调到达时以严格大于到期帧判死。原Missiles关联技能62与发射技能337分开保存在MissileSkillDamage，发射从本头的当前等级及原始属性读取伤害、支配及长度。固定1.13c D2Game.dll（MD5 `978f6f74cd031bce6ecee602cd5988cc`）的 `A3610` 负责出生及宠物登记，`A37A9` 登记首次AI，`43F19–43F54` 依次赋自身技能、安装召唤贡献、写AI期限。

玩家与怪物共用逐头出生准备。每头从召唤主人所在房间按原偏移查房，执行−1／4搜索；成功出生才分配单位身份及单位种子，随后一次搜索可见此前成功头的碰撞。宠物超限按成功头逐次淘汰最早登记者，再登记新头。房间随机贷款和游戏种子随出生发布。三头全失败保留房间搜索消耗和已支付的起手费用，Do144返回失败。完整跨系统同帧交错与实机结果仍待核对。

三种装甲互斥；Frozen Armor响应近战伤害，Shiver Armor响应近战尝试；Chilling Armor在HitByMissile阶段按实际飞弹的ReturnFire资格反击，准确率／格挡／回避及基础扣血之前即可触发，落空仍可反击。反击弹体的创建随机流及公共事件边界见[公共持续效果入口](EFFECTS.md#当前持续效果入口)。Energy Shield先吸收再减免，以基础Telekinesis协同求耗蓝；生命／蓝／清盾成组提交。Enchant接受同区友方玩家、已接伙伴及真实NPC，赋予状态不表示NPC攻击AI已实现。

原服省略普通本人4C／4D；上述可重建地面程序及其火段也不按每次施法广播73。自研同步遵守该行为，其他可见玩家仍收到动作；客户端保留0x73视野同步及有界不同来源配对，不合并两次独立施法。原表CelFile=null且有Light的弹体仅产生光源，不制造替代贴图。

## 边界

- Teleport已接原落点搜索和已准备、步行相连的同幕邻区；未准备区域、完整同行者即时迁移／多人包序及全部地图组合尚未对齐。跨区弹体已由`missiles/rooms`迁移真实房间、坐标／路径与区域代次，完整创建来源、接触和跨区组合仍待同版本验收，见[飞弹](../combat/MISSILES.md)。
- Chilling Armor的完整远程AI、ReturnFire来源组合及客户端接触流程仍待对齐。
- 碎冰、爆炸密度、连锁在延迟／可见性变化下的时序、循环声音、三难度／全装备／协同等级／免疫和背压仍没有完整认证。
- 充能、装备触发、武器通用限制由[库存](../../modules/INVENTORY.md)及[GENERAL](GENERAL.md)维护；保存字段与校验边界由[SAVES](../../modules/SAVES.md)维护。晚入视野和客户端权限沿COMMON，不以本页30项入口扩张其他职业或宣称完整原版对齐。

## 原服代表技能与发包收敛

本机1.13c D2GS／PvPGN参考栈（本地RElesgoe适配构建），D2X客户端使用普通难度99级、30项基础等级1的SkillTestSor，角色来自D2DBS，未上传自研D2S。服务端消息与状态可作为所列原服行为参考；画面由D2X重建，不等于原版客户端画面，适配参考栈也不代表全部纯原版规则。

| 实际路径 | 观察与限制 |
| --- | --- |
| 直接与弹体技能 | 心灵传动真实仓库／传送点77／63开窗；Teleport位置／扣蓝；Blizzard怪物死亡；Chain Lightning目标生命比例128→0。Frozen Orb散射、Meteor标记／落地火、Fire Wall两侧展开及Blaze移动留火是该连接中的D2X画面观察，原版逐帧图形仍待核对 |
| Hydra／Inferno | 曾观察到头部351／352／353及4C技能337，以及Inferno开始、转向与停止。新版出生失败／事件顺序和原服续按请求节拍、持续伤害仍待实机复核 |
| 原包对照 | 常规本人4C／4D及所列地面程序重复73未出现；按PlrMsg::sub_6FC81D20、SUnitMsg::FirstFn／MISSILES_SyncToClient及CltDo26／28解释服务端通知与客户端重建职责 |

参考连接仍有未消费包；一次握手超时在重启D2GS后恢复，原因未确定。全30项原服参数、跨机器LAN、Linux、长期稳定性、PvP、完整充能／触发、极限召唤／背压、断线及像素／人工听音仍待对齐。
