# 装备资格、属性贡献与切组

更新：2026-10-15。本页负责武器／防具／首饰／护符的资格、活动装备、套装激活与属性贡献。生成与词缀归[GENERATION](GENERATION.md)／[AFFIXES](AFFIXES.md)，战斗消费归[攻击](../combat/ATTACKS.md)，充能／触发归[物品技能](../skills/ITEM_SKILLS.md)。

## 当前执行

身体需求由公共loadout／contributions／requirements计算，不重复维护力量／敏捷公式。未鉴定、损坏、非活动手部不贡献，护符只在背包生效；普通交换、双手冲突、装备与两组武器切换已有权威执行。属性／需求变化同步重新校验装备，装备资格与人物总值同笔提交。

人物资格通过同步候选回调采样保留的原始物品／套装、缓存被动及临时来源，保留参数层、等级和幕时段的原op运算。`updateEquipmentActivation`按原库存护符补投、bodyloc1–10禁用、反复启用及最后套装刷新执行；原首轮要求ITEMS_IsCharmUsable，只补投背包可用护符，身体装备不抢先按库存顺序投递。候选保留旧有效来源，因此需求检查可读取已投递装备的自身支撑。BROKEN与NOEQUIP分别维护，不能从未活动直接推断NOEQUIP。库存变化、幕通知和需求属性变化触发装备刷新；普通状态采样不无条件重投所有套装，完整装备组合仍待原版对齐。

套装候选掩码按当前活动组实际装备位置与BROKEN／NOEQUIP读取，不以已投递来源集合代替。撤出本件前先撤出该套装的单位全局列表；补投先刷新全局列表再合并本件及条件层，最后逐体位重投套装。候选保留单位六个ITEMSET槽，全局同套属性指令合并为同一原始列表；公共equipmentSetContributions亦用于类型化投影。固定1.13c D2Common ordinal10600／RVA2C910确认UpdateSets的掩码与撤出分支，RVA2C630处理全局列表；ItemMods RVA45F90中部分奖励取min(count,full−1)，全套分支仍比较原count，不采用D2MOO将count本身截断的重建笔误。

每次候选来源变更及被动刷新记录raw最大生命／法力／耐力，事务与PlayerResourceTrace按相同中间上限重放资源回调；人物公式读取相应时点的当前资源。事务先重放的状态来源回调显式传入resourceBase，避免后续公式再按旧上限缩放一次。父级propagateNativeStatDelta已开始逐packed属性键传播物品FullStats投递／撤出、套装全局清空／重写及玩家被动写入；按原反向op与op-base依赖顺序触发各中间资源上限，并保留raw FullStats与MinAccr查询投影的区别。物品完整条件层先挂接再传播，撤出前按无pUnit重算本件，再从父级逐项减回；单位六槽全局列表撤出与已挂接列表RemoveAllStats的逐项清零分开。

InventoryState已保存相对节点顺序，原D2S根物品读取顺序、创角插入、装备草案移动、事务Moved／Created／Removed事实和入场ID重映射接入该序列；护符按存活节点顺序采样，普通放置先摘除再追加链尾。孔内子物品继续沿主物品自身顺序，具体原格式及校验见[存档](../../modules/SAVES.md#文件与值边界)。人物已保留逐本件raw FullStats，并在投递／撤出时通过itemStatsAfterUnitChange只刷新原item-applying op；保留旧主人关系时也按prepared原地属性／充能差异逐项更新本件，再将原输入delta传播给单位，避免把各派生op差额再次相加。普通查询读取发布的不可变FullStats，raw重算使用另一入口；手位选择只撤旧主手／加新主手的DamageRelated项，保留其他缓存。佣兵准备也开始保存本件及单位FullStats。

玩家装备投递／撤出及原地写入开始同步调用skillStatRefresh／PlayerSkillRefresh：单技能定位本技能，职业／页／元素／全技能依原技能链刷新被动，新建非职业／单技能条目保留AddSkill基础1再AssignSkill基础0的两次刷新；被动逐写后的新树返回父级传播，后续op不继续使用回调前的树。先天技能按当前CharStats列序而非名称排序，运行技能链保存无主／充能所属GUID条目；同一物品同一技能的充能条目按原10954更新等级，不以各层为独立技能。最终被动投影沿用这份即时缓存，资源轨迹保留中间值。单技能失效及充能条目删除记录原技能／所属GUID的选择清理事实；人物事务在普通回调边界清除匹配选择，在原库存刷新边界仅按最终同ID／GUID条目及恢复门判断是否保留。skillSelectionRestorable只排除NoLevel／NoQuantity，保留Disabled／Aura／Passive的原先后条件，不用法力、冷却或完整施法就绪拒绝恢复；普通配对类型／弹药判断位于gameplay公共equipment入口，参数从当前MPQ准备。

装备容器持有运行期registeredWeapon，物品移动及武器切换按登记候选重放，入场按原JM节点序重建；资格禁用／重启先更新登记再传播属性，套装来源刷新不重复登记。公共weaponRegistrationOnInsert／OnRemoval与registeredWeaponHand保留登记GUID而非固定右手，移除时对手位盾牌也可写入GUID，已登记本件再次插入可清空GUID；这些分支核对固定1.13c D2Common ordinal10138／RVA20620及10694／RVA1FC70。技能恢复区分已知空登记与未知输入，并读取同一候选；DamageRelated手位也从登记解析。普通移动／切换顺序按D2MOO ItemMode移植，同版全部操作时序、换组标志、佣兵及跨主人组合仍待核对。回调依据为本地D2MOO ItemMode::ServerStatlistCallback、Skills::RefreshPassiveSkills、D2Skills::AddSkill／AssignSkill，全部同版分支仍缺完整对照。

玩家used-skill由actions保存独立的技能ID／所属GUID，不从左右键选择或施放记录推导；PlayerSkillRefresh生成独立移除事实，无主条目删除只匹配无主实例，充能删除则按技能ID匹配任意used实例。单人物、双库存及成组人物事务发布成功后共同清空当前引用，库存刷新恢复左右键时不恢复used。固定1.13c D2Game RVA D9A04–D9A87确认源物品当前充能低字节／主人检查，D2Common ordinal10029／RVA4F610进入4EE00；4EE82–4EEAA清理左右键时核对GUID，4EEBA–4EECA清理used时只比较技能ID，非正技能等级在4EE18–4EE1E提前返回。公共removeSkillUse与nativeSkillSlotLevel使用这些身份语义；普通玩家动作帧按D2MOO sub_6FD13410读取used实例及当前实际等级，零级条目不因存在于映射而获准。used清空不直接调用整动作取消，已经发布的飞弹／效果继续存在；新施放先分配动作／used节点，再发布事实，只有已知清空引用的技能动作放开技能锁，恢复动作及冷却仍有独立条件。Inferno开始扣费与动作事实经release成组发布，资源单独提交不重放旧的技能移除账本。普通帧与直接Skills_Handler的最高条目回退是不同入口，完整模式、动态SrvStart／SrvDo参数、怪物引用及全部释放组合仍待迁移／同版核对。

尚待迁移全部基础／状态入口的缓存延续、未投递物品列表、完整登记／换组与佣兵组合、完整模式／使用中技能程序、物品光环与单位事件注册，以及完整佣兵／特殊装备更新过程；跨所有者链操作、尸体与交易组合仍待核对。该纯值契约不传入MPQ、世界或完整游戏系统。

基础Stats的SetStat(0)与AddStat归零都会删除该项，KeepZero只用于派生FullStats；公共writeNativeBaseStat、物品／模式与状态写入、被动刷新沿这一边界。固定1.13c D2Common ordinal10887／RVA3A740进入3A280，3A2E5–3A2FC确认基础零值删除；39F70区分直接加值与39CE0反向op重算，父级反向依赖表按当前MPQ原stat编号准备。hasOwner表达原pOwner，hasUnit另表达来源列表的pUnit；玩家／怪物单位根没有因pOwner存在而自动取得pUnit，物品挂接及解除时才按相应关联刷新op 4／5，并保留挂接前的原系数槽。固定1.13c ordinal10973／RVA38B70进入38A80，选择FullStats／基础数组及现有项，不在普通查询中重算op。缺失零项也不由全表op扫描凭空生成，避免对不存在的项应用MinAccr。最大资源回调先低钳1再高钳新上限，保留暂时非正上限的原结果，不使用上下界倒置的std::clamp；原D2Game D95E4–D95FA确认该顺序。

物品单位事件开始从当前MPQ Events名称链及ItemStatCost.itemevent1/2、itemeventfunc1/2准备为StatRule绑定。公共refreshItemStatEvents遵循ServerStatlistCallback的首事件>0门、非零注册／零值注销、完整statId/layer键的队列2查重；每个有效绑定头插，所以第二绑定位于第一绑定之前。固定1.13c D2Game RVA D94DF–D9554确认该门及调用，RVA370E0的GetEvent核对queue／custom／GUID1，37190–37213确认头插，函数表RVA1077A8只在1–31槽非空。PlayerSkillRefresh在每项已接属性回调先更新注册候选，Totals.itemEvents持有顺序并随人物事务一起发布。已接ItemTriggers按调用者请求的阶段顺序、再按注册链准备触发，几率从当前原始参数层读取，双近／远程绑定在当前阶段API中不重复施放同层。完整原单位事件类别分派、回调中自注销／一次性事件、伤害现场修改和单位随机流尚未迁移，不能据此宣称完整SUnitEvent复刻。

item_aura回调单独保留Totals.itemAuraChanges：每项记录物品GUID、当时聚合等级、AuraState、当前PerDelay采样及immediate标志；零值撤销，非零值激活，包括原版允许进入激活分支的负值。固定1.13c D2Game RVA D99C3–D99F5进入ADB30／AD050，激活先按物品GUID删除PeriodicStats，再排期，最后才执行immediate；撤销先移除该AuraState，再删除该GUID事件。事件实际触发时另读聚合item_aura层并要求正值，不能从最终装备光环映射重建这一过程。目前记录是本次属性计算的回调账本，资源单独提交不得把旧账本当新事件重放；BasicAura即时程序、逐源状态安装／撤出及计时器候选已集成，完整基础／状态回调、物品事件／光环组合和单位事件仍待闭合。

佣兵装备／创建、铁魔材料来源准备接到共享事务内的逐键回调。活跃来源轨迹由同一MonsterResourceTrace持有；即时光环对拥有者及嵌套返回目标的列表写入参与后续属性传播，不再只在最终装备属性上执行账本。计时器直接共用候选，状态列表采用State来源投影并在查询时替换；物品投递／撤出和召唤被动更新保留不可变快照，提交前清除来源程序及活跃借用。装备组合、完整即时PLR、物品技能／单位事件链和生命周期仍需继续核对；原生周期事件入口见[内核子系统](../../modules/SERVER_SYSTEMS.md#原生帧事件迁移)。

ItemAuraTimer候选已沿物品GUID保存激活／撤销顺序：激活先取消同GUID全部事件、采样当时PerDelay并排期，再执行已支持的immediate；撤销先撤出AuraState来源、传播资源／被动回调，再取消事件。SkillFormulaSnapshot保存当次回调的来源、属性、资源和等级，资源单独提交不重放账本。玩家及单位的状态来源、接收者、计时器在共同候选中复验修订并随输出接受发布，成组及嵌套回调累积同一候选，入场使用真实逻辑帧。普通光环、装备光环、佣兵与怪物使用公共原公式、列表／资源轨迹和逐目标作用／伤害游标，不能由此推定完整物品事件与生命周期已复刻。

飞弹／周期模块把这些计时器接入GameSystems.frameEvents的PeriodicStats 9；effects/item_auras逐GUID事件读取当前聚合等级，保留跨背压阶段并在程序结束后采样当前PerDelay重排。旧全单位光环扫描已撤出；玩家右手及单位技能周期分别使用PeriodicSkills 8，完整原事件优先级、物品队列2交错及同步重入仍待闭合。共享来源与算法入口见[COMMON](../skills/COMMON.md#当前持续效果入口)。

当前Weapons的sst短法杖单手mindam／maxdam为空，双手2handmindam／2handmaxdam为1／5；纯双手武器不能被要求同时具有单手伤害。prepareNativeEquipmentSources现在独立准备单手、双手通道，合法空白取0且不借用另一通道，也不因劣质最低伤害补出不存在的近战通道；投掷通道仍以原MaxMisDam门处理。全武器品质／无形组合仍需原版对照。

需求使用公共nativePercentage(base,pct,100)后原signed32加值及无形减10，不将负需求钳为零；原资格还要求单位力量／敏捷各自大于零。数量拒绝只针对原主类型book；D2X书本的原QUANTITY保存在charges，普通装备／弹药不因实例quantity为零统一失去属性贡献。固定1.13c D2Common.dll（MD5 `ee1238806ef6d6d9801d12a09d128fe1`）ordinal10244／RVA26DB0，26E7F–26E9F调用RVA11E0共享比例，26EBF–26EC9无形减值，26EDD起确认正属性条件；27040–27059仅为怪物hcIdx0x230／0x231接受原bar职业例外。佣兵携带真实nativeClass，不用人物职业外观代替该条件。箭袋活动贡献通过quiverReady按当前组另一手的原Quiver类型判断；1.13c D2Game D1830–D188E将非装备操作的资格与箭袋条件组合，库存更新D51A0随后按体位处理。铁魔材料的原强制装备通过显式skipRequirements策略保留，已鉴定／金属资格仍由材料入口复验，不伪造力量／敏捷以通过公共需求。

玩家装备预览及RemoteInventory提交沿当前MPQ ItemTypes.Class／Equiv1／Equiv2继承职业限制；例如女巫持刺客爪应显示WrongClass并在客户端拒绝该装备意图，不等待不存在的通用库存ACK。服务端仍复验最终资格，拒绝的装备草案不改变Cursor或现有装备。保留光标与原版拒绝行为见[协议参考](EVIDENCE.md#职业装备与原版拒绝对照)。

套装按不同SetItems记录身份计数，同一部件的两个实例不重复计数，条件随机值在生成时冻结；addfunc=1按特定其他部件、addfunc=2按不同部件数激活。公共activeSetItemLayers两端使用，依据D2Common Items::sub_6FDA4380；客户端仅消费已知属性，不安装权威贡献。普通、符文之语和已激活套装层的技能来源沿同一有效装备资格收集。

## 手位与双手剑

`equipment_rules`共用装备双手冲突和伤害握持资格，客户端准备原请求，服务端最终校验。MPQ ItemTypes的BodyLoc1／2允许盾牌、弓／弩及箭袋装在任一手位；弓／弩只能配相应Shoots／Quiver类型，不能配盾牌或另一件武器；射击从持弓手的另一侧扣除弹药。盾牌只能配可单手使用的武器；两面盾和两种不匹配弹药不能共存。切组沿当前活动武器组判断，不把I组的另一手误算到II组。

野蛮人仅对Weapons.1or2handed标记的武器拥有单手例外，当前原表是双手剑；双手斧、长柄和弓／弩不因职业放宽。单独持双手剑取2handmindam／2handmaxdam和2handedwclass；另一手装备武器或盾时取mindam／maxdam和wclass，两武器使用原双持动画类。另一手是否占位按实际装备记录判断，损坏或需求失效只停止属性贡献，不让双手剑自动改成双手握持。佣兵不获得玩家野蛮人的该例外。

物品说明以查看角色职业为上下文：野蛮人查看原1or2handed武器同时显示单手和双手伤害，其他职业仅显示双手伤害。装备后的角色伤害和动作按实际握持计算，不用说明中两组基础伤害替代人物数值。选角与世界原组件投影也按剑盾／单剑选择正确武器类，保留原图。

依据本地D2MOO `Items::ITEMS_Is1Or2Handed`、`Inventory::INVENTORY_GetWieldType`、`ItemMode::sub_6FC43280`及当前MPQ；原D2S v96仍保存既有body位置／武器组，没有新增握持字段或私有段，重入从原装备位置推导。没有存档格式迁移；完整握持、动作与保存组合仍待核对。

## 入口与边界

[equipment_loadout](../../../src/gameplay/items/equipment_loadout.cpp)、[contributions](../../../src/gameplay/items/equipment_contributions.cpp)、[activation](../../../src/gameplay/items/equipment_activation.cpp)、[requirements](../../../src/gameplay/items/equipment_requirements.cpp)提供纯计算，server/attributes与inventory/equipment规划总值及位置，transactions提交；内容只由hosting准备。武器手位／类型与六通道伤害不在本页再实现。

武器、防具、盾、腰带、靴／手套、戒指／项链、护符及职业专属基础类型共享资格框架，仍须逐条登记特殊属性与消费者。玩家吸血／吸魔／压碎／撕裂已接公共[武器命中效果](../combat/WEAPON_EFFECTS.md)，按实际出手武器收集本件贡献。完整双持组合、专用触发和部分职业消费者仍有缺口；能显示属性不能算该效果已执行。四类佣兵装备服务与自身属性贡献已接，类型限制和有限换装证据见[佣兵](../characters/HIRELINGS.md)；女武神装备准备不代表本人或佣兵全类型支持。

全属性、全部套装组合、技能来源失效、多人及三难度的装备／切组与保存流程仍待逐项原版对齐。
