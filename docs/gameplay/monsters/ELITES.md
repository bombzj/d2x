# 阶级、精英词缀与超级暗金

更新：2026-10-16。本页负责rank、冠军／暗金／仆从、SuperUnique身份与词缀执行。专用AI归[BOSSES](BOSSES.md)，人口／刷新点归[POPULATION](POPULATION.md)，原包／视觉归[PRESENTATION](PRESENTATION.md)。

## 当前固定身份与准备

SuperUniques读取十三条第一幕记录：Bishibosh、Bonebreak、Coldcrow、Rakanishu、Treehead WoodFist、Griswold、The Countess、Pitspawn Fouldog、Flamespike the Crawler、Boneash、The Smith、The Cow King、Corpsefire。固定词缀、额外词缀、组数、名称、变换和TC由当前表准备；Flamespike的表存在不等于当前地图存在刷新点，不自造位置。普通池之外的中立NPC、牲畜和不可击杀墙面发射器不进入敌对人口，后者属于物件机关范围。

hosting准备MonUMod权重、排斥、类型限制、难度常数及不可变数值程序；已迁入的密度／预设／任务／物件精英由server从实际领队单位流选择词缀、决定随从并生成姓名，再异步准备选定规则，不在hosting另掷一份。仆从按原minion头插链、已有槽数及Xfer标志继承，原类型和出生身份保留。runtime只接纯值，不读MPQ。实际经验／掉落携带真实rank及rewardModifiers；首领任务资格、首杀TC、组队／人数规则另属任务／奖励领域，尚未完整接入。

普通A1／A2与原元素附伤按MonMode实际攻击时的单位等级重新准备，包含冠军／暗金／仆从等级加成，不沿用出生前基础等级伤害／命中。生命、防御、经验不一并重算。S1与技能借用A1／A2也使用当前等级攻击快照，之后应用词缀／人数／技能加成。依据为D2MOO MonsterMode的DATATBLS_CalculateMonsterStatsByLevel与当前MPQ三难度MonLvl。

## 当前运行效果

当前源码已将生命加成移到实际Actor出生：基础范围投掷与人数加成之后，在定点生命上执行UMod2，再应用Possessed／Berserk修正，复用原整数比例函数。怪物祭坛升级共用实际单位流和当前属性；普通暗金保留RollLimitedRandom(1)，已有八槽时不再掷冠军机会和该限定随机。追加冠军循环仅适用于密度生成及MonPlace3，在领队修饰器完成后决定数量；邪恶瓮、吉得宾及MonPlace2不执行该循环。追加冠军自身的普通party先于其冠军修饰器执行，且不随领队自动晋升为仆从。固定超级暗金保留全局Stacks限制、固定词缀、难度追加、特殊子体和最终任务修饰器。UMod41按原75帧修饰器事件更新AI回调，不伪造速度加成。逐修饰器跨整条minion链的原回调次序、完整属性副作用、原类型位及首领专用初始化仍未全部闭合。

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

特殊休眠恢复与实时出生共用修饰器准备和Actor发布：unique重新消费一次实际单位流后覆盖保存的nameSeed，minion不消费姓名随机；保留保存的词缀身份、阶级和主从链。恢复未完成的实体暂缓对客户端可见，完成后才恢复保存的生命与尸体资格。精英不再因身份被整类禁止压缩；原TYPE_OTHER／MINION寻位、光环初始化、全部特殊AI命令及逐修饰器链顺序仍需完整原版对照，未运行认证。

Amplify Damage、攻击光环及火强化爆炸使用统一战斗关系筛选怪物目标，涵盖佣兵、死灵召唤／重生及转换单位；友方属性光环只授予实际盟友，不能按owner空值误授敌对转换单位。暗金／超级暗金萨满可复活范围内普通／仆从沉沦魔及萨满尸体；普通／冠军萨满只检索本人族群，复活模式取目标MonStats2。

词缀行为接线不表示所有排斥／权重／难度／目标组合已运行验证。Holy Fire／Freeze／Shock与Amplify Damage在此只登记精英来源，不能认证玩家圣骑士／死灵完整技能；技能来源边界见[怪物技能](../skills/MONSTERS.md)。原MonProp血乌地狱knock从表准备，chance空白／0按原规则无条件应用，但没有地狱运行认证。

## 姓名、标签与调色

原服与自研客户端共用RemoteScene消费AC的rank、hcIdx、词缀与16位nameSeed。随机暗金沿master适配，用当前MPQ UniquePrefix／UniqueSuffix的TBL文本及公开nameSeed组合；冠军与Ghostly／Fanatic／Possessed／Berserker读取原标签和ChampionFormatX，仆从显示原Minion，固定超级暗金使用SuperUniques.Name。词缀说明只读已公开ID，不从生命高位猜rank。共享monsterDisplayName／monsterModifierDescription不滚玩法随机。

调色按本地原1.13c D2Client A7A5A／539E0／53AA5：普通TransLvl对应原palshift的第TransLvl+2行；暗金及勇士的UNIQUE位用classId+nameSeed重新初始化显示种子，选择9..38，原末端大于等于30回退2。MonStats2的难度Utrans优先，再用固定SuperUnique难度Utrans覆盖；大于等于8读全局randtransforms，0..7读怪物palshift原行。安达利尔2／3／4保留原难度调色，序列／普通动作／尸体共用缓存键。Utrans255按已公开宠物归属／队伍选择0／1，不认证完整PvP关系。

hosting的冠军AC同时保留CHAMPION与UNIQUE，符合MonsterUnique原TypeFlags，不把两位视为互斥。`online-world.scene.monsters`只读当前已解析姓名、词缀和实际transform供观察；不替代原截图或玩法认证。

## 原包与待补范围

AC的rank、hcIdx、词缀及nameSeed布局由[PRESENTATION](PRESENTATION.md)负责；不把0C生命触发位当rank。第一幕身份与程序见[ACT1](ACT1.md)，电／冰强化的原函数及Clt／Srv差异见[参考设计](../../architecture/REFERENCE_DESIGN.md#第一幕怪物公共计算依据)。普通区域管理生成不能证明自然刷新点。

全部五幕固定身份逐ID台账、全部随机词缀／冠军变体／继承组合及多人难度尚未认证。随机姓名组合是master同范围适配，不声称与原客户端全部命名随机顺序逐字一致；当前调色选择已按本地原版修正，完整截图矩阵仍未认证。原rank奖励、首杀／任务TC选择和完整组队／NoDrop各有独立依赖，见[库存](../../modules/INVENTORY.md)、任务专题与总计划；当前MonStats／SuperUniques定义存在不算已实现。
