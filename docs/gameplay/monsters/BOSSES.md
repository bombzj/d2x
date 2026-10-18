# 首领与专用策略

更新：2026-10-18。本页维护专用首领／固定单位策略及任务控制与战斗AI的边界。精英rank与SuperUnique数据归[ELITES](ELITES.md)，普通家族归各幕台账，逐技能程序归[怪物技能](../skills/MONSTERS.md)。专用AI、幕首领和固定金怪不是同一个分类。

## 已接第一幕专用程序

| 程序 | 权威行为及数据 |
| --- | --- |
| Andariel | 四项MPQ AI概率；普通近战、AndyPoisonBolt、AndrialSpray原MonSeq九次释放。公共andarielSprayRay只计算原整数方向／起点；客户端保留subtile中心，服务端执行命中和毒伤 |
| BloodRaven | 原home边界、走跑／撤退／绕行、普通弓箭、Quick Strike独立序列；受击后限制远距离射击，绕行失败继续近距离撤退／射击分支；召唤zombie2采用原位置随机、难度数量上限，巢子NOXP／NOTC。地狱MonProp的knock从表准备，chance空白／0按原规则无条件应用（非地狱运行认证） |
| Griswold／Smith | 原近战／等待、Smith随生命变化速度；The Smith另保留固定SuperUnique身份／词缀 |
| Countess | 独立Countess AI，不改所有CorruptRogue；分别核对本人／目标与home的房间，回位使用原坐标距离；使用真实DS1 path nodes，逐点FirewallMaker／地面火，原700帧周期；没有路径节点不捏造火墙点 |
| GargoyleTrap | 原距离、轴向窗口、概率、射后恢复与等待；静止可击杀单位，原seq_gargoyletrap的event4释放。gargoyleTrapRay显式保留原Clt／Srv轴向差异 |

monsters持home、真实DS1技能位置、谱系和死亡连锁；[boss_decision](../../../src/gameplay/monsters/boss_decision.cpp)是服务端策略纯函数，skills持动作与多次释放，missiles／effects执行弹体和效果。A1无MissA1的技能用原event1（安达利尔毒弹／伯爵夫人火墙），远程A1用event2；Nest／Gargoyle及对应序列沿原事件。方向共享但保留Clt／Srv差异，原函数证据见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)。

血乌与安达利尔死亡连锁采用QuestsFX的范围、延时和undead筛选；连锁本身不授予任务奖励或首杀完成。任务资格见[第一幕任务](../quests/ACT1.md)，历史技能201／164／167／172代表动作及Countess未认证边界见[第一幕有限证据](ACT1.md#有限运行证据)，不重复维护运行记录。

## 第一幕死亡表现

RemoteScene按原1.13c D2Client RVA20FB0消费新鲜DEATH动作，不从已入场尸体推造历史演出；重复死亡通知只触发一次，原死亡图形在首领进入视野时提前准备。同区资源长帧保留已启动的死亡程序与子效果；换区／换局仍清空，不重播过期死亡。安达利尔52230创建当前MPQ `andycontrol0`，血乌52320创建`brdeathcontrol`（原50帧启动计时）、`brdeathspirits1`与`brdeathsmokes1`。现有公共客户端弹体队列执行烟雾／灵魂原ExplosionMissile链、CltDo21／22的闪电、CltDo27的游走火墙，以及CltDo29的原火柱／落石选择；图形、寿命、链接和Clt参数读取MPQ。Andariel火柱使用显示像素位移，不产生伤害／碰撞或服务端单位。

控制器只持25Hz显示时钟和独立随机流；权威死亡连锁仍由monsters／effects结算。完整像素粒子随机流、原相机震动和全部落石残片变体未逐帧认证，其他幕死亡程序不在本批范围。2026-10-11最终包代表死亡／重复通知／清理的有限观察见[第一幕](ACT1.md#有限运行证据)，不认证正常玩家击杀或全部难度的死亡连锁。

## 其他幕与特殊首领

| 范围 | 当前实现与明确缺口 |
| --- | --- |
| 第二幕Duriel及任务固定目标 | 已接Duriel冲锋／Jab／Smite／光环、Summoner五槽、Radament支持技能和虫后死亡毒云；第二幕新增AI／动画接线及有限运行范围，见[第二幕](ACT2.md) |
| 第三幕Mephisto／议会 | 已接Mephisto专用AI及六技能、议会治疗／Hydra；具体源码边界见[46／72项](ACT3.md#逐项接入)，正常首领战斗与全难度未完整验收 |
| 第四幕Izual／Hephasto／Diablo | 衣卒尔Fn055及新星、Hephasto沿Smith已接；Diablo第73项仍在补且保留替身，封印组不等于完整精英验收，见[第四幕](ACT4.md) |
| 第五幕古代人／Baal及五波 | 开战／重置、波次与任务完成控制已接；专用战斗AI、全部词缀／召唤／清理仍待补，见[第五幕](ACT5.md) |
| 牛王、Uber／Pandemonium与其他扩展首领 | UberIzual的Fn144／冰甲／传送已接；第一幕固定Cow King及单个扩展首领不等于事件完成，其余首领程序与运行证据仍待补，见[SPECIAL](SPECIAL.md) |

第三至第五幕可能使用已授权替身，真实任务身份保留不等于真实首领战斗完成。管理击杀验证死亡后的任务链，不能认证Boss AI、技能组合、伤害平衡或正常通关；具体历史条件只维护在[第二幕任务](../quests/ACT2.md#有限运行证据)和[第三至第五幕任务](../quests/ACT3_5.md#依据与运行证据)。

后续每个首领登记真实MonStats／SuperUnique、生成条件、专用AI、技能与序列、阶段／召唤／死亡演出、难度、奖励与任务依赖。尚未完成的原技能不回落普通攻击，世界／任务控制器不承担首领战斗规则。
