# 第一幕怪物执行台账

更新：2026-10-09。本页负责第一幕实际类型与普通AI家族；精英规则见[ELITES](ELITES.md)，专用首领策略见[BOSSES](BOSSES.md)，客户端支持见[PRESENTATION](PRESENTATION.md)。

当前MPQ第一幕Levels的mon／nmon／umon池共57个身份；连同固定预设／任务怪的andariel、bloodraven、griswold、smith、skmage_pois3及gargoyletrap，共63个可击杀敌对战斗类型。19类普通AI之外接Smith、Griswold、BloodRaven、Countess、Andariel、GargoyleTrap，共25类程序。普通、冠军、暗金、超级暗金、仆从和幕首领保留各自身份与规则。迁入规范见[COMMON](COMMON.md)。代表路径的有限V2见本页证据，其余分支仍为V1；源码接线不代表所有随机／难度组合已认证。

| AI家族 | 当前MPQ身份 | 权威程序 |
| --- | --- | --- |
| Skeleton | skeleton1–3、hellbovine | 接近、A1／A2、等待 |
| Zombie | zombie1–3 | 察觉／受击及埋骨之地追击、游荡、A1／A2 |
| Brute | brute1–3 | 生命比例速度、绕行、A1／A2 |
| Fallen | fallen1–4 | 领队号令、族群指令、恐惧逃跑、A1／A2、S2 |
| CorruptRogue | corruptrogue1–4 | 难度距离、走跑、近战 |
| Goatman | goatman1–3、goatman5 | 接近、近战与等待 |
| CorruptLancer | cr_lancer1–3 | 走跑／冲锋标记、近战 |
| Wraith | wraith1–2 | 原墙穿越掩码、接近、近战／元素及资源伤害 |
| BloodHawk | foulcrow1–2 | 飞行、冲锋、撤退与近战 |
| Fetish | fetish1 | 接战计数、按目标生命逃跑／再接战 |
| QuillRat | quillrat1–4 | A1近战、A2尖刺、SEIS额外针刺、撤退 |
| CorruptArcher | cr_archer1–4 | 箭矢、距离走跑、撤退失败继续原决策 |
| SkeletonBow | sk_archer1–3 | 箭矢、绕行、步数与目标距离 |
| SkeletonMage | skmage_fire1–2、skmage_ltng1–2 | 元素弹、撤退及失败射击、距离／绕行 |
| Bighead | bighead1–4 | 生命阈值、A1／A2、原火／毒等弹体 |
| FallenShaman | fallenshaman1–4 | 号令、本人族群尸体复活、类链火弹、近战 |
| FoulCrowNest | crownest1–2 | 静止Nest序列、出生次数／间隔、飞禽准入及耗尽死亡 |
| Arach | arach1 | 近战、生命阶段、SpiderLay移动轨迹／地面减速 |
| Vampire | vampire5 | 火球范围命中、FireHead命中恢复、生命阶段与撤退 |

vampire5三难度aip5仅开启位1；表中Firewall／Meteor槽的位2／4未开启，普通AI不执行。骷髅／法师的SkeletonRaise是复活动画，非普通AI主动技能；Countess相关槽不作为普通罗格程序调用。其他原行／精英须另准备条件，不能根据已有槽位宣称支持。

## 固定身份与尚未覆盖范围

额外六种类型已计入本页63类型，不能再叠加十三SuperUnique记录成为新的类型总数；固定怪物也可能复用普通家族。十三原表身份及地图刷新边界仅维护在[ELITES](ELITES.md)，Smith／Griswold／BloodRaven／Countess／Andariel／GargoyleTrap的专用执行仅维护在[BOSSES](BOSSES.md)。牛类与Cow King被本批计入，但普通区域准入不能替代牛场完整事件认证。

63类型／25类AI描述本批源码接入，完整Vision／活动房间／动态拥挤、全局随机顺序、全部难度与精英组合、特殊死亡演出、多人任务／经验／掉落仍有缺口。后续补逐MonStats身份、区域与三难度行，不能把家族表扩张为完整逐分支验收。

## 有限运行证据

怪物历史包身份见[基线历史怪物证据](../../../BASELINE.md#历史第一幕全部怪物证据)，当前运行包另见[基线当前包](../../../BASELINE.md#当前运行包与有限冒烟)。只用现有EXE、原服与调试管道；资源、日志、截图、角色及参考代码保留在忽略目录。

| 历史证据 | 覆盖与边界 |
| --- | --- |
| `artifacts/act1-monsters-20261008` | 57普通身份准入、19类AI代表动作、萨满复活／再次死亡、巢生、蛛网、吸血鬼远程及保存重入；普通难度92级女巫，不等于逐分支验收 |
| `artifacts/act1-monsters-closeout-20261009` | 最终普通包57身份准入、区域三组合／13类型容量、盾牌骷髅BL、人物0D／19、第二鸟巢耗尽及蛛网来源死亡后到期；保存重入无短时状态 |
| 同目录 `native-melee-ranged.json` | D2GS 1.13c fallen1 A1／A2／S2及quillrat1 A2；1级人物死亡，ignoredPackets=43（入局11），不能称成功击杀或完整原服回归 |

2026-10-09第一幕全部怪物批次的历史最终包证据在`artifacts/act1-all-monsters-closeout-20261009`：

| 路径 | 实际观察／边界 |
| --- | --- |
| 入场／准入 | 该批最终EXE正常营地→鲜血荒地；`final-admission.json`及`final-spawn-snapshot.json`：六种额外战斗类型和全部十三固定超级暗金成功；只证明该区域普通难度准入，不证明自然刷新位置 |
| 首领代表动作 | `final-reaction-boss.json`：Andariel技能201喷毒／164毒弹、BloodRaven技能167、GargoyleTrap技能172；权威生命变化及客户端原动作。未认证全部随机分支、血鸟召唤／死亡连锁；调试Countess没有DS1路径节点，不以准入认证其火墙 |
| 电／冰强化 | `final-reaction-client.json`、`final-reaction-server.json`、`final-reaction-cold.json`：Rakanishu一次GH的69生命81及后续0C触发209，无快照重复GH；八条195。Coldcrow死亡第五步时三十二条194；`final-cold.png`可见电弧及冰环，客户端原死动画自行生成64方向。后续命中观察冷却清触发，不推造隐藏等级 |
| 正常玩家战斗 | `final-reaction-pve.json`：原3C确认右手44、原C2S施放；brute2生命24→20.859375及chilledUntil，伤害和状态由权威执行。没有只靠管理击杀认证普通攻击 |
| 保存／重入 | `final-before-save.json`／`final-reentry-saved.json`：人物92级、生命931／法力217保存恢复；原入局ProtocolReady，短时状态不恢复。该批诊断和stderr见基线 |

这些为有限V2；准入、空诊断和静态核对不能把所有怪物或P3标为V3。噩梦／地狱、全部状态组合／取消与背压、多人、Linux及长期运行仍未认证。
