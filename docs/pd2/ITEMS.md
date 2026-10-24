# PD2物品与属性差异

本页限定ProjectDiablo.dll SHA-256 `17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57`及配套MPQ。核心ImageBase为0x10000000，下文使用RVA；旧`pd2-core`完整目录的9c2b…地址不能直接套用。只静态解析，不加载核心或运行Hook。物品基础、词缀、暗金／套装、符文之语、配方和属性编号仍从当前MPQ准备，本页只记规则、入口及限制。

公共物品模型／保存边界见[物品目录](../gameplay/items/README.md)及[存档](../modules/SAVES.md)，已有网络／外观取证见[物品链复核](../resources/pd2-core/ITEM_FUNCTION_REVIEW.md)。近战溅射的接触程序归[技能](SKILLS.md#do163实际手位分配伤害与独立防御)；Open Wounds与Crushing Blow由本页维护。共同版本与证据口径见[版本与证据](BUILDS.md)。

## 暴击与致命攻击

当前核心`0x3048E0`合并武器掌握、属性337 passive_critical_strike及258 item_crit_chance，只掷一次百分比，封顶75；失败后才对属性141 item_deadlystrike掷数，封顶为75加属性210 item_maxdeadlystrike。第二分支判断的是原DS为正，即使调整后的上限非正也仍消费一次随机。

`0x3049C0`将成功类型分别转换为200加属性256 item_crit_multiplier、150加属性257 item_ds_multiplier的物理百分比；256／257除无参数值外，还经`0x37F760`匹配当前武器类型参数层。两种成功均写ResultFlags 0x2000；倍率为零或负不等于没有触发暴击。元素通道不随之加倍。

独立计算入口为`src/gameplay/combat/pd2/item_modifiers.cpp`，属性投影为`src/gameplay/stats/pd2/item_values.cpp`；普通版仍使用原掌握／被动／致命攻击依次掷数及双倍物理。主／副手数据按接触手位采样，不能将非活动手位的damageRelated属性合并进来。飞弹出生与接触是两个入口：当前D2Game `0x3A730`跳转核心`0x3A1380`，`0x3A734`的16字节NOP移除原版接触时的固定双倍。接触按当前主人、已分配技能及SrcDam／三个风系技能资格重新掷数，倍率只作用于物理；出生掷数不能因此删除。代码入口为`src/server/systems/combat/pd2/critical.cpp`，双持飞弹的完整手位组合仍待覆盖。

## 动态主人属性

`0x2FAA20`替换内部GetTotalStat路径，原包装器`0x3A0450`经指针全局0x5DECD4、getter `0x33AB00`、版本表0x4A8D94解析到配套D2Common `0x38A80`（D2MOO的STATLIST_GetTotalStat调用链）。它在查询时提供以下特殊值：

- 属性183 equipped_eth：要求实际主人为玩家或怪物，从当前库存链计数，经过装备位置及资格检查、原生无形判断；不是已解码所有物品中无形项的数量。位置helper `0x380C10`读取ItemData+0x45页及+0x44身体位置，排除身体位置0／11／12；换组必须使用实际活动位置。
- 属性184 missing_hp：要求实际玩家／怪物主人，最大生命为零返回0，否则以无符号生命／最大生命作double计算，截断`(1-current/max)*100`；不从客户端生命球像素或物品显示值推算。
- 属性478 inc_splash_radius：原值大于零且主人missing_hp、属性489 inc_splash_radius_permissinghp均为正时，加两者32位乘积并只封顶40。没有下限钳制；原半径为零不凭489产生半径。

`0x2FAE70`在op4／5求值时通过上述查询取真实主人的basis。当前ItemStatCost将484／487分别链接equipped_eth／missing_hp并作用于物品伤害百分比；不能把条件求值后的结果覆盖保存的系数。`0x2FAD10`在装备属性变化后向客户端发送相关物品属性更新，也不等于将条件结果永久写回物品。

代码中动态采样位于`src/gameplay/stats/pd2/owner_values.cpp`，活动无形装备计数位于`src/server/systems/attributes/pd2/item_values.cpp`。临时结果仅供当前查询／接触／公式使用，角色资源仍归CharacterRecord，物品保存原属性系数。装备资格交错、全部伙伴装备与极端回绕值还需逐项对照。

## 物品充能恢复

初次登记`0x3451E0`，充能回调`0x3452D0`，物品再生分派位于`0x3454xx`。属性422 item_replenish_charges非零时使用物品StatRegeneration事件；已有耐久／数量恢复分支优先，成功占用该分支时不再执行充能恢复。

回调从原生属性列表／FullStats中的第一条204 item_charged_skill层取当前值，按原属性编码比较当前数量与上限，不遍历补满全部技能。未满时只加1，保持上限及技能层；有玩家主人时发送原3E属性增量。间隔为`2500/rate+1`帧，特殊PvP区域157／159／166先将商除2再加1；没有普通耐久再生的125帧下限。即使该层已满仍重新登记，不能拿满充能取消其他层或替换成另一层。

已复用物品原帧事件、库存事务及ChargeChanged原包；纯充能层操作在`src/gameplay/items/pd2/charges.hpp`，物品恢复入口仍由inventory／items所属系统持有。运行时钟不写D2S，保存数量保持原204编码。全部套装／符文之语复合充能层及携带位置组合尚待覆盖。

未鉴定物品的完整包不包含可分配的充能属性列表，但仍可能收到3E数量更新。PD2客户端此时校验数量边界，保留已解码物品，不把缺少属性层当成整件物品损坏；鉴定后以新的完整属性列表恢复对应充能层。

## 随机女巫技能属性

Properties函数25当前为`0x39EF20`，技能池初始化为`0x206F80`。池8按原顺序包含39、45、44、55、59、369、64、38、49、48、53、43、36、47、46、51、56、383、62、376。使用物品的PD2单位随机流取模20，写107技能加成及200施法触发。加成取par；触发概率取min，所选技能SrcDam为正时乘67再除100；触发等级取max减去有符号截断的`(reqlevel-6)/6`。技能层仍为`skill<<6 | rank&63`。代码入口为`src/content/pd2/item_properties.cpp`，Ormus的这两条属性均保存为原生属性，读档不重新抽取。

Skills名称参数按PD2表链接的不区分大小写规则解析，例如Properties引用`Phasing Selfaura`，Skills记录为`Phasing SelfAura`。不能将未匹配的合法技能名称静默变成层0；修正只影响新属性准备，不迁移已有物品层。

Properties的fireskill／coldskill／ltngskill／poisskill／magskill通过函数21向126 item_elemskill写入对应元素参数层，再由函数3生成362–366镜像属性。有效技能等级继续读126参数层，不能把镜像再次相加。es_efficiency与eaglehorn_raven分别由当前Skills的Energy Shield calc2及Raven aurastatcalc1读取，属于表公式消费者。

## 抗性穿透与生命吸取上限

初始化`0x36AA90`把伤害表物理行PierceStatId改为425、魔法行改为358。指针经getter `0x31F250`、版本表`0x4A871C`解析到当前D2Game `0x102AB0`，不是仅供Open Wounds使用的表。代码在`src/server/systems/combat/pd2/item_pierce.cpp`，玩家和非玩家伤害接收均按实际来源采样。

属性488由`0x2FC030`限制安全回血入口`0x306E80`的整次生命写入。条件使用无符号当前／新生命与最大生命的double比值；旧值或新值低于`(100-cap)/100`即可写入。因此一次吸取可以越过门槛，此后暂停；不能把每次结果钳成门槛，也不能未经依据推广到药水或所有治疗。纯规则位于`src/gameplay/combat/pd2/leech.hpp`。

## 装备触发与装备自带技能

当前`0x36E240`在普通SrvDo前登记dospellcast，5帧后采样当时的物品属性；`0x302930`在盾格挡或武器格挡后登记doblock，延迟2帧。它们通过`0x36AED0`登记原MonsterModifier事件，`0x36AE60`按事件号、单位类型及GUID恢复目标，再调用`0x347670`。目标飞弹等不支持的类型回到自身；空目标不是自身。普通施法另排除城镇和`0x37B650`的PvP安全区；后者是Level157／159的固定全局坐标矩形，不是DT1 NoHarm瓦片。该资格已在PD2施法准备中接入，扩展布局可由管理旅行进入；正常PvP／BR入口和胜负程序仍未实现，详见[特殊PvP区域](MAPS.md#特殊pvp区域)。

施法事件通常要求SC，或序列6／12／18的SQ。`0x190E20`初始化强制集合242、251、256、261、262、313、272、276、392；`0x190E90`初始化排除集合277、57、219、220、514、217、218、513。每次普通Do均可登记，物品／光环强制Do不递归触发此事件。ItemStatCost中的docrit／dopierce回调槽可登记，但当前核心未发现实际发送事件16／17的入口，也没有当前物品使用它们，不能自行加进暴击／穿透循环。

施法触发使用ItemEffect替代程序；攻击类是否使用替代程序还取决于ItemTgtDo。当前183（核心`0x3B2BF0`）以实际目标或自身为中心发射64向环；187（`0x3B3420`）要求真实目标并抑制来源物品事件；188（`0x3B3530`）走Skills_CreateMissile；151保留零售Chain Lightning替代程序从目标坐标出生的行为。ItemTgtDo由原主人掷概率，再由受害者作为实际施法单位；ItemTarget3按原AuraFilter0x1002、范围10寻找第一个可用尸体，不能额外消费随机数。已准备的Hydra／Bone Prison起手程序可由ItemCheckStart使用，其他未准备的起手程序明确拒绝。独立执行仍归skills／reactions的PD2目录。

普通玩家Do的5帧路径已接入；`0x345520`还含喷射状态中按FCR周期登记、延迟4帧的独立路径，怪物的普通施法包装也有单独条件。这两条尚未完整恢复，不能用普通玩家施法通过代替认证。

属性191通过当前`0x36DE90`／`0x345DF0`安装及周期执行装备技能，保留原选中和使用技能。当前表包含Energy Shield、Chilling Armor、Burst of Speed、Blade Shield、Armageddon、Thunder Storm、Phasing的专用记录。准备入口为`src/hosting/pd2/equipped_skills.cpp`，状态及周期执行位于effects的PD2目录；雷暴／刀刃之盾不重复创建手动技能的第二条周期事件。相关技能的状态、回调、aura和周期元数据必须同时准备，不能只复制显示公式。

Thunder Storm的普通／装备记录均由本条Skills的srvmissilea准备伤害飞弹编号；只有状态或通用PD2程序的missiles数组，无法满足持续雷暴的独立出生入口。

Phasing及Blaze改变真实移动碰撞规则。核心`0x373980`／`0x346000`观察状态244／13的增删，`0x37EC90`把玩家测试掩码改为0xC09、非玩家改为0xC01，保留单位自身占位。包装器`0x309560`实际是配套D2Common ordinal10647的SetPathType，不是占位图案设置。服务端移动和客户端路径表现使用同一纯掩码规则`src/gameplay/rules/pd2_phasing.hpp`。

属性424在当前ItemStatCost同时绑定domeleedamage／domissiledamage的函数28。该回调不掷概率、不施法，命中后按当前属性值增加定点生命，再发overlay151；并非仅近战可用。实现位于`src/server/systems/reactions/pd2/item_heal.cpp`，其他非原生接触入口仍待覆盖。

## 飞弹命中间隔

核心`0x3A6E10`／`0x3A6EF0`使用目标单位的原事件15。玩家飞弹通常以主人GUID及飞弹class为键；单位标志0x80000改为`(-1,class)`，非玩家主人使用`(-1,-1)`。NextHit和NextDelay均非零才登记。这与原版目标上的单个JUSTHIT门槛不同。

ItemEffect183的环形helper `0x36BAA0`会设置0x80000。它不等于构造参数0x10000的“抑制来源事件”。D2X分别保存两项，并由effects／missiles的`pd2/hit_delay.cpp`使用实例帧事件队列持有期限，不写入存档。原版仍使用原状态门槛。

## 方块链与重掷

当前CubeMain启用op0、16、18、28、30、31。op16／18是第一个输入物品的属性比较，分别为小于等于及等于，value按ItemStatCost.ValShift作逻辑右移；比较包含镶嵌子物品。输入数量可跨多个材料堆叠分配，不能把一个堆叠等同一份材料。代码位于`src/content/pd2/cube{,_inputs}.cpp`。

主入口`0x365790`把ladder列作为程序模式：2完成后重新扫描当前方块，1／3要求存在未腐化的wss／cwss；模式2配方会在output b生成下一轮所需的材料。腐化概率、分段阈值、孔数及属性均取当前CubeMain，不另写概率表。`src/hosting/pd2/cube.cpp`在私有候选中连续准备各轮，再一次提交；未支持的后继程序不先消耗材料。模式3破坏结果保留原无形身份，再执行`0x3632D0`的额外腐化：按新稀有物品身份扫描当前表，以破坏分支value为下界，由独立CRT rand在剩余千分区间抽取并应用第一个更高阈值的属性，忽略这些属性的chance列。

CRT状态由实例持有，内容工作只借用期望状态和候选下一状态；提交复验随机状态，物品事务成功后才更新，背压及过期准备不消费第二次随机。公共crafting内容快照绑定实际读取时的人物revision，不能将帧内再生前的请求revision用于随后读取的快照；库存revision及全部材料仍独立复验。

op30在`0x365790`匹配后直接退出，是明确阻断配方，不能照output执行。op31转到`0x365200`，重建同一暗金／套装file index，保留升级后基底、孔与镶嵌物、基础防御／耐久／数量、原生需求百分比和shiny属性；当前准备入口为`src/hosting/pd2/item_reroll.cpp`。`0x3650F0`读取匹配op31行的输出属性，当前配套表的这些槽均为空，不应据其名称另造升级属性；未来含这些属性的表及全部镶嵌组合仍需核对。

## Open Wounds与Crushing Blow

Open Wounds Fn015为RVA `0x346400`，常量表`0x44E310={0,1,125,74}`，state62；属性189是openwounds_stack而非modifierlist_level。持续125帧，最多3层，未满时相加伤害率，满后只刷新全层共用期限。复用状态helper `0x36C020`保留原来源／信用；对应`src/server/systems/combat/pd2/wounds.cpp`，层数与伤害率不写入角色存档。

基础率40，按等级分段增量9／18／27／36／45，再加Deep Wounds乘5。PvP普通／噩梦／地狱分别取8／6／4百分比，并非除以8／6／4；没有state225时，血魔290使Deep Wounds的PvP比例减半；有state225时，该列表贡献的Deep Wounds取普通PvP比例的三分之一，其余部分用普通比例。目标物抗减来源属性425，免疫≥100不按普通穿透处理；负有效物抗减半、PvP封顶50，玩家所属宠物再取四分之一，PvP城镇率为零。MPQ属性代码`Deep-Wounds`的大小写不能导致准备时漏掉`deep_wounds`。当前未实现扩展首领935／936的镜像写入。

Crushing Blow Fn016为RVA `0x3469B0`，对应`src/server/systems/combat/pd2/crushing.cpp`。概率及效率包含当前手位无参数值与`0x37F760`匹配参数层；Smite另外取已用技能Calc3／Calc4。基础除数玩家／佣兵为10，普通怪8；首领／超级金怪不可搜尸时30，可搜尸分支为`(100-HP百分比)*10+70`，再加玩家数减1；远程乘1.5，效率以`1+efficiency/100`修正除数。生命商保持double直到原生CVTTSD2SI截断点，不提前取整；极端溢出及全部首领组合尚未认证。

## 实现和取证边界

普通表驱动物品、属性生成、权威效果、UI显示及原生保存是独立路径。解析器接受某条属性不代表存在玩法消费者。扩展地图／首领修饰、特殊掉落来源、上述未覆盖的触发及加工组合仍须分别接入和核对；当前代码不能宣称全部物品差异完成。共享／材料仓库属于本地宿主明确排除的独立范围，见[协议与存档](PROTOCOL_AND_SAVES.md#三种不同的范围)。

| 项目 | 当前边界 |
| --- | --- |
| 普通物品／属性 | 暴击、动态主人属性、穿抗／吸取门槛、充能及已列出的装备技能／方块程序已有消费者；完整数值、双持、伙伴、镶嵌及多人组合未全部认证 |
| 喷射与怪物施法触发 | 按FCR周期登记的4帧额外路径、怪物普通施法包装的条件尚未完整恢复，属于待实现项 |
| 起手与替代程序 | 已准备的程序按明确资格执行；其余ItemCheckStart及未恢复的替代程序拒绝，不能按原版猜测放行 |
| 地图及特殊来源 | 开图／区域生命周期、词缀全链、扩展首领与特殊掉落未完成，分别归[地图](MAPS.md)和[怪物](MONSTERS.md)维护 |

忽略目录`reference/pd2-cn-analysis/town-queries/pd2-item-{attributes,rules,dynamic,properties,pools}`保留当前函数、指令与xref辅助查询。复核以本页SHA、模块RVA、MPQ字段及源码入口为准，不依赖这些缓存永久存在；不将DLL、MPQ、反编译全集、截图、日志或存档纳入源码提交。
