# 阶级、精英词缀与超级暗金

更新：2026-10-09。本页负责rank、冠军／暗金／仆从、SuperUnique身份与词缀执行。专用AI归[BOSSES](BOSSES.md)，人口／刷新点归[POPULATION](POPULATION.md)，原包／视觉归[PRESENTATION](PRESENTATION.md)。

## 当前固定身份与准备

SuperUniques读取十三条第一幕记录：Bishibosh、Bonebreak、Coldcrow、Rakanishu、Treehead WoodFist、Griswold、The Countess、Pitspawn Fouldog、Flamespike the Crawler、Boneash、The Smith、The Cow King、Corpsefire。固定词缀、额外词缀、组数、名称、变换和TC由当前表准备；Flamespike的表存在不等于当前地图存在刷新点，不自造位置。普通池之外的中立NPC、牲畜和不可击杀墙面发射器不进入敌对人口，后者属于物件机关范围。

hosting按每个出生身份滚独立词缀，仆从继承已准备领队数值；不把已滚暗金规则按类型缓存。MonUMod的权重、排斥、类型限制、难度常数与MonLvl、DifficultyLevels、Skills共同准备生命／等级／经验、物理增伤、命中、抗性、速度、元素、法力燃烧、冠军变体及光环／诅咒。runtime只接纯值，不读MPQ。实际经验／掉落携带真实rank及rewardModifiers；首领任务资格、首杀TC、组队／人数规则另属任务／奖励领域，尚未完整接入。

## 当前运行效果

| 程序 | 权威行为及数据 |
| --- | --- |
| 数值／冠军变体 | Extra Strong／Fast、冠军数值、抗性／石肤及对应难度修饰 |
| 元素／资源 | 元素附伤、Mana Burn、Spectral Hit持久元素stat；真实数值与随机由权威准备／执行 |
| 多发 | Multishot与原NoMultiShot／NoUniqueMod限制，传播由missiles处理 |
| 光环／诅咒 | 范围Amplify Damage、友方属性光环、Holy Fire／Freeze／Shock敌对脉冲 |
| 传送／恢复约束 | 低生命Teleport、PreventHeal复验，不由客户端推算实际落点或恢复 |
| 察觉／等待 | 首次察觉音效与原等待条件 |
| 受击／死亡事件 | Lightning GH延时／冷却，Cold／Fire死亡事件，独立发生编号与时钟 |

monsters持Spectral Hit累计stat、谱系与死亡连锁，effects/monster_enchantments持每单位光环／诅咒／受击与死亡事件时钟。死亡触发与光环使用不同发生编号，可靠队列重试保留首次计划，具体公共事务要求见[COMMON](COMMON.md)。

词缀行为接线不表示所有排斥／权重／难度／目标组合已运行验证。Holy Fire／Freeze／Shock与Amplify Damage在此只登记精英来源，不能认证玩家圣骑士／死灵完整技能；技能来源边界见[怪物技能](../skills/MONSTERS.md)。原MonProp血乌地狱knock从表准备，chance空白／0按原规则无条件应用，但没有地狱运行认证。

## 原包与待补范围

AC的rank、hcIdx、词缀及nameSeed布局由[PRESENTATION](PRESENTATION.md)负责；不把0C生命触发位当rank。第一幕电／冰强化与固定身份准入的有限历史证据只维护在[ACT1](ACT1.md#有限运行证据)，普通区域管理生成不能证明自然刷新点。

全部五幕固定身份逐ID台账、全部随机词缀／冠军变体／继承组合、精英染色／完整随机名称、专用死亡演出与多人难度尚未认证。原rank奖励、首杀／任务TC选择和完整组队／NoDrop各有独立依赖，见[库存](../../modules/INVENTORY.md)、任务专题与总计划；当前MonStats／SuperUniques定义存在不算已实现。
