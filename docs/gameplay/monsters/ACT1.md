# 第一幕怪物执行台账

更新：2026-10-17。本页负责第一幕实际类型与普通AI家族；精英规则见[ELITES](ELITES.md)，专用首领策略见[BOSSES](BOSSES.md)，客户端支持见[PRESENTATION](PRESENTATION.md)。

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

## 三难度重新核对

2026-10-12重新提取当前`assets/mpq2`的Levels、MonStats／2、MonPreset、SuperUniques、MonLvl、MonUMod、MonProp、DifficultyLevels、Skills、Missiles与MonSeq。普通mon／umon、噩梦／地狱nmon，加固定／任务及递归spawn／minion重建集合：三个难度各63类，当前身份集合相同，参数分别准备。24个原AI名加Countess专用程序为25类。逐身份矩阵位于忽略目录`artifacts/act12-reaudit-20261012/identity-difficulty.csv`，全集沿用下表。

| 分支 | 普通 | 噩梦 | 地狱 |
| --- | --- | --- | --- |
| 普通怪等级 | MonStats.Level | 非boss／noRatio取区域MonLvl2Ex | 非boss／noRatio取区域MonLvl3Ex |
| 数值、元素、命中、经验 | 无后缀MonStats及MonLvl L列 | MonStats(N)及MonLvl L列(N) | MonStats(H)及MonLvl L列(H) |
| 技能／普通弹体等级 | SkNlvl／1 | SkNlvl+3／4 | SkNlvl+7／8 |
| 冷／冻结时长除数 | 1／1 | 2／2 | 4／4 |
| 随机暗金／固定暗金额外词缀 | 1／0 | 2／1 | 3／2 |

boss／noRatio保留各难度MonStats等级；密度、精英组数、固定仆从数和TC各读当前难度。冠军、暗金、仆从变体见[精英](ELITES.md)。

CorruptLancer、SkeletonMage及Bighead低生命接近使用原单位距离停止条件，不再将WithSteps截短路线或混用WalkInRadiusToTarget。骷髅弓手／法师及萨满分别保留主目标与可射击目标，法师撤退失败仍射主目标；Bighead只用射击回调判断资格，实际攻击仍指向主目标。绕行使用原方向优先级±2与逐格碰撞；同区域近房间取原六tile间隙条件，依据见[COMMON](COMMON.md#移动与目标依据)。

源码／数据核对不等于完整运行认证。Vision缓存、跨区域完整活动房间、动态单位占位、全随机流及所有自然精英组合仍未认证；最小运行范围见本文有限证据。

## 三难度核对后的最小冒烟

2026-10-12历史有限观察位于忽略目录artifacts/act12-reaudit-20261012/smoke，包身份见同级package-hashes.json及[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)。该批结果不认证后续源码。

| 场景 | 实际观察 |
| --- | --- |
| 普通第一幕冰冷之原 | 自然人口与骷髅法师真实准入；100固定步后法师移动／动作、人物受伤；原技能36火弹命中，法师生命15→11.18359375 |
| 普通第二幕干燥高地 | 自然人口与高等木乃伊真实准入；125固定步后追击／动作及人物受伤；原移动命令到达4818／5178 |
| 保存退出／新进程重入 | 正常退局返回CharacterSelection；新进程ProtocolReady，生命644.3359375、法力215、等级91及经验1800000131恢复，物品数量保持 |
| 两幕客户端／服务端 | scene.available=true，unavailableUnits=0，mapErrors／effectLimitations／characterIssues为空，failures=0；stderr为空 |

旧批次曾禁用重复名称cr_lancer8；当前源码已按MPQ编译表恢复原名称链接与数值类别，见[第29项](ACT3.md#逐项接入)。普通难度最小观察不认证噩梦／地狱、全部AI／词缀分支、自然通关、巢生边界或完整原版等价；独立服务端未启动PvPGN后端。

## 逐身份源码与原资源核对

2026-10-11直接从当前`assets/mpq2`提取MonStats、MonStats2、Levels、SuperUniques、Skills、Missiles、MonSeq、DifficultyLevels与AnimData，重建57项区域池＋6项固定／任务身份。以下每行均已核对真实身份、三难度AI参数准备、攻击／技能调用、原动作与组件路径；这属于源码／资源核对，不表示每行全部随机分支已运行验收。

表中459项已声明的普通动作对应166个不同COF，AnimData均存在；逐COF层和MonStats2变体核对6971项引用（含重复／nil），非nil组件缺失0。KB按D2Common COMPOSIT映射到GH，不要求不存在的KB图片；时钟按BaseId的WL AnimData与Velocity比例准备。八条相关MonSeq保留原帧、模式及事件；安达利尔喷毒为九次event2。原nil组件按空层省略，不补造图片。原资源与CSV索引保留在忽略目录`artifacts/act1-audit-20261011`。

| MonStats ID | hcIdx | AI | 原Token | 主动能力／槽位边界 |
| --- | ---: | --- | --- | --- |
| skeleton1 | 0 | Skeleton | SK | A1／A2 |
| skeleton2 | 1 | Skeleton | SK | A1／A2 |
| skeleton3 | 2 | Skeleton | SK | A1／A2 |
| zombie1 | 5 | Zombie | ZM | A1／A2 |
| zombie2 | 6 | Zombie | ZM | A1／A2 |
| zombie3 | 7 | Zombie | ZM | A1／A2 |
| bighead1 | 10 | Bighead | BH | bighead1 |
| bighead2 | 11 | Bighead | BH | bighead2 |
| bighead3 | 12 | Bighead | BH | bighead3 |
| bighead4 | 13 | Bighead | BH | bighead4 |
| foulcrow1 | 15 | BloodHawk | BK | A1 |
| foulcrow2 | 16 | BloodHawk | BK | A1 |
| fallen1 | 19 | Fallen | FA | A1／A2／S2 |
| fallen2 | 20 | Fallen | FA | A1／A2／S2 |
| fallen3 | 21 | Fallen | FA | A1／A2／S2 |
| fallen4 | 22 | Fallen | FA | A1／A2／S2 |
| brute2 | 24 | Brute | YE | A1／A2 |
| brute3 | 25 | Brute | YE | A1／A2 |
| brute1 | 28 | Brute | YE | A1／A2 |
| wraith1 | 38 | Wraith | WR | A1 |
| wraith2 | 39 | Wraith | WR | A1 |
| corruptrogue1 | 43 | CorruptRogue | CR | A1 |
| corruptrogue2 | 44 | CorruptRogue | CR | A1 |
| corruptrogue3 | 45 | CorruptRogue | CR | A1；Countess固定身份另用CountessFirewall |
| corruptrogue4 | 46 | CorruptRogue | CR | A1 |
| goatman1 | 53 | Goatman | GM | A1 |
| goatman2 | 54 | Goatman | GM | A1 |
| goatman3 | 55 | Goatman | GM | A1 |
| goatman5 | 57 | Goatman | GM | A1 |
| fallenshaman1 | 58 | FallenShaman | FS | Resurrect／ShamanFire（原类链） |
| fallenshaman2 | 59 | FallenShaman | FS | Resurrect／ShamanFire（原类链） |
| fallenshaman3 | 60 | FallenShaman | FS | Resurrect／ShamanFire（原类链） |
| fallenshaman4 | 61 | FallenShaman | FS | Resurrect／ShamanFire（原类链） |
| quillrat1 | 63 | QuillRat | SI | spike1 |
| quillrat2 | 64 | QuillRat | SI | spike2 |
| quillrat3 | 65 | QuillRat | SI | spike3 |
| quillrat4 | 66 | QuillRat | SI | spike4 |
| arach1 | 122 | Arach | SP | SpiderLay |
| vampire5 | 135 | Vampire | VA | VampireFireball／VampireMissile；AI掩码位2／4（火墙／陨石）关闭 |
| fetish1 | 141 | Fetish | FE | A1 |
| andariel | 156 | Andariel | AN | AndrialSpray／AndyPoisonBolt |
| cr_archer1 | 160 | CorruptArcher | CR | cr_arrow1 |
| cr_archer2 | 161 | CorruptArcher | CR | cr_arrow2 |
| cr_archer3 | 162 | CorruptArcher | CR | cr_arrow3 |
| cr_archer4 | 163 | CorruptArcher | CR | cr_arrow4 |
| cr_lancer1 | 165 | CorruptLancer | CR | A1 |
| cr_lancer2 | 166 | CorruptLancer | CR | A1 |
| cr_lancer3 | 167 | CorruptLancer | CR | A1 |
| sk_archer1 | 170 | SkeletonBow | SK | skbowarrow1 |
| sk_archer2 | 171 | SkeletonBow | SK | skbowarrow2 |
| sk_archer3 | 172 | SkeletonBow | SK | skbowarrow3 |
| crownest1 | 206 | FoulCrowNest | BN | Nest；耗尽NOTC |
| crownest2 | 207 | FoulCrowNest | BN | Nest；耗尽NOTC |
| bloodraven | 267 | BloodRaven | CR | Nest／Quick Strike／raven1 |
| gargoyletrap | 273 | GargoyleTrap | GT | GargoyleTrap（event4） |
| skmage_pois3 | 276 | SkeletonMage | SK | skmage1 |
| griswold | 365 | Griswold | GZ | A1 |
| skmage_fire1 | 383 | SkeletonMage | SK | skmage3 |
| skmage_fire2 | 384 | SkeletonMage | SK | skmage3 |
| skmage_ltng1 | 387 | SkeletonMage | SK | skmage4 |
| skmage_ltng2 | 388 | SkeletonMage | SK | skmage4 |
| hellbovine | 391 | Skeleton | EC | A1／A2 |
| smith | 402 | Smith | 5P | A1 |

SkeletonRaise在骷髅及fetish1的原技能槽中存在，但普通Skeleton／Fetish AI没有主动调用分支；不能据槽位存在给它们新增主动复活。vampire5的三难度aip5均为1，保留Fireball与FireHead程序，不擅自打开原掩码关闭的火墙／陨石。

本次补缺：KB使用GH原图及基础WL时钟，走跑动画按原BaseId和协议速度；血乌受击后射击限制及绕行失败后的撤退／射击；长枪罗格步数与到达距离分离；伯爵夫人区分自身／目标所在房间及回位；SC技能时钟不使用攻击速度／冰冷减速（SQ仍沿序列）；普通／冠军萨满使用本队尸体，暗金萨满使用沉沦魔及萨满族群的原平方距离筛选并排除暗金／冠军尸体；复活模式按目标MonStats2；鸟巢耗尽自死标NOTC；精英诅咒、攻击光环和火强化爆炸纳入死灵召唤物／佣兵／转换单位的战斗关系。数值与资源继续从MPQ准备，新增运行态不写盘，D2S仍为v96。

## 固定身份与尚未覆盖范围

额外六种类型已计入本页63类型，不能再叠加十三SuperUnique记录成为新的类型总数；固定怪物也可能复用普通家族。十三原表身份及地图刷新边界仅维护在[ELITES](ELITES.md)，Smith／Griswold／BloodRaven／Countess／Andariel／GargoyleTrap的专用执行仅维护在[BOSSES](BOSSES.md)。牛类与Cow King被本批计入，但普通区域准入不能替代牛场完整事件认证。

63类型／25类AI描述本批源码接入，完整Vision／活动房间／动态拥挤、全局随机顺序、全部难度与精英组合、多人任务／经验／掉落仍有缺口；血乌／安达利尔死亡表现和精英姓名／调色的已接范围及认证边界分别见BOSSES与ELITES。逐MonStats身份与三难度参数准备的静态台账已补齐；区域与三难度逐分支运行验收尚未完成，不能把家族或身份表扩张为完整逐分支验收。

## 有限运行证据

历史普通难度观察包括63战斗身份与13固定身份的管理准入、萨满复活、正常Telekinesis击退、自然Countess火墙、安达利尔代表技能、强化受击／死亡表现及保存重入。管理准入不证明自然刷新，管理击杀不证明正常战斗；噩梦／地狱、全部AI／词缀组合、多人／Linux及逐随机一致性未认证。包身份统一见[运行包记录](../../development/BUILD_AND_RUN.md#运行包记录)，这些结果不认证后续源码。

| 本地证据目录 | 适用范围 |
| --- | --- |
| artifacts/act1-audit-20261011/smoke | 逐身份／固定身份准入、萨满、击退、自然Countess及代表首领观察 |
| artifacts/act1-monsters-20261008、artifacts/act1-monsters-closeout-20261009 | 早期57普通身份、代表AI、组件／盾牌与保存重入；原服人物死亡记录不算成功击杀验收 |
| artifacts/act1-all-monsters-closeout-20261009 | 六种额外战斗类型、固定身份、电／冰强化、正常玩家法术及保存重入 |

### 历史姓名与死亡表现观察

历史观察覆盖原姓名／词缀／调色、强化受击／死亡以及首领死亡资源。当前实现和未认证的粒子／震动／残片边界见[表现专题](PRESENTATION.md)；源码修复不追认所有历史包。

### 历史普通与全部怪物包

早期包只保留上表证据范围，当前二进制身份与构建记录由运行包记录维护。本页不重复保存包哈希、交付过程或逐次快照日志。
