# PD2与D2X逐函数等价性审查

本页逐项记录人工读取原函数和D2X消费者后的判断；每完成一个函数即更新对应条目，不以批量名称匹配代替分析。规则正文归各领域分册，后续任务归[补齐计划](GAP_PLAN.md)。当前目标为[BUILDS](BUILDS.md)的核心17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57；旧核心9c2b…只在明确标注的条目使用。规则结论仍以逐函数静态证据为准；已完成Windows Release构建及有限互通验证，范围见[协议与保存](PROTOCOL_AND_SAVES.md#当前互通验证范围)，不等于逐函数场景验收。

## 判断口径

“公共”指D2X跨原版／PD2或跨多个玩法复用的算法及状态能力，“专用”指PD2规则或特定程序；不等同PE导出／非导出。原生包装器、CRT和内存补丁无需逐字移植，但必须由固定配置、内容准备或领域执行实现对应契约。函数返回值相同不等于随机流、副作用、顺序及失败行为相同。

结论分为：**局部对应**（给定明确输入及依赖时算法对应）、**存在差异**（源码可指出不同分支或数值）、**证据不足**（依赖／调用者未闭合）、**无需移植**（平台安装机制由D2X架构替代）。没有全库完成率；未列出的函数尚未在本页逐项审查。

## F01 暴击种类判定3048E0

- **性质／原生依据**：PD2专用；当前3048E0–3049B0，原指令及条件见[函数契约](ITEMS.md#3048e0暴击种类判定契约)。依赖3067B0掌握、308030属性查询、3A0040随机。
- **D2X入口**：[rollPd2Critical](../../src/gameplay/combat/pd2/item_modifiers.cpp)；飞弹接触消费者为[preparePd2MissileCritical](../../src/server/systems/combat/pd2/critical.cpp)。
- **逐分支对照**：nativeAdd将被动、掌握和物品暴击按32位合并；只在正概率时掷第一下，上限75；成功返回种类1并短路。第二阶段以原deadly>0为门控，即使调整上限非正仍掷数，成功为2，否则空结果。200／150加对应倍率也是nativeAdd；未把两类成功叠乘。
- **副作用对照**：纯函数仅推进传入的随机引用；消费者先复制caster.random，计算后由事务或monsters.consumeRandom复验并提交，再缓存contact.pd2Critical，已缓存时不重掷。这是D2X背压语义，需核对调用重入时机，不能直接视为原生所有时序已还原。
- **差异／未知**：原函数内部选择武器掌握，D2X由调用者预先提供；3067B0的直接Throwable与求和差异已按F12修正；当前飞弹接触从玩家equipment.weapons[0]选武器，未由该代码证明与原生330B90实际手位相同。公共limitedRandom已在F06对上该调用所用的原3A0040，种子所有权、掌握和属性来源仍未闭合。
- **结论**：条件、种类、回绕加法及同种子百分比随机为局部对应；不能认证整条暴击链。先处理F02百分比差异，再审实际手位、属性视图、出生与接触两阶段，不能仅改概率值。优先级P0（公共依赖）／P1（手位及调用链）。

## F02 百分比helper37A6F0

- **性质／依据**：PD2跨玩法共享数值函数，当前37A6F0–37A734，证据`town-queries/current-functions/0x1037a6f0.json`。不是原版整数ApplyRatio的别名。
- **原指令**：ECX按有符号int转double；XMM2为倍率；EDX按无符号比较取至少1并转double。执行MULSD后DIVSD，结果超过RVA44DF20的double常量时返回INT_MAX，否则CVTTSD2SI向零截断。该常量为2147483647；COMISD后JA不接受unordered，因此NaN继续到转换，负溢出及NaN得到INT_MIN。负数先截断再判断可表示范围，不将其写成双向饱和。
- **D2X公共候选**：[nativeMultiplyDivide／nativePercentage／nativeStatPercentage](../../src/gameplay/rules/arithmetic.cpp)。前者先32位乘法回绕，第二个按原版整数阈值拆运算，第三个先算percent/100再乘且越界抛异常；三者均不能直接作为此函数的完整等价物。
- **实际消费者**：[missile_contact](../../src/server/systems/combat/missile_contact.cpp)的PD2暴击现改用独立[pd2MultiplyDivide](../../src/gameplay/rules/pd2/arithmetic.cpp)；[Teleport](../../src/server/systems/skills/pd2/teleport.cpp)的四类掌握扣减同样复用该函数。[targetWeaponChannels](../../src/gameplay/skills/weapon_damage.cpp)已按PD2标记选择同一helper；当前补丁表49B9A0–49B9D4及原D2Game DD52C→核心3A0060→303330→3049C0证实近战映射，详见物品分册。原版仍保留整数算术。
- **判别例**：输入20,000,000、倍率150、分母100，double乘除得到30,000,000；D2X中间3,000,000,000按int32解释为-1,294,967,296，再除100得到-12,949,672。这是静态算术反例，不声明已经在真实装备场景复现，也不依赖上限常量含义。
- **结论／步骤**：独立PD2函数、已核定飞弹／近战及Teleport消费者已修正，已构建。仍属P0部分完成：其他调用点、完整物理／元素顺序与随机消费需要分别闭合，不能由这些消费者的静态审查认证整个倍率链。

## F03 生命写入门槛2FC030

- **性质／依据**：PD2专用拦截，原生分支见[生命契约](ITEMS.md#2fc030生命写入门槛契约)。
- **对应函数**：[pd2LifeDrainWriteAllowed](../../src/gameplay/combat/pd2/leech.hpp)保留严格小于、候选或旧值任一满足就整次放行、最大值0用1，以及旧生命／最大生命的uint32转换。没有钳至门槛，普通非负cap下局部对应。
- **已修差异**：原属性488无符号转double，D2X已将double(cap)改为double(uint32_t(cap))。cap=-1、previous=850、next=900、maximum=1000时，旧代码阈值1.01允许；新代码按原4294967295得到负阈值并拒绝。已构建，真实MPQ、聚合、存档／网络是否可形成此值也未确认。
- **调用者**：[advanceMissileDrain](../../src/server/systems/combat/missile_drain.cpp)仅在存活且lifeGain>0时准备候选，先限最大生命再调用；玩家走CharacterEdit事务，怪物走MonsterResourceTrace及事实发布，drainCommitted防重复。非生命属性没有送进这个谓词，与原包装器直接委托非6属性在架构上可分离。
- **边界**：玩家cap来自totals.character.combat，怪物来自effects.combatModifiers，尚未证明与原“写入时查询”在同帧状态变化下相同；近战、佣兵及其他安全回血调用点尚未全量追踪。不能认定所有吸血已一致，也不应把该谓词加到所有药剂／治疗。
- **结论／步骤**：纯函数的高位解释已修正，调用链仍为P1未完成；继续核所有原安全回血调用者及D2X提交时采样。原版公共资源接口未改，不将本谓词推广到无证据的治疗入口。


## F04 充能回调3452D0

- **性质／依据**：PD2专用回调，依赖公共属性列表、物品主人、事件调度及通知。原输入和分支见[充能契约](ITEMS.md#3452d0充能事件回调契约)。
- **D2X实际入口**：[inventory::replenishItem](../../src/server/systems/inventory/replenishment.cpp)、[items::restoreGround](../../src/server/systems/items/system.cpp)；纯值辅助为[pd2FirstChargeLayer／pd2RestoreCharge](../../src/gameplay/items/pd2/charges.hpp)及[itemRestoration](../../src/gameplay/items/replenishment.hpp)。
- **已对应部分**：满充能仍rearm而非撤销；每次只加1；正速率间隔2500/rate后再加1，玩家特殊区域先将商减半；地面无玩家主人不减半。库存更新经事务，成功后重排事件；地面先发布事实再更新。没有重新抽技能或将运行时钟写入物品。
- **记录来源缺口**：原先要求标志40列表及FullStats均有204，使用前者首记录。D2X跨savedStats、runewordStats、savedSetStats选择最小parameter，没有显式检查两份运行列表。相同层可在不同保存列表重复；pd2RestoreCharge只改首个未满匹配项，pd2RestoreChargeValues却改所有匹配的已解析项。需要证明保存／运行列表折叠及激活条件，否则不能认定多列表等价。
- **编码已核**：当前pd2data的ItemStatCost BIN共511条、步长0x15C，204行+10的Multiply=256、+19的SaveBits=16、+30的Encode=3，低8位当前值／高8位上限与D2X对应。BIN摘要eea4bed2894fb094f695ba935b0178e6d953dc77518b140328cb812f92596cdf，证据charges-identity.json。原生包装器30C530→Common ordinal10195／384F0→37F40从pMyLastList逆链选第一张含0x40的列表；30D630→ordinal11037／3A790→3A1D0只更新指定列表的指定层，并传播差值。不能把FullStats的存在性门槛当成第二次恢复。
- **执行差异**：原rate非零即续订，D2X只接受charges>0；负速率可达性待核。D2X在trade.find(playerId)存在时仅续订，不恢复；原本回调没有交易门控，上游是否取消事件未知。3451E0已核只在没有事件3时初次登记，已有事件不因区域／速率变化重排；D2X restorationClock同样仅在hasEvent为假时插入，回调按当时区域重新求下次期限。这一分支不应改成跨区立即重置计时。
- **结论／步骤**：单层、正速率、非交易、已正确准备运行值时局部对应；多列表与调度边界证据不足，P1。依次核37A470／30C530／30D630的结构及排序，收敛选层和运行缓存更新，再审3451E0初次注册及交易／移物／伙伴生命周期。不能只修改间隔或遍历补满所有技能。

## F05 暴击倍率应用3049C0

- **性质／依据**：PD2专用，原生寄存器、返回与写入见[倍率契约](ITEMS.md#3049c0倍率应用契约)，依赖F01判定、F02缩放、37F760参数层和330B90实际武器。
- **D2X映射**：[preparePd2MissileCritical](../../src/server/systems/combat/pd2/critical.cpp)准备结果，[missile_contact](../../src/server/systems/combat/missile_contact.cpp)写入rolled.critical及channels[0]；[weaponCritical／targetWeaponChannels](../../src/gameplay/skills/weapon_damage.cpp)承担近战计算。不存在必须照搬的单一寄存器包装。
- **对应部分**：kind而非percentage控制成功，零／负倍率仍保留critical；只有物理通道被缩放；[applyPd2WeaponParameters](../../src/gameplay/stats/pd2/item_values.cpp)将无参数倍率与匹配武器类型层分开相加。输入无有效参与者时由领域资格拒绝，替代原指针判空，但完整上游行为尚未全审。
- **确定缺口**：飞弹及近战倍率已改为F02的PD2浮点缩放，近战安装／桥接链见物品分册；飞弹输入固定玩家武器槽0，原330B90选择语义尚未闭合，不能认证双持／切组。出生路径还单独保留种类标志，是否恰好按原生两阶段消费随机需审出生消费者，不能简单删除出生掷数。
- **结论／步骤**：非完整等价，P0先修经核定的数值依赖，P1再闭合武器和属性投影。保留成功标志、物理通道及已有事务缓存，不能把倍率<=0改成“未暴击”，也不能把所有原版暴击统一替换为PD2算法。

## F06 随机包装器3A0040及D2Game1480

- **性质／身份**：包装器无需移植，实际算法为可复用公共SEED。已闭合当前核心3A0040→全局5DEE08→构造器2131E0→getter3190F0→版本表4A8CC4首DWORD=1480→模块6/D2Game。原模块固定摘要24b293b86f95bea0228826c502ea04737ba21b91ca9832e52aac4b11fef14f53，基址6FC20000，原指令6FC21480–6FC214C8。
- **输入纠正**：包装器三个栈参数转ECX种子、EDI偏移、ESI宽度，ret12；不是在该入口计算max-min。ESI有符号非正时直接返回偏移，不推进随机。正宽度下，低DWORD乘0x6AC690C5并加旧高DWORD，存回64位结果；宽度为2的幂时按低位AND，否则无符号DIV取余，最后加偏移。
- **D2X对应**：[rollRandom／limitedRandom](../../src/core/random.hpp)使用同一64位递推，bound在1..INT_MAX才掷数；对2的幂用模与原AND结果相同。F01传入偏移0、宽度100时，给定相同初始种子，一次调用返回值和下一种子局部等价。非零偏移的其他消费者需要显式32位加法，不可原样只调用limitedRandom。
- **结论／边界**：暴击所需公共随机算法局部对应，不需要替换成PD2事件随机；原生单位+20的种子与D2X combatRandom在全部出生／继承／提交顺序是否一致仍是调用链门槛。3A0040不是SHRD20事件流，也不是CRT rand。P0只处理种子来源与调用次数的未闭合项，不重写已对应的公共递推。


## F07 武器参数层37F760

- **性质／依据**：PD2跨暴击、格挡、压碎及溅射复用的筛选helper；当前37F760–37F8EB，证据`town-queries/pd2-final-melee-helpers/0x1037f760.json`。ECX为单位、EDX为属性号，先由30BC90从库存取武器，再由30DFA0复制最多32条属性。
- **D2X对应**：[pd2ParameterizedItemStat](../../src/gameplay/items/pd2_parameterized_stat.hpp)。真实武器不存在返回0；按StatKey(id,layer)有序映射扫描指定属性，前32条计数包括零层，零层不贡献；匹配层用nativeAdd累加，符合原32位ADD。
- **逐分支核对**：原37F83E起的2han层要求另一手空或同一物品；37F87F起的1han层对双手类型要求另一手存在且不同，对非双手允许另一手空但拒绝同一物品。D2X对应条件一致；双持单位的2han武器可尝试1han层，与原类型不匹配后30DCE0的资格补充分支对应。
- **仍未闭合**：D2X weapon.typeLayers是否完整包含原ITEMS_CheckItemTypeId的父类，types映射是否使用编译ID，StatSnapshot聚合后32条截取是否等同原FullStats；这些不能仅凭std::map排序认证。D2X另一手来自equipment.weapons及shield，原通过实际身体槽查询，330B90／30BC90已定位为不同的当前技能选手／库存已选GUID入口，实际运行阶段仍待闭合。职业／影子双持资格由调用者传入，不由此纯函数保证。
- **反编译纠正**：伪代码把累加器local_11c误用作单位指针；原37F84D／37F889从栈+18读取单位，再取+60库存，不能复制伪代码中的空指针访问。
- **结论／步骤**：筛选条件局部对应，输入语义证据不足，P1。先闭合原属性复制与实际装备槽，再决定是否需要改D2X；不得取消32条上限、先过滤零层再截32条，或把所有同类武器属性无条件合并。

## F08 动态主人查询2FAA20

- **性质／依据**：PD2共享属性查询拦截；当前证据`town-queries/pd2-item-dynamic/0x102faa34.json`实际入口2FAA20。183遍历真实玩家／怪物主人库存计无形合格装备，184按主人生命求缺血百分比，478在原值与两项输入均正时增加贡献，其余委托原总属性查询。
- **D2X映射**：[samplePd2OwnerStats](../../src/gameplay/stats/pd2/owner_values.cpp)将动态字段放入临时根及快照；[pd2EquippedEthereals](../../src/server/systems/attributes/pd2/item_values.cpp)使用active集合、无形位、装备容器和活动武器组。消费者包括attributes/calculation、effects/amazon及skills/evaluation，不修改持久属性系数。
- **数值对照**：missingLife对生命／最大生命使用uint32后double相除，最大值0返回0；超出有符号结果范围返回INT_MIN以表达转换边界。半径使用nativeMultiply回绕、min(40,乘积)、nativeAdd到原半径，符合原2FAA20分支的先截新增量后相加，不是最终半径<=40，也没有下限钳制。
- **查询模型差异**：原在每次总属性查询经主人指针取值；D2X在采样点构造快照，遇到动态伤害系数递归清理fullValues后重新求值。是否所有费用、持续伤害、反伤、物品提示及伙伴调用者都取得新快照未闭合。纯公式不能证明缓存失效时序。
- **身份／装备缺口**：active集合是否等同380C10的全部身体位置和资格须沿装备投影核对。[effects::unitStatValues](../../src/server/systems/effects/amazon.cpp)已在玩家查询重新采样生命，在怪物路径合并当时状态并从Item贡献构造active集合，调用相同计数器；不能将它判为缺失实时查询。仍未证明所有调用者都走此入口，以及怪物装备／铁魔材料／佣兵资格与原生同义。原183和184只接受玩家或怪物主人，D2X领域查询也区分这两类，但直接纯函数调用仍须保证主人来源。
- **结论／步骤**：正常数值部分局部对应，实际查询时点和装备资格证据不足，P1。先核主人和active来源，再核op4／5及缓存重算；不把瞬时值写入存档，不用UI生命值替代权威资源。


## F09 公共nativeAdd对x86 ADD

- **性质／原生依据**：跨原版及PD2公共整数基础；F01概率合并、F05倍率及F07累加均有32位ADD，溢出取低32位，不是饱和，也不是C++有符号溢出的未定义行为。
- **对应实现**：[nativeAdd](../../src/gameplay/rules/arithmetic.cpp)先将first扩展为int64再加second，随后nativeNarrow转uint32并bit_cast为int32。两个int32之和不会越过int64，因此结果与32位ADD的位模式对应；nativeNarrow保留低32位，不抛异常。
- **结论／限制**：所审ADD用途可复用，不需要PD2版重复实现；只返回结果，不提供CPU CF／OF。需要进位的随机算法不能用一次nativeAdd代替ADC链；涉及移位、除法或float的helper也不能由此推广。源码中的普通int加法须逐调用复核，不因公共函数存在就认定都已使用。后续任务保持P0调用点审查，不改此函数。

## F10 地图说明36C9B0／37AF70与排序36A210

- **性质／版本**：当前17d5…核心的PD2专用格式程序；证据`town-queries/gap-map-melee`、`gap-description`、`gap-sort`、`gap-sort-detail`及`gap-description-charges`。具体分支、排序、缓存容量与特殊格式见[地图信息显示](MAP_PROPERTIES.md#地图信息显示)。
- **确定差异及修正**：旧RemoteScene调用普通describeItemStats，导致全组要求、降序排序、零值过滤、格式及文本容量均与地图程序不同。现改为[describeMap](../../src/content/pd2/map_description.cpp)，保留报告顺序、15条候选、同组同值合并、稳定优先级升序以及507个UTF16单元尾部；普通物品描述器不改动。
- **共享／专用边界**：共享utf8Decode、nativeMultiply／nativeSubtract和当前表模型；地图选择、格式及容量在content/pd2内独立实现，客户端只映射原报告并提供区域名。调用者仍以GUID／类型匹配已收到的地图报告，不据客户端说明推算权威属性。
- **限制／验证条件**：代码已构建；最终需覆盖重复组、跨层同值、15／16条、零值、同优先级、负值、函数5／19／20／21、UTF16尾部截断及多行颜色。孤立代理项采用U+FFFD保护；格式19仅消费当前表文案的单个%d及%%，不模拟任意CRT格式串。未认证所有语言包、未知格式及像素表现。

## F11 PoisonEggSpawn：3A9710／3B1090

- **版本与原生链**：当前17d5…安装表36A270／36A560分别挂Clt109／Srv173；客户端依次委托原Clt25、Clt46(159)，服务端依次委托原Do22、Do84(159)，等级均沿用调用值。原D2Client固定dd8b…样本56780只停dwSeqSpeed；服务端原逻辑对照D2MOO的SkillSor／SkillMonst。具体MPQ消费者见[技能分册](SKILLS.md#poisoneggspawn的双端委托)。
- **确定差异及修正**：旧服务端按GargoyleTrap准备单射线，且未准备spawn子怪；客户端未分派109。现准备独立Pd2PoisonEgg动作、真实子怪，PD2执行器复用公共Nova／Hatch并保留两阶段发布状态；客户端接原毒环与序列停帧。
- **共享边界**：原Do22／84提取为同类私有成员，原动作继续使用，PD2编排在独立目录；数量、飞弹、子怪和模式读取MPQ，159是DLL明确传入的算法常量。没有新增协议／存档字段。
- **限制**：完成当前回调及调用者的静态接线，已构建，尚未逐场景运行对照；完整首领场景、原随机交错、非标准序列速度与同帧生命周期仍未闭合。不能将本条扩展为全部GargoyleTrap或Torajan场景完成。

## F12 掌握3067B0与实际武器入口

- **当前契约**：详见[掌握选择与求和](ITEMS.md#3067b0掌握选择与参数层求和)。3067B0通过当前Common主类型／Throwable导出选择melee或throw，再调用37F760；原版最大值与Range门槛不能替代。330B90对应D2Game372C0的当前技能／序列选手；30BC90对应Common20320的库存已选GUID，二者不能合并解释。
- **已修正**：`content/pd2/skill_data`读取直接Throwable并保留隐藏技能元数据；`gameplay/stats/pd2/item_values`独立求和，装备派生及执行采样共用；飞弹接触补实际武器Throwable门槛和完整参数层上下文。原版最大值及原版准备规则不变。
- **静态证据边界**：当前MPQ直接Throwable技能的Range均为rng，门槛差异是契约恢复，未声称现表复现；求和与最大值则是可明确区分的算法。配套DLL导出、实际手位调用和表成员摘要已记入负责条目。
- **未闭合**：飞弹接触的weapons[0]仍未按当前技能WeapSel／标志／序列选择；原运行列表、切组、32条顺序及动态采样时点继续沿F07审查。已构建，不把本条标为完整武器链等价。

## F13 皇家火客户端3A8BE0

- **契约／证据**：当前CltDo83按技能表回调号门控，专用路径见[皇家火](SKILLS.md#皇家火cltdo83)；`gap-client-programs`函数指令、`p3-helpers/37BB50`及配套Client40F00／40200共同确定本单位坐标、种子、方向、步长与螺旋路径。Common ordinal10614为低种子初始化，10637写飞弹TargetX。
- **修正**：独立RemoteScene PD2准备方法与显式83分派，复用原方向、SEED和Spiral；1113场地分支只对真实class生效。保留渐进83原路径，不用目标坐标代替施法者坐标。Skills+14C为Param2，不能按字面第一个参数误读Param1。
- **限制**：只接已核定客户端入口，已构建，尚未逐场景运行对照；未知公式与非正步长拒绝，原生异常无限循环不移植。服务器143直接入口已按F14接入；完整子飞弹及特殊控制器仍有缺口，未认证视觉随机初态及全场景像素。

## 待审依赖

掌握、属性聚合及全部原生调用者仍需分别审查；技能／地图／AI等分册已有规则不自动计入本页已审范围。全库范围见[分析入口](README.md#全库分析的未完成范围)。

## F14 皇家火SrvDo143／3AE960

- **性质／依据**：当前PD2专用直接Do分支；36A47A安装槽143→3AE960→3A2580→配套Game93D60，绑定及指令证据见[皇家火服务端](SKILLS.md#皇家火srvdo143)和town-queries/gap-royal-fire。非直接Do143仍委托原渐进程序。
- **已补缺口**：[pd2MonsterProjectileProgram](../../src/gameplay/monsters/pd2/skill_program.hpp)与怪物selfAction接入143；[preparePd2RoyalFire](../../src/server/systems/missiles/pd2/royal_fire.cpp)按一次路径种子更新、只覆主人低字、独立局部SEED、整数方向／Calc1步进生成螺旋出生；采用当前实际怪物身份1113的场地分支。原server仅飞弹568写额外参数，未套客户端全弹体参数写入。
- **共享／专用边界**：专用准备放missiles/pd2，共享原螺旋路径、NativeBirthPublication、伤害与子飞弹；技能层持有准备批次，路径种子与出生同批复验／提交。容量重试不重掷，过期拒绝；没有新增存档字段或独立伤害系统。
- **结论／限制**：已补直接技能入口，已构建；当前表568／574分别5／4枚，依赖981→982及997→998→999须继续审完整推进、碰撞、NextDelay及客户端一致性。LucionControl与特殊首领控制器仍未完成，不能据单技能接入放开地图入口。

## F15 皇家火依赖的原CltDo6／B92B0

- **性质／依据**：配套原Client dd8b…，DD8A0槽6；指令、当前MPQ调用者及Common导出分别见[皇家火子弹](SKILLS.md#皇家火子弹与公共cltdo6)。本条为PD2新增技能所依赖的原版公共回调补缺。
- **确定差异及修正**：旧客户端在移动后只生成第一子弹，并将位置取整。现于公共运动前采样上一帧换格标记，按原顺序抽变体、固定第一子弹Range、抽光照门控，并沿用父弹精确坐标；复用现有创建／SEED／光照能力。当前981实际触发三个变体，997仅第一槽仍执行光照抽样。
- **边界**：`CelFile=null`已有无图像程序消费者，不列为新增修复。CltHit53、SrvDo6／Hit48已有消费者，但InitSteps显示边界、首路径点寿命、视觉种子与双端完整交错尚未闭合；已构建，未逐场景运行对照，不认证整条皇家火或首领场景。

## F16 传送门控36B570与阻挡物件37E170

- **契约／证据**：当前17d5…核心，配套Common040692…与当前States／Levels／Objects；来源、掩码、距离、各轴严格比较及调用者见[传送落点](SKILLS.md#pd2传送落点与怪物blink)。214是实际disable_tele，613–615是真实地图物件。
- **已补差异**：玩家PD2传送及Joust遗漏施放时的214检查，Mode3遗漏邻房物件门控，怪物Blink没有消费者。现以所属travel/pd2落点检查复用，怪物有独立施放编排及Lucion落地环；公共射线只增加掩码／来源尺寸输入，原默认调用保持相同。
- **限制**：Hell全局禁传集合来源、跨区怪物Blink及完整休眠／物件重新安装未闭合。当前两种怪物Blink无掌握减益，其他组合未开放；出生批次和阶段已保留，已构建，尚未逐场景运行对照，不认证全部Do155／156／157／162组合。

## F17 Guardian决策35A470与SrvHit6／3A5360

- **契约／证据**：当前Guardian本体／克隆均从Regurgitator按1064／1065分派；CRT、三项AI记忆、FIFO／2×2寻位、550→908→909及配套Game47CF0预设出生见[Guardian条目](MONSTERS.md#ai程序与原版回退)。入口由当前3A6D8C写表槽6核定，未从旧地址直接推定。
- **已补差异**：专用AI保留有目标门槛与无目标原版委托；共享宿主CRT，固定方向FIFO寻位与位移技能请求独立于普通单位SEED。SrvHit6由有序飞弹操作接普通预设出生，当前克隆的身份／模式／0x4020000来自MPQ；原型、首次／后备搜索和房间随机沿公共出生发布。首次半径零抽样也只执行一次。
- **准入复查**：CS A1原COF只有事件1，原准备层套Regurgitator事件2导致克隆替身；1064／1065的A1已改用事件1。实际管理出生保留本体357／493／550、克隆357／493及真实代码，Blood Moor准备结果不再列Guardian克隆替身；不据此认证完整AI／Orb轨迹。
- **边界**：当前只处理普通NU／S1出生和无经验／无掉落标志；特殊预设、附属物件、其他标志／模式保持未实现。没有自动增加原函数不存在的主人关系；完整Orb时序、休眠、场景死亡／奖励／释放及全局随机交错未闭合，已构建，尚未逐场景运行对照。
