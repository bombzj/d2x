# 怪物、首领与精英技能

更新：2026-10-11。本页负责非玩家敌对单位的技能入口与覆盖台账。身份、AI、人口、动作、数据契约及有限运行证据由[怪物目录](../monsters/README.md)链接负责页维护。第二幕已补技能执行与家族AI／动画，最终包及有限运行证据见第二幕专题。

## 当前执行家族

| 使用者／能力 | 当前自研入口与边界 |
| --- | --- |
| 沉沦魔萨满 | 本族尸体复活、按BaseId链选择火弹、普通近战；不开放任意尸体复活 |
| 鸟巢 | 原序列出生、数量／间隔及耗尽死亡；巢子不重复给予经验／掉落 |
| 蜘蛛 | SpiderLay轨迹、蛛网／地面减速；生命周期与效果分开持有 |
| 吸血鬼 | vampire5的火球／FireHead；vampire1额外准备火墙／陨石，按MPQ掩码选择，掩码3未开启陨石 |
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
| 167 Nest | crownest3／sarcophagus；MonSeq出生事件、上限与耗尽，子怪NOXP／NOTC |
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

本批源码已接移动序列回绕、飞行／栖息／集群AI，技能声明与AI启用条件分开保留。Windows最终包身份与有限冒烟统一见[第二幕](../monsters/ACT2.md#有限运行证据)和基线，不据本表宣称三难度完整战斗验收。

## 代码与归档

[monster_actions_content.cpp](../../../src/hosting/monster_actions_content.cpp)及[act2_monster_skills.cpp](../../../src/hosting/act2_monster_skills.cpp)按当前MonStats槽、Skills、Missiles、MonSeq及难度准备规则；[skills/monster_actions.cpp](../../../src/server/systems/skills/monster_actions.cpp)持开始／释放队列，monsters执行复活／出生／移动，missiles／effects执行弹体和效果。AI只提交窄请求，不另建技能执行器。精英所有权见[精英](../monsters/ELITES.md)与[首领](../monsters/BOSSES.md)。

已有公共方向／路径计算及原Clt／Srv差异沿[技能COMMON](COMMON.md)和[怪物COMMON](../monsters/COMMON.md)维护。怪物调用职业技能时链接职业页，怪物专用程序才在本页登记算法；与物件、任务死亡演出、伙伴能力的分类见[目录](README.md)。

后续逐项补当前MPQ技能ID／原名、实际使用槽与AI条件、Clt／Srv程序、共享函数、执行和证据。当前表为代码执行家族摘要，不是全部非玩家Skills行清单；未归档行先列入[SPECIAL](SPECIAL.md)。历史代表路径有限V2及未认证分支以[第一幕证据](../monsters/ACT1.md#有限运行证据)为准。
