# 物品与公共数值等价性审查

本页维护F编号审查正文，不复制原函数契约。判断口径与完整编号见[审查目录](../FUNCTION_PARITY.md)，版本见[BUILDS](../BUILDS.md)，实施顺序由[缺口计划](../GAP_PLAN.md)维护。未另标版本的核心地址沿17d5分析基线，其他模块与构建以条目为准。
## F01 暴击种类判定3048E0

- **性质／原生依据**：PD2专用；当前3048E0–3049B0，原指令及条件见[函数契约](../ITEMS.md#3048e0暴击种类判定契约)。依赖3067B0掌握、308030属性查询、3A0040随机。
- **D2X入口**：[rollPd2Critical](../../../src/gameplay/combat/pd2/item_modifiers.cpp)；飞弹接触消费者为[preparePd2MissileCritical](../../../src/server/systems/combat/pd2/critical.cpp)。
- **逐分支对照**：nativeAdd将被动、掌握和物品暴击按32位合并；只在正概率时掷第一下，上限75；成功返回种类1并短路。第二阶段以原deadly>0为门控，即使调整上限非正仍掷数，成功为2，否则空结果。200／150加对应倍率也是nativeAdd；未把两类成功叠乘。
- **副作用对照**：纯函数仅推进传入的随机引用；消费者先复制caster.random，计算后由事务或monsters.consumeRandom复验并提交，再缓存contact.pd2Critical，已缓存时不重掷。这是D2X背压语义，需核对调用重入时机，不能直接视为原生所有时序已还原。
- **差异／未知**：原函数内部选择武器掌握，D2X由调用者预先提供；3067B0的直接Throwable与求和差异已按F12修正；当前飞弹接触已按used-skill WeapSel／序列／旋风标志选择实际武器，37F760另按库存登记GUID筛参数层；新改动已统一构建，完整调用链仍待专项对照。公共limitedRandom已在F06对上该调用所用的原3A0040，种子所有权、掌握和属性来源仍未闭合。
- **结论**：条件、种类、回绕加法及同种子百分比随机为局部对应；F02已核定消费者的百分比差异及当前技能选手已修正并构建，不能据此认证整条暴击链。剩余是完整手位组合、属性视图及出生／接触两阶段，不能仅改概率值。优先级P0（公共依赖）／P1（手位及调用链）。

## F02 百分比helper37A6F0

- **性质／依据**：PD2跨玩法共享数值函数，当前37A6F0–37A734，证据`town-queries/current-functions/0x1037a6f0.json`。不是原版整数ApplyRatio的别名。
- **原指令**：ECX按有符号int转double；XMM2为倍率；EDX按无符号比较取至少1并转double。执行MULSD后DIVSD，结果超过RVA44DF20的double常量时返回INT_MAX，否则CVTTSD2SI向零截断。该常量为2147483647；COMISD后JA不接受unordered，因此NaN继续到转换，负溢出及NaN得到INT_MIN。负数先截断再判断可表示范围，不将其写成双向饱和。
- **D2X公共候选**：[nativeMultiplyDivide／nativePercentage／nativeStatPercentage](../../../src/gameplay/rules/arithmetic.cpp)。前者先32位乘法回绕，第二个按原版整数阈值拆运算，第三个先算percent/100再乘且越界抛异常；三者均不能直接作为此函数的完整等价物。
- **实际消费者**：[missile_contact](../../../src/server/systems/combat/missile_contact.cpp)的PD2暴击现改用独立[pd2MultiplyDivide](../../../src/gameplay/rules/pd2/arithmetic.cpp)；[Teleport](../../../src/server/systems/skills/pd2/teleport.cpp)的四类掌握扣减同样复用该函数。[targetWeaponChannels](../../../src/gameplay/skills/weapon_damage.cpp)已按PD2标记选择同一helper；当前补丁表49B9A0–49B9D4及原D2Game DD52C→核心3A0060→303330→3049C0证实近战映射，详见物品分册。原版仍保留整数算术。
- **判别例**：输入20,000,000、倍率150、分母100，double乘除得到30,000,000；D2X中间3,000,000,000按int32解释为-1,294,967,296，再除100得到-12,949,672。这是静态算术反例，不声明已经在真实装备场景复现，也不依赖上限常量含义。
- **结论／步骤**：独立PD2函数、已核定飞弹／近战及Teleport消费者已修正，已构建。仍属P0部分完成：其他调用点、完整物理／元素顺序与随机消费需要分别闭合，不能由这些消费者的静态审查认证整个倍率链。

- **其余已核消费者**：Vengeance 3B10E0的三次掌握缩放、Sacrifice 3ADAA0反伤与303C60的两项尸爆贡献、Fn015 346400的PvP／状态225／抗性缩放、Fn016 3469B0的抗性扣减以及Properties25 39EFB4的67%概率均调用同一37A6F0，D2X现分别接`skills/pd2/vengeance`、`skills/weapon_melee`、`effects/pd2/sacrifice`、`combat/pd2/{wounds,crushing}`及`content/pd2/item_properties`。旧实现有原版整数乘除或独立双向钳制，正溢出／负中间量不能视为等价。状态225的保留贡献严格先将倍率除3，再乘值／除100，不交换浮点运算顺序。飞弹Dmg17亦共用此helper。41d49的Vengeance 3B3550在3B393E／3B3956／3B396E调用37C510，已读实际指令；其他条目仍限定17d5证据，不作全库平移。这些修正不增加掷数或重置准备／提交游标，全部来源组合仍未认证。

## F03 生命写入门槛2FC030

- **性质／依据**：PD2专用拦截，原生分支见[生命契约](../ITEMS.md#2fc030生命写入门槛契约)。
- **对应函数**：[pd2LifeDrainWriteAllowed](../../../src/gameplay/combat/pd2/leech.hpp)保留严格小于、候选或旧值任一满足就整次放行、最大值0用1，以及旧生命／最大生命的uint32转换。没有钳至门槛，普通非负cap下局部对应。
- **已修差异**：原属性488无符号转double，D2X已将double(cap)改为double(uint32_t(cap))。cap=-1、previous=850、next=900、maximum=1000时，旧代码阈值1.01允许；新代码按原4294967295得到负阈值并拒绝。已构建，真实MPQ、聚合、存档／网络是否可形成此值也未确认。
- **调用者与新差异**：17d5的306E80／41d49的307010是安全回血入口，先拒绝非正增量、死亡及state92，再以有符号64位相加、限最大生命，负值或越界饱和INT_MAX后调用2FC030／2FC040。原D2X吸血先nativeAdd再min，会把大正和回绕成负生命。现`pd2SafeLifeAfterGain`共用该候选及门槛，`advanceMissileDrain`玩家／怪物调用均补state92，保留原事务、随机及drainCommitted。原版分支不改。
- **边界**：玩家飞弹吸血在准备生命写入时通过effects.unitStatValues重新读取实时lifedrain_percentcap，不再直接使用totals.character.combat中的旧cap；怪物仍从当时的effects.combatModifiers取值。该修正已构建，同帧状态变化／背压实测及近战、佣兵等全部安全回血调用点尚未闭合。不能认定所有吸血已一致，也不应把该谓词加到所有药剂／治疗。
- **吸收消费者**：核心305B60的305CF0及305E20的305F7E以伤害描述符+48的AbsLife调用306E80；3A3810另安装于配套Game位移DBB80／DE23E，分别包含吸血与吸收。`effects/skills::receiveMonster`及`monsters::damage`现让PD2吸收回血使用同一安全入口，在减伤害前读取当时的488；玩家使用吸收回调后资源投影，怪物重新查询unitStatValues，死亡延迟不回血。没有把该规则加到药水、Holy Bolt或Life Tap的未核实直接写入。
- **结论／步骤**：高位解释、64位候选及已核吸血／吸收消费者已修正；本次新修正已通过Windows Release公共目标构建，PD2运行仍待验收。全部伙伴、同帧状态、源端死亡模式及其他安全回血调用者仍待对照，不能据此关闭完整T06。

## F04 充能回调3452D0

- **性质／依据**：PD2专用回调，依赖公共属性列表、物品主人、事件调度及通知。原输入和分支见[充能契约](../ITEMS.md#3452d0充能事件回调契约)。
- **D2X实际入口**：[inventory::replenishItem](../../../src/server/systems/inventory/replenishment.cpp)、[items::restoreGround](../../../src/server/systems/items/system.cpp)、[companions::restoreMaterial](../../../src/server/systems/companions/pd2/material_restoration.cpp)；纯值辅助为[pd2FirstChargeLayer／pd2RestoreCharge／pd2WriteChargeValues](../../../src/gameplay/items/pd2/charges.hpp)及[itemRestoration](../../../src/gameplay/items/replenishment.hpp)。
- **已对应部分**：满充能仍rearm而非撤销；每次只加1；正速率间隔2500/rate后再加1，玩家特殊区域先将商减半；地面无玩家主人不减半。库存更新经事务，成功后重排事件；地面先发布事实再更新。没有重新抽技能或将运行时钟写入物品。
- **记录来源缺口**：原先要求标志40列表及FullStats均有204，使用前者首记录。已据原Common的pMyLastList／pMyStats与Items读档登记顺序修正为符文之语优先、否则基础列表，条件套装列表排除；只在选中列表内选首层，FullStats以已准备物品属性中存在204作门槛。保存、执行缓存及3E按同一源记录更新一份，客户端也按该列表消费。单一列表内非规范重复键及所有动态列表重挂顺序仍未认证。
- **编码已核**：当前pd2data的ItemStatCost BIN共511条、步长0x15C，204行+10的Multiply=256、+19的SaveBits=16、+30的Encode=3，低8位当前值／高8位上限与D2X对应。BIN摘要eea4bed2894fb094f695ba935b0178e6d953dc77518b140328cb812f92596cdf，证据charges-identity.json。原生包装器30C530→Common ordinal10195／384F0→37F40从pMyLastList逆链选第一张含0x40的列表；30D630→ordinal11037／3A790→3A1D0只更新指定列表的指定层，并传播差值。不能把FullStats的存在性门槛当成第二次恢复。
- **执行差异**：原rate非零即续订，D2X只接受charges>0；负速率可达性待核。已移除PD2充能在交易期间的全局暂停，沿原PlrTrade无物品再生撤销及取消时载入pSaveData的链补交易镜像和取消后的完整物品刷新；非充能恢复保持原行为。3451E0已核只在没有事件3时初次登记，已有事件不因区域／速率变化重排；D2X restorationClock同样仅在hasEvent为假时插入，回调按当时区域重新求下次期限。这一分支不应改成跨区立即重置计时。
- **结论／步骤**：正速率、原生保存列表和既有事件路径已补上述差异并统一构建；Demonlimb单层的施放／恢复、交易同位置回滚、对手镜像、交换及保存重入已有实测，多列表、地面背压和全部跨区交错仍需覆盖。铁魔材料已补单一物品时钟、主人／实体材料事务和204来源差值回调并通过双端构建，真实Gavel材料消耗、204层2/3→3/3定时恢复、kf保存及新进程恢复已有闭环，多层组合仍待覆盖；负速率、非规范重复键和全部伙伴装备组合仍为未认证边界，不把新增修正推广到全部物品生命周期。

## F05 暴击倍率应用3049C0

- **性质／依据**：PD2专用，原生寄存器、返回与写入见[倍率契约](../ITEMS.md#3049c0倍率应用契约)，依赖F01判定、F02缩放、37F760参数层和330B90实际武器。
- **D2X映射**：[preparePd2MissileCritical](../../../src/server/systems/combat/pd2/critical.cpp)准备结果，[missile_contact](../../../src/server/systems/combat/missile_contact.cpp)写入rolled.critical及channels[0]；[weaponCritical／targetWeaponChannels](../../../src/gameplay/skills/weapon_damage.cpp)承担近战计算。不存在必须照搬的单一寄存器包装。
- **对应部分**：kind而非percentage控制成功，零／负倍率仍保留critical；只有物理通道被缩放；[applyPd2WeaponParameters](../../../src/gameplay/stats/pd2/item_values.cpp)将无参数倍率与匹配武器类型层分开相加。输入无有效参与者时由领域资格拒绝，替代原指针判空，但完整上游行为尚未全审。
- **确定缺口**：飞弹及近战倍率已改为F02的PD2浮点缩放，近战安装／桥接链见物品分册；飞弹输入已改为当前技能WeapSel、旋风切手标志及序列时钟选手，参数层另读库存登记GUID，新改动已统一构建，完整调用链仍待专项对照。出生路径还单独保留种类标志，是否恰好按原生两阶段消费随机需审出生消费者，不能简单删除出生掷数。
- **结论／步骤**：非完整等价，P0先修经核定的数值依赖，P1再闭合武器和属性投影。保留成功标志、物理通道及已有事务缓存，不能把倍率<=0改成“未暴击”，也不能把所有原版暴击统一替换为PD2算法。

## F06 随机包装器3A0040及D2Game1480

- **性质／身份**：包装器无需移植，实际算法为可复用公共SEED。已闭合当前核心3A0040→全局5DEE08→构造器2131E0→getter3190F0→版本表4A8CC4首DWORD=1480→模块6/D2Game。原模块固定摘要24b293b86f95bea0228826c502ea04737ba21b91ca9832e52aac4b11fef14f53，基址6FC20000，原指令6FC21480–6FC214C8。
- **输入纠正**：包装器三个栈参数转ECX种子、EDI偏移、ESI宽度，ret12；不是在该入口计算max-min。ESI有符号非正时直接返回偏移，不推进随机。正宽度下，低DWORD乘0x6AC690C5并加旧高DWORD，存回64位结果；宽度为2的幂时按低位AND，否则无符号DIV取余，最后加偏移。
- **D2X对应**：[rollRandom／limitedRandom](../../../src/core/random.hpp)使用同一64位递推，bound在1..INT_MAX才掷数；对2的幂用模与原AND结果相同。F01传入偏移0、宽度100时，给定相同初始种子，一次调用返回值和下一种子局部等价。非零偏移的其他消费者需要显式32位加法，不可原样只调用limitedRandom。
- **结论／边界**：暴击所需公共随机算法局部对应，不需要替换成PD2事件随机；原生单位+20的种子与D2X combatRandom在全部出生／继承／提交顺序是否一致仍是调用链门槛。3A0040不是SHRD20事件流，也不是CRT rand。P0只处理种子来源与调用次数的未闭合项，不重写已对应的公共递推。

## F07 武器参数层37F760

- **性质／依据**：PD2跨暴击、格挡、压碎及溅射复用的筛选helper；当前37F760–37F8EB，证据`town-queries/pd2-final-melee-helpers/0x1037f760.json`。ECX为单位、EDX为属性号，先由30BC90从库存取武器，再由30DFA0复制最多32条属性。
- **D2X对应**：[pd2ParameterizedItemStat](../../../src/gameplay/items/pd2_parameterized_stat.hpp)。先按EquipmentStats.registeredWeapon查当前活动手位，登记为空或失效返回0；按StatKey(id,layer)有序映射扫描指定属性，前32条计数包括零层，零层不贡献；匹配层用nativeAdd累加，符合原32位ADD。
- **逐分支核对**：原37F83E起的2han层要求另一手空或同一物品；37F87F起的1han层对双手类型要求另一手存在且不同，对非双手允许另一手空但拒绝同一物品。D2X对应条件一致；双持单位的2han武器可尝试1han层，与原类型不匹配后30DCE0的资格补充分支对应。
- **准备与剩余边界**：`content/items/equipment_data`递归加载Equiv1／2并拒绝循环或缺失父类，`content/skills/formula_data`按ItemTypes记录顺序准备ID→代码，`deriveNativeEquipmentStats`遍历该映射为两手建立包含父类的typeLayers，不能再列为完全缺少类型继承。StatSnapshot的全部动态列表聚合及32行存在性仍需对照；缓存子列表零行丢失已沿F62修正。D2X另一手来自equipment.weapons及shield，原通过实际身体槽查询，330B90／30BC90已定位为不同的当前技能选手／库存已选GUID入口，实际运行阶段及完整编译表身份仍待闭合。职业／影子双持资格由调用者传入，不由此纯函数保证。
- **反编译纠正**：伪代码把累加器local_11c误用作单位指针；原37F84D／37F889从栈+18读取单位，再取+60库存，不能复制伪代码中的空指针访问。
- **结论／步骤**：筛选条件局部对应，输入语义证据不足，P1。先闭合原属性复制与实际装备槽，再决定是否需要改D2X；不得取消32条上限、先过滤零层再截32条，或把所有同类武器属性无条件合并。

## F08 动态主人查询2FAA20

- **性质／依据**：PD2共享属性查询拦截；当前证据`town-queries/pd2-item-dynamic/0x102faa34.json`实际入口2FAA20。183遍历真实玩家／怪物主人库存计无形合格装备，184按主人生命求缺血百分比，478在原值与两项输入均正时增加贡献，其余委托原总属性查询。
- **D2X映射**：[samplePd2OwnerStats](../../../src/gameplay/stats/pd2/owner_values.cpp)将动态字段放入临时根及快照；[pd2EquippedEthereals](../../../src/server/systems/attributes/pd2/item_values.cpp)使用active集合、无形位、装备容器和活动武器组。消费者包括attributes/calculation、effects/amazon及skills/evaluation，不修改持久属性系数。
- **数值对照**：missingLife对生命／最大生命使用uint32后double相除，最大值0返回0；超出有符号结果范围返回INT_MIN以表达转换边界。半径使用nativeMultiply回绕、min(40,乘积)、nativeAdd到原半径，符合原2FAA20分支的先截新增量后相加，不是最终半径<=40，也没有下限钳制。
- **查询模型差异**：原在每次总属性查询经主人指针取值；D2X在采样点构造快照，遇到动态伤害系数递归清理fullValues后重新求值。是否所有费用、持续伤害、反伤、物品提示及伙伴调用者都取得新快照未闭合。纯公式不能证明缓存失效时序。
- **身份／装备缺口**：active集合是否等同380C10的全部身体位置和资格须沿装备投影核对。[effects::unitStatValues](../../../src/server/systems/effects/amazon.cpp)已在玩家查询重新采样生命，在怪物路径合并当时状态并从Item贡献构造active集合，调用相同计数器；不能将它判为缺失实时查询。新补铁魔材料在活动Item来源及无形位成立时计入实际铁魔主人，材料武器补表驱动类型层及属性系数；不把制造材料复制为另一份持久库存。仍未证明所有调用者都走此入口，以及怪物装备／铁魔材料／佣兵资格与原生同义。原183和184只接受玩家或怪物主人，D2X领域查询也区分这两类，但直接纯函数调用仍须保证主人来源。
- **结论／步骤**：正常数值部分局部对应，实际查询时点和装备资格证据不足，P1。先核主人和active来源，再核op4／5及缓存重算；不把瞬时值写入存档，不用UI生命值替代权威资源。

## F09 公共nativeAdd对x86 ADD

- **性质／原生依据**：跨原版及PD2公共整数基础；F01概率合并、F05倍率及F07累加均有32位ADD，溢出取低32位，不是饱和，也不是C++有符号溢出的未定义行为。
- **对应实现**：[nativeAdd](../../../src/gameplay/rules/arithmetic.cpp)先将first扩展为int64再加second，随后nativeNarrow转uint32并bit_cast为int32。两个int32之和不会越过int64，因此结果与32位ADD的位模式对应；nativeNarrow保留低32位，不抛异常。
- **结论／限制**：所审ADD用途可复用，不需要PD2版重复实现；只返回结果，不提供CPU CF／OF。需要进位的随机算法不能用一次nativeAdd代替ADC链；涉及移位、除法或float的helper也不能由此推广。源码中的普通int加法须逐调用复核，不因公共函数存在就认定都已使用。后续任务保持P0调用点审查，不改此函数。

## F12 掌握3067B0与实际武器入口

- **当前契约**：详见[掌握选择与求和](../ITEMS.md#3067b0掌握选择与参数层求和)。3067B0通过当前Common主类型／Throwable导出选择melee或throw，再调用37F760；原版最大值与Range门槛不能替代。330B90对应D2Game372C0的当前技能／序列选手；30BC90对应Common20320的库存已选GUID，二者不能合并解释。
- **已修正**：`content/pd2/skill_data`读取直接Throwable并保留隐藏技能元数据；`gameplay/stats/pd2/item_values`独立求和，装备派生及执行采样共用；飞弹接触补实际武器Throwable门槛和完整参数层上下文。原版最大值及原版准备规则不变。
- **静态证据边界**：当前MPQ直接Throwable技能的Range均为rng，门槛差异是契约恢复，未声称现表复现；求和与最大值则是可明确区分的算法。配套DLL导出、实际手位调用和表成员摘要已记入负责条目。
- **未闭合**：飞弹接触已按当前技能WeapSel／旋风切手标志／动作序列时钟选择，并与37F760的库存登记GUID分开；新改动已统一构建。完整怪物动作、FullStats聚合及出生／接触随机交错继续沿F07审查，不把局部修正标成全链认证。

## F22 充能技能源D2Common_10954与隐藏ItemEffect

- **原生依据**：D2MOO D2Skills.cpp的D2Common_10954以技能记录及物品GUID寻找／创建独立技能，保留真实等级／数量，不检查charclass。当前MPQ The Gavel of Pain的charged参数为442／3／4；442为InGame、ItemEffect1、SrvDo30且有独立SkillDesc的无职业记录，9gm的bitfield1为3，可作金属材料。
- **已补消费者**：RemoteCombat及SkillCatalog保留有依据的隐藏物品技能元数据，前端仍验证来源GUID、有效充能、手位、目标和当前场景；没有来源的技能不会因元数据存在获得可用等级。PD2内容准备SrvDo30的原诅咒输入，hosting接受已有执行器的隐藏ItemEffect，复用Effects的诅咒与充能原子支付；本人释放及Leap统一查询实际来源等级。
- **已知阻塞／边界**：此前双端构建后，隐藏ItemEffect元数据将无职业怪物的职业SkillDesc交给通用图集，场景报`Missing original common skill icon: 冰尖柱`。当前已在`content/skills/skill_data.cpp`按SkillDesc的SkillPage／SkillRow／SkillColumn收紧：无职业ItemEffect只有原表明确的通用布局才进入通用图集，MonGlacialSpike等引用职业布局的怪物文本不构造通用图标；AmpDmg Proc等零布局的真实物品技能仍保留。图形单机现已实际进入并完成442选择、施放3→2、定时恢复2→3与保存／新进程恢复；服务端隐藏ItemEffect执行元数据在content/pd2独立准备，避免沿显示布局过滤掉Do30。该结果关闭此前入局及442执行阻塞，其他隐藏技能仍按各自执行器与来源判断。无职业旧前置名和未接执行器的怪物公式已分别限制在非职业元数据／未准备分支，职业前置校验保持严格。没有SkillDesc的记录不自造UI，未准备执行器明确拒绝；材料恢复及原生kf保存沿F04，不以客户端元数据存在代替服务端执行成功。

## F62 属性浮点累计与查询边界

- **证据与范围**：17d5的2FAE70、41d49的2FAE80实际函数体；共同的op3／5在17d5的2FB017–2FB036及41d49的2FB027–2FB046执行IMUL、DIVSD、ADDSD、CVTTSD2SI。原版D2MOO `D2StatList.cpp::sub_6FDB5830`的op3／5／7同样将整个浮点和转回整数。2FAE70的通知用途及op1例外见[函数契约](../functions/ITEMS_AND_STATS.md#2fae70-物品通知的递归属性查询)，不能把该helper整体当权威GetTotalStat。
- **确定差异与修正**：公共`gameplay/stats/stat_list.cpp::Evaluation::compute`原先先截断百分比增量再nativeAdd，现保留32位乘积回绕，除100后加当前累计值，最后经`nativeTruncate`统一向零转换；非有限值及越界返回INT_MIN。op1／11／13继续沿原版独立增量语义，不受通知helper中的特殊op1污染。
- **可区分输入**：累计1、乘积-50时，原整和`trunc(1-0.5)=0`，旧实现返回1；累计INT_MAX、乘积200时，原转换返回INT_MIN，旧整数回绕返回-2147483647。这里只说明静态算术，不声称真实装备已复现。
- **FullStats存在性**：重建根键集合时保留缓存子列表中的零行，随后仍按当前规则的KeepZero决定结果是否存储。D2MOO `D2Common_CopyStats_6FDB9C50`逐行复制且零值占用容量；F07的前32行不能因键集合预先过滤零而前移。基础零值仍删除，未凭空创建不存在的零行。完整动态列表重挂、32行及DamageRelated组合继续保留F07边界。

## F63 41d49核心计数映射

- **版本与原生入口**：41d49完整摘要见BUILDS。37E630先以37DEE0读取实际Misc记录代码，经37DEC0查434DB0的60项代码；失败再用`index-7`的无符号0–299边界索引37EE60字节表及37EC40跳转表，默认37EC38返回空。每个有效case均逐字节核对`mov eax,[esi+14]; pop edi; add eax,offset; pop esi; ret`，offset减436为0–390内偶数。
- **逐项结果**：300项普通选择与17d5的37D040／37CE20逐项相同，181项有效；60项地图代码与17d5的432DB0逐DWORD相同，29个非零代码优先映射115–143。不是按地址差推测算法。41d49的394060复制98个DWORD至PlayerData+436；394140按包+1的WORD调用37E630，并将包+3的WORD写到返回地址，尾WORD不参与计数。
- **消费者与边界**：`app/client_setup.cpp`以精确摘要选择Normal20261026，`realm_session_pd2_state.cpp`的85全量与86绝对增量均使用`content/pd2_core_counters.cpp`已准备的current表；仍逐件校验Misc BIN／TXT身份。其他未知核心继续保存原始数组及未映射增量，不放宽登录之外的格式校验，不开放材料存取／共享仓库。此项关闭I07的身份／映射缺口，非零实服同步与未来核心版本仍各自核对。
