# 怪物、首领与精英技能

更新：2026-10-17。本页负责非玩家敌对单位的技能入口与覆盖台账。身份、AI、人口、动作、数据契约及有限运行证据由[怪物目录](../monsters/README.md)链接负责页维护。第三至第五幕按[逐项接入](../monsters/ACT3.md#逐项接入)补齐，未完成项目不计为覆盖。

## 当前执行家族

| 使用者／能力 | 当前自研入口与边界 |
| --- | --- |
| 沉沦魔萨满 | 本族尸体复活、按BaseId链选择火弹、普通近战；不开放任意尸体复活 |
| 鸟巢 | 原序列出生、数量／间隔及耗尽死亡；巢子不重复给予经验／掉落 |
| 蜘蛛 | arach1至7的SpiderLay轨迹、蛛网／地面减速；生命周期与效果分开持有；本轮六变体的有限实机范围见第三幕 |
| 沙地跳跃者 | Leap（132）的怪物St40／Do77分支：起手原A1接触、落点／飞行和落地后退，UNINTERRUPTABLE／SKILL_MOVE与原A5结束同步；不追加玩家Leap范围击退，尚待统一构建／实机 |
| 骨Fetish | MonStats.deathDmg的死亡模式回调：原飞弹117承载物理范围伤害，原表等级生命／Ladder、难度倍率及单位随机；与火强化死亡效果分开，尚待统一构建／实机 |
| Fetish萨满 | FetishInferno（178）沿原St53／Do95：A1事件2、INFERNO起止、calc3续放、MonStats2收尾；原飞弹144以自身行伤害和实际等级执行；Resurrect2沿原序列复活。FetishAura不增加AI施放分支；萨满死亡重建另见第三幕，尚待统一构建／实机 |
| ThornHulk | MonFrenzy（215）：A2的原攻击事件，独立命中／伤害处理后才叠加monfrenzy；skill_frenzy栈数以技能等级为上限，持续时间及移动／攻击／其他动画加速均读Skills公式，沿公共状态计时到期移除；AI连击与普通攻击冷却见第三幕 |
| 吸血鬼 | vampire1至9准备自身Fireball／FireHead／Firewall／Meteor，Fn028按原表掩码选择；普通法术使用独立可射击目标，火墙／陨石使用主目标；冷却和动作核对见第三幕第07项，尚待统一构建／实机 |
| 安达利尔 | AndyPoisonBolt及AndrialSpray多次释放；毒伤、方向与原Clt／Srv偏移分开处理 |
| 血乌 | Quick Strike序列及zombie2召唤；普通弓箭与专用召唤分别准备 |
| 伯爵夫人 | 真实DS1路径节点上的FirewallMaker／地面火；缺节点不自造落点 |
| GargoyleTrap | 原序列发射、距离／轴向限制；是可击杀单位型陷阱，不是刺客陷阱或普通物件 |
| 精英主动／周期效果 | Amplify Damage、友方属性光环、Holy Fire／Freeze／Shock脉冲、低生命Teleport；只是精英来源入口，不表示对应玩家职业全部可用 |
| 精英受击／死亡效果 | 电强化延时／冷却与充能弹、冰／火死亡事件等由独立效果时钟处理；并非每项都是可施放Skills行 |

普通箭矢、尖刺、骷髅法师元素弹、近战与元素附伤已有共享程序，但A1／A2或MissA1／A2不一定对应独立Skills行。SkeletonRaise槽是复活动画，不归普通AI主动；关系见[第一幕](../monsters/ACT1.md)。第三至第五幕程序尚未全面迁入，任务死亡链和身份替身不能认证战斗技能。

## 第二幕技能逐项台账

当前MPQ74个可击杀身份的Skills槽去重30项；下表区分主动技能、动画声明和被AI关闭的槽。等级读取SkNlvl及MonsterSkillBonus，Duriel光环等级另从aip1读取。Skills以外的MissA1／A2和固有回调亦列于表后，不将其混算为技能行。

| ID／Skills原名 | 使用者／执行 |
| --- | --- |
| 10 Jab | 甲虫S1两击、Duriel S1三击；ln34伤害比例、命中加成，各释放帧独立命中 |
| 44 Frost Nova | Summoner；64个原环形方向及冷伤／冰冷时长 |
| 47 Fire Ball | Summoner；原fireball伤害与范围爆炸 |
| 49 Lightning | lightningspire第2槽；原arcanelightningbolt及等级 |
| 55 Glacial Spike | Summoner；原范围爆炸／冻结，难度冻结时长除数 |
| 72 Weaken | Summoner第5槽；目标中心半径、持续时间与damagepercent负值，状态事实同步 |
| 97 Smite | blunderbore／Duriel；A2伤害、命中、眩晕ln12上限250帧、命中类型0x65 |
| 107 Charge | Duriel；A2、ln34比例、额外近战范围、原速度加成、击退和0x70命中类型 |
| 114 Holy Freeze | Duriel；AI等级的原光环、周期脉冲及状态，独立于精英光环 |
| 132 Leap | SandLeaper；先A1后向起跳时选定的2×目标−起点跳跃，落点按原50格边界搜索空位，Run速度及skill_move；怪物落地不加玩家范围击退 |
| 156 Fire Hit | SandRaider；S1物理、命中及S1元素附伤，独立于A1／A2 |
| 157 UnHolyBolt | GreaterMummy／Radament；按BaseId链偏移魔法弹，不与普通A2毒弹混用 |
| 158 SkeletonRaise | 骷髅声明；继续通过实际ResurrectMode显示复活，不添加主动AI技能 |
| 159 MaggotEgg | rank＜5时rank只，否则min(12,5+(rank−5)/3)；原子怪、位置探测及孵化后终止 |
| 161 MagottUp | SandMaggot／Queen声明；检查占位、恢复受击资格及原上钻序列 |
| 162 MagottDown | SandMaggot／Queen声明；开始钻地时取消受击资格，释放时按当前生命10%恢复 |
| 163 MagottLay | SandMaggot／Queen声明；原朝向偏移、spawn子怪及S1出生，只禁经验 |
| 166 Swarm Move | 5步移动／路径回退入口；原Swarm使用Fn019，不主动施放此槽 |
| 167 Nest | crownest1至4：St49固定点预留／不可中断，原SQ事件4清预留并沿房间随机流出生，子怪NOXP／NOTC；成功预检即消耗一次尝试，耗尽NOTC死亡。sarcophagus保留独立AI入口 |
| 169 VampireFireball | vampire1；原vampirefireball弹体、范围爆炸及难度等级 |
| 170 VampireFirewall | vampire1／Summoner；两侧maker及中心／路径地火，目标落点 |
| 171 VampireMeteor | vampire1声明；延迟中心、等级伤害／半径、原18处地火及持续帧；MPQ掩码3不自然调用 |
| 172 GargoyleTrap | 原机关射线、MonSeq释放与轴向资格，复用已接入口 |
| 182 Bestow | GreaterMummy／Radament；支持目标资格及原公式，当前calc1／calc2空值导致0治疗 |
| 203 DesertTurret | firetower；环境控制器原三发火弹及间隔，复用已接入口 |
| 204 ArcaneTower | lightningspire；64方向lightningtowernova及两阶段次数／间隔 |
| 216 QueenDeath | Queen死亡回调／54行死亡序列，八道queenpoisoncloud；不是活体AI施放 |
| 327 Resurrect2 | GreaterMummy复活lUndead；Radament支持两类亡灵及普通难度重要尸体，动态碰撞、原复活模式及满生命，禁重复奖励 |
| 333 VampireMissile | vampire1；原FireHead弹体与来源生命恢复 |
| 352 SerpentCharge | ClawViper；A1伤害、原速度加成、额外范围、击退与0x70命中类型 |

普通A2、骷髅法师元素弹、投矛／投瓶从MissA1／A2准备；绿色瓶子按原八方向产生rancidgascloud及SubLoop时长，橙瓶当前没有有效Srv爆炸半径，不自造范围伤害。甲虫GH／死亡的buglightning与虫后死亡的queenpoisoncloud各为四个基方向×两条ChargedBolt路径；固有计时不覆盖精英电／冰／火词缀。

本批源码已接移动序列回绕、飞行／栖息／集群AI，技能声明与AI启用条件分开保留。Windows包身份见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)，历史有限冒烟见[第二幕](../monsters/ACT2.md#有限运行证据)，不据本表宣称三难度完整战斗验收。

## 代码与归档

Mosquito（206）：`mosquito1`至4使用原SQ事件1，起手按双方GUID独立抽取循环预算；每次释放先按物理伤害的calc3回血，再结算自身随机流生成的物理、毒和法力／耐力消耗，离开近战范围终止吸取。序列回绕遵循原Param1和剩余帧计数，未把预算次数直接当作实际命中次数；本轮实机核对待统一构建后进行。

Submerge（176）／Emerge（180）：青蛙恶魔的隐藏和现身声明。目标资格与地面碰撞分别更新；Submerge在原序列零帧暂停，不阻断AI定时；现身前沿原1C09空位恢复入口处理位置和地面碰撞。远程显示读取自己的原序列与S1图形，本轮实机核对待统一构建后进行。

MonBow（339）／MonFireArrow（340）／MonExplodingArrow（342）：`cr_archer6`至8按Fn035的槽顺序选择，A1事件2分别释放cr_arrow6、firearrow和explodingarrow，技能ID／等级随原飞弹构造传入公共伤害与碰撞执行器；St4仅检查射击资格，不提前发射。本轮三种箭的实机核对待统一构建后进行。

第五幕持矛罗格的Jab（10）读取自己的seq_crlancerjab及A1物理参数；MonPowerStrike（344）／MonIceSpear（347）沿公共St6起手分配、Do2事件结算，读取各自雷／冰伤和持续时间。Fn036按存在的技能槽依次抽取概率，冲近后清除强制攻击标记；本轮实机核对待统一构建后进行。

[monster_actions_content.cpp](../../../src/hosting/monster_actions_content.cpp)及[act2_monster_skills.cpp](../../../src/hosting/act2_monster_skills.cpp)按当前MonStats槽、Skills、Missiles、MonSeq及难度准备规则；[skills/monster_actions.cpp](../../../src/server/systems/skills/monster_actions.cpp)持开始／释放队列，monsters执行复活／出生／移动，missiles／effects执行弹体和效果。AI只提交窄请求，不另建技能执行器。精英所有权见[精英](../monsters/ELITES.md)与[首领](../monsters/BOSSES.md)。

已有公共方向／路径计算及原Clt／Srv差异沿[技能COMMON](COMMON.md)和[怪物COMMON](../monsters/COMMON.md)维护。怪物调用职业技能时链接职业页，怪物专用程序才在本页登记算法；与物件、任务死亡演出、伙伴能力的分类见[目录](README.md)。

后续逐项补当前MPQ技能ID／原名、实际使用槽与AI条件、Clt／Srv程序、共享函数、执行和证据。当前表为代码执行家族摘要，不是全部非玩家Skills行清单；未归档行先列入[SPECIAL](SPECIAL.md)。历史代表路径有限V2及未认证分支以[第一幕证据](../monsters/ACT1.md#有限运行证据)为准。
