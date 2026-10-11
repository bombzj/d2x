# 第一幕怪物执行台账

更新：2026-10-11。本页负责第一幕实际类型与普通AI家族；精英规则见[ELITES](ELITES.md)，专用首领策略见[BOSSES](BOSSES.md)，客户端支持见[PRESENTATION](PRESENTATION.md)。

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

本次补缺：KB使用GH原图及基础WL时钟，走跑动画按原BaseId和协议速度；血乌受击后射击限制及绕行失败后的撤退／射击；长枪罗格步数与到达距离分离；伯爵夫人区分自身／目标所在房间及回位；SC技能时钟不使用攻击速度／冰冷减速（SQ仍沿序列）；普通／冠军萨满使用本队尸体，暗金萨满使用沉沦魔及萨满族群的原平方距离筛选并排除暗金／冠军尸体；复活模式按目标MonStats2；鸟巢耗尽自死标NOTC；精英诅咒、攻击光环和火强化爆炸纳入死灵召唤物／佣兵／转换单位的战斗关系。数值与资源继续从MPQ准备，新增运行态不写盘，准入规则升至v39，D2S仍为v96。

## 固定身份与尚未覆盖范围

额外六种类型已计入本页63类型，不能再叠加十三SuperUnique记录成为新的类型总数；固定怪物也可能复用普通家族。十三原表身份及地图刷新边界仅维护在[ELITES](ELITES.md)，Smith／Griswold／BloodRaven／Countess／Andariel／GargoyleTrap的专用执行仅维护在[BOSSES](BOSSES.md)。牛类与Cow King被本批计入，但普通区域准入不能替代牛场完整事件认证。

63类型／25类AI描述本批源码接入，完整Vision／活动房间／动态拥挤、全局随机顺序、全部难度与精英组合、多人任务／经验／掉落仍有缺口；血乌／安达利尔死亡表现和精英姓名／调色的已接范围及认证边界分别见BOSSES与ELITES。逐MonStats身份与三难度参数准备的静态台账已补齐；区域与三难度逐分支运行验收尚未完成，不能把家族或身份表扩张为完整逐分支验收。

## 有限运行证据

2026-10-11第一幕批次的Windows Release历史包为规则v39／D2S v96，客户端SHA256为`253F3AE1D28991F8BC7501B14EF9A7BC175C0D14B0C4756F1BE84158C329FA70`，独立服务端为`CAF7AF871442667A2AFE1F6F8965CD8737FDE11E4C36437BE984006087190450`。当前包见[基线](../../../BASELINE.md#当前运行包与验证边界)，下面的证据不认证后续规则版本。92级女巫隔离角色、普通单机、当前MPQ；只用已有资源工具、游戏及调试管道，无新增测试脚本／用例／专用程序。证据目录为`artifacts/act1-audit-20261011/smoke`：

| 第一幕v39包证据 | 实际观察／边界 |
| --- | --- |
| `final-identities.json`、`final-identities-summary.csv`及逐ID JSON | 63类型逐项生成后推进40帧，记录真实规则、单位模式／技能及原姓名；63项均成功，原图显示缺失0。管理击杀只用于清理条件，不认证正常玩家击杀／掉落概率或每条随机分支 |
| `final-superuniques.json` | 13个固定身份全部准入，The Countess为AI22，真实姓名／调色显示缺失0；管理生成Flamespike不授予自然刷新点，Cow King不认证牛场开启事件 |
| `shaman-before.json`、`shaman-revived.json`、`shaman-client.json`、PNG | 普通fallenshaman1尸体完成死亡奖励后被Bishibosh复活，恢复生命7，rewardComplete保留；客户端原图正常，没有误用S1复活动画 |
| `knockback-active.json`、`knockback-client-confirmed.json`、PNG | 正常Telekinesis请求后牛怪处于KB原模式13，权威击退有效，GH原图可见，显示缺失0 |
| `countess-initial-step.json`、`countess-observed.json`、`countess-combat.json`、PNG | 原Level25自然Countess及四个DS1节点；依次收到四处原弹体通知，100帧时有29段331地面火，火墙原图实际显示，显示缺失0 |
| `nest-before.json`、`nest-observed.json`、`nest-exhausted.json` | crownest1出生6只后自行死亡，三次250帧观察中区域物品数均为141；记录耗尽NOTC边界 |
| `final-save.json`、`final-reload.json`、`final-reload-summary.json` | 保存、正常退出及新进程重入，姓名／经验／基础技能一致，宿主失败0、角色准入问题0。只认证隔离角色往返 |

最终客户端与独立服务端包均已更新；独立服务端只检查帮助启动，不据此认证DBS／PvPGN联网。原动画／参数静态核对涵盖三难度，运行仅限上述普通单机路径；多人、全部词缀／伙伴组合、全部随机顺序及逐像素速度比较未认证。既有`wraith_attack_1`声音中断淡出限制仍在客户端诊断中保留。

### 历史姓名与死亡表现观察

2026-10-11此前姓名／死亡表现包在`artifacts/act1-map-closeout-20261010/recheck`保留代表单机观察。35级死灵角色副本、普通难度、当前MPQ、种子2044627023，只使用已有EXE与调试管道，无新增测试脚本／用例：

| 当前包路径 | 实际观察／边界 |
| --- | --- |
| `verified-entry.json`、`verified-elites.json` | 正常营地→鲜血荒地，显示缺失0；Devil Sludge＋Aura Enchanted／transform14，Champion Skeleton／25，Rakanishu／29、Coldcrow／18，Blood Raven与Andariel／2。金／蓝标题截图保留于上级目录，最终包重复确认相同显示值 |
| `verified-blood.json`、`verified-blood-lightning.json`／PNG | 血乌死亡50帧后，一套284控制器、285闪电与286接触原图；293烟雾及296灵魂，原图实际显示，后续294／297烟雾／灵魂链正常。三条重复DEATH通知不重复创建控制器 |
| `verified-andy.json`／`verified-andy.png` | 一套307控制器、311火柱、308游走／309地面火；真实MPQ原图。`verified-tail.json`的显示弹体在寿命结束后清空，effectLimitations为空 |
| 探索保存 | 关闭地图继续记忆，手动保存、正常退出／重启及换种子隔离；准确计数和错误文件保留证据由[自动地图](../world/AUTOMAP.md#证据与限制)维护 |

首次包在读取新增怪物诊断时发生失效副本引用崩溃，报告保留于`artifacts/crashes/20261010-154534-611-92248-92352`；已改为帧内生成诊断值副本，最终包复测没有该崩溃。首领原死亡图形提前准备；同区资源长帧保留死亡程序，避免清除刚收到的死亡回调或已启动的子效果。没有把管理击杀视为AI／玩家正常击杀验收；本批不重测全部63身份、难度／词缀组合或联机，完整粒子随机／相机震动／落石残片逐帧仍未认证。

### 历史普通与全部怪物包

2026-10-09全部怪物批历史最终包为Windows Release、规则v19／D2S v96；EXE SHA256 `95624C814123FCA8436F8C524D4A2E5843C4A3B53E7088DC8681924F8675E945`，DLL `9F7B1FAF7AF453E3DC481F05207C76460AECC38FF33A50E92823032228B2969F`。最终增量构建无warning／error，早期诊断仍保留；当前运行包另见[基线](../../../BASELINE.md#当前运行包与有限冒烟)。只用现有EXE、原服与调试管道；资源、日志、截图、角色及参考代码保留在忽略目录。

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
| 保存／重入 | `final-before-save.json`／`final-reentry-saved.json`：人物92级、生命931／法力217保存恢复；原入局ProtocolReady，短时状态不恢复。最终failures／ignoredPackets／unavailableUnits为0，characterIssues／mapErrors／effectLimitations及stderr为空 |

这些为有限V2；准入、空诊断和静态核对不能把所有怪物或P3标为V3。噩梦／地狱、全部状态组合／取消与背压、多人、Linux及长期运行仍未认证。
