# 库存、物品与掉落

原服和自研宿主共用RealmSession／RemoteInventory／RemoteUiClients、原1.13c物品包和公共UI。物品、金币、容器、装备效果与交易结果属于连接的权威服务端；客户端只读已收到的属性并提交原请求。

## 领域与内容边界

| 入口 | 职责 |
| --- | --- |
| server/systems/inventory | 位置、装备、数量、使用、鉴定、仓库／方块授权、武器与防具耐久、自恢复 |
| server/systems/items／loot | 地面库存、落点、来源去重、生成准备、唯一物品记录、掉落事实 |
| server/systems/merchant | NPC共享货架、个人赌博货架、买卖／回购／批量、维修与鉴定事务 |
| server/systems/crafting | 镶嵌与普通方块配方的有界准备、复验及原子提交 |
| server/systems/attributes／transactions | 总值和装备资格计算；库存／人物／世界值统一提交 |
| hosting/item_content、loot_content、merchant_content、crafting_content | 读取当前MPQ，生成及冻结实际掷值、准备不可变规则 |
| gameplay/items、gameplay/loot及content/items纯函数 | 类型／品质、词缀、StaffMods、NoDrop、配方匹配、需求、属性、套装与报价 |
| persistence/d2s_* | 原v96读写及保存值校验；不属于客户端 |

GameInstance只组装这些领域，不持有第二套商店或库存规则。内核不读取MPQ、GPU、输入、文件或Win32。准备结果绑定请求token、种子、库存／人物revision；提交前复验所有权、交互、区域代次与容量。失败不消费材料；Outbox背压保留准备身份，不重新抽奖。库存、人物与必要世界事实成组接收后才提交状态。

## 已接入的执行路径

背包、Cursor、腰带、装备、两组武器、普通交换、双手冲突及腰带缩容；合堆、卷轴／同类书转装；普通拾取、金币部分拾取／丢弃及仓库存取；药剂、鉴定卷轴／书本和Cain批量鉴定均沿原包执行。身体需求由公共loadout／contributions／requirements计算，不重复维护力量／敏捷公式。未鉴定、损坏、非活动手部不贡献，护符只在背包生效；套装按不同原件身份计数。

合堆校验基底、品质、file index、无形、物理伤害及无孔条件；普通／超强／劣质可合并，魔法品质不合并。最大数量取原基础加item_extra_stack并复验9位上限。书本使用Books配对／charges，根数量仍为1；新生成页数按原minstack／spawnstack选取，网络和磁盘数量映射为页数。普通移物无法安置时保持原库存；自动拾取允许提交实际合并量并保留地面余物。数量归零按原品质／item_throwable规则保留或移除。

待拾取请求绑定本次落地generation、人物／区域代次和走跑意图；服务端自恢复及原堆叠余量可以更新物品revision，接近完成后取同一次落地的最新句柄。移除、拾走后再次丢下或位置变更使旧generation失效。GroundLifetime与过期时钟由items持有，不增加客户端协议，也不忽略所有权或区域复验。

P1边界继续复用Draft与transactions。普通地面拾取先装入背包已有书本或原AutoStack堆叠，再按已鉴定／需求／空手位尝试自动装备，最后入带／背包；Cursor拾取不合并、不自动装备。合并后余量无法安置时留地并更新数量／页数，整笔无变化则拒绝。争用复验原句柄；接近／背压等待绑定人物、区域代次和走跑意图。新拾取物品直接装备时发原9D Equip。

腰带收缩先移背包，余药按原ItemMode::sub_6FC45930落地；同一规划供脱下、换带和商店出售使用，实际落点／世界容量失败时不提交任何一边。尸体回收按原装备／腰带／背包顺序，余物保留，不套地面书本／堆叠合并；master旧collect桥的此项行为未沿用。第一具非空尸体保存规则见[SAVES](SAVES.md)。删除物品时事务同步裁剪EquipmentRules，避免耗尽药剂／卷轴遗留无主规则。

鉴定支持授权有效的方块／仓库目标，属性和耗页一次提交；空书、重复鉴定、非本人目标、非空Cursor不消费。F1–F8绑定、切组及物品来源技能继续用原技能／数量更新；原包清除热键独立于Attack ID0。P1源码随P5及物品身份收尾统一构建打包；列出的有限冒烟见基线，其余新增路径仍为V1。P1修正未改客户端。

普通TC、正负Picks、层级升级、品质／MF／GF、暗金一次生成限制、失败暗金／套装耐久倍数与装备架路径共用旧单机迁入的生成器。NoDrop采用原有效人数指数和怪物生成时人数上限；当前未建立队伍，因此没有近队友的额外人数贡献。普通箱桶及怪物生成的新物品发布GroundDropFact，手动丢弃同样发布；原9C action=2驱动现有flippy／声音，静态地面同步用action=0不重播。

全品质1–8生成、词缀、稀有名、Crafted、暗金／套装、无形、自然孔、StaffMods、AutoMagic、孔内和符文之语保留实际值。原品质9 Tempered支持导入／保存及原网络投影，不自行增加MPQ没有的生成配方。AutoMagic原包／D2S为表内一基索引，报价与需求使用同一身份；不能按后缀／前缀合并索引解释。条件套装随机值在生成时冻结，激活不重掷。基础最大耐久和属性修饰分开，读写／提示／维修共用实际最大耐久，避免保存后重复加成。

镶嵌收原0x28，Cursor中宝石／符文／珠宝加入合法已鉴定宿主；固定宿主GUID、真实子项和顺序，按当前Runes核对符文之语。方块打开收pSpell=7，原77确认；原4F子24提交合成、子23关闭。仅持有背包方块且授权有效时访问page=3；普通CubeMain输入、品质、等级、难度、职业、升级、重置、修复／充能、打孔／去孔和Crafted使用原表，输出与全部材料一次提交。限制见[方块](../gameplay/items/CUBE_AND_GOLD.md)。

商店共享有限／永久库存；卖出原件进入回购货架，买入分配新根／子GUID，保留已分配属性及套装规则。全部玩家离开原城镇后刷新普通货架；240000ms原周期转换为6000个权威帧，只标记待刷新，下次单人交谈重新打开时准备货架，有其他观察者时保持原库存。merchant.step已进入固定步，不依赖客户端打开触发清理。赌博为各玩家独立预生成14件隐藏货品，实际品质不泄漏给客户端；原交易类型2购买。原0x32 mode高位multibuy支持Shift购买：药剂填腰带、卷轴填书、堆叠物填可兼容堆叠剩余容量，资金不足按可买数量处理；赌博不批量购买。服务端重算报价，不信客户端金额。

普通成功近战4%武器耐久损耗、防御命中10%加权防具损耗、Impale专有数量消耗、投掷数量和充能维修已接；不可摧毁／无形等条件沿原规则。耐久变化发原3E基础属性及必要资格更新。公共items/replenishment按原优先级先自修耐久，再补数量；未鉴定不禁自恢复，破损不自行修复。初次2500/rate+1帧，后续至少125帧；时钟在items按GUID持有，地面与本人库存转移保留时钟。无主地面恢复不伪造原主人3E，后续准入／拾取携带真实值。

充能技能保留原层skill<<6|rank、原次数与源GUID；3C／51／23／7B、本人原物品位流及3E共同管理选择／热键／次数。充能释放免普通耗蓝，资源与次数同笔扣费，Inferno起始一次扣费；源失效显式取消，不回退到同ID普通技能。普通、符文之语和已激活的物品套装层沿同一资格贡献收集来源；套装addfunc=1按特定其他部件、addfunc=2按不同部件数激活，公共activeSetItemLayers用于两端，依据D2Common Items::sub_6FDA4380。保存沿原204层值，运行GUID不写D2S。客户端从原物品解析提供技能栏、热键与本人伤害显示，同样用于原D2GS。

装备触发由effects的有界ItemEventPlan接Attack／Hit／Kill／GetHit／Death／LevelUp，复用已实现技能程序并发原99／9A；概率只掷一次，输出背压不再次执行效果。hosting读取ItemEffect／ItemTarget／ItemTgtDo／ItemCheckStart，空ItemEffect不施放；本人／随机落点与防御方Oculus传送按SkillItem规则区分。Skills::Handler仅在a6／a7置位且ItemEffect>1时用替代Do程序；攻击触发的替代程序与已准备SrvDo不同时明确暂缓，不能沿普通技能执行。其他职业未实现程序、攻击方ItemTgtDo作用于怪物、随机尸体及额外start-check保持明确暂缓，诊断itemTriggers.deferred记录原因。事件框架不等于全部装备触发已实现。

Charsi灌注、Larzuk打孔、Anya署名使用原38(action=0,npc,item)及7字节58；空Cursor先沿既有移物序列取原件，58是服务结果，不把9C／9D当领奖确认。联机菜单按原个人任务字的RewardPending显示，沿master已有选择界面与MPQ说明，两种服务端共用。服务端复验交谈revision、个人难度阶段及原物品资格；真实掷值、材料、任务阶段与产物一次提交，满背包按原落点落地，未授予任务奖励不能加工。原operation28组装、牛门／Pandemonium与Token见[方块](../gameplay/items/CUBE_AND_GOLD.md)。技能书、生命药剂、抗性卷轴只消费已有个人一次性资格，奖励数值仍由原任务记录派生。

地面寿命采用ITEMS_GetGroundRemovalTime：任务物品不消失；稀有／套装／暗金／Crafted或超过10000金币45000帧，孔内填料30000帧，其他15000帧。可靠0A移除接受后清理世界物品和规则。首杀TC读取MonStats的TCQuestId／TCQuestCP及击杀时个人原任务字，不因异步生成或之后任务变化重新选择TC。

merchant准备请求及打开货架绑定交谈revision，持续复验NPC距离／视线、人物／区域代次；关闭／重开同一NPC、死亡／换区／离线使旧请求失效。成交前复验商品、库存及人物版本；出站背压保留待成交请求／种子，失败不会先扣金币或先移物。

## 客户端与原版依据

RemoteInventory消费9C／9D、0A、3E、2A、58、77、97；UI预览位置、Cursor及GUID／revision，发送前复验连接／游戏／区域／交互代次。Pending／Updated不等于全部成功；买卖维修等待2A，任务加工等58，未知不自动重试。孔内首次指派顺序不按GUID重排；截断位流decoded=false并禁止操作。

itemTradePrice／itemGamblePrice两端共用，只对完整已知值报价。自动词缀、未鉴定、StaffMods、词缀、孔内、符文之语、触发／充能、按时间属性、耳朵／身体部件、自修复与投掷售价按原字段计算。未知孔内掷值不以seed=0重掷。原1.13c D2Common MD5 EE1238806EF6D6D9801D12A09D128FE1提供整数顺序补证：RVA 0x29947–0x2996f自修复，0x29beb–0x29bfb投掷售价，0x258d2–0x25934充能成本；书本按Books索引核对配对及CostPerCharge。

本地D2MOO证据：Items的NoDrop／品质／装备架／StaffMods／ethereal／耐久，ItemsMagic的词缀与赌博，SUnitNpc／SUnitProxy的货架及刷新，PlrTrade与CubeParser的材料和输出，ItemMods的真实属性与套装层，SCmd及D2PacketDef的原包。AutoMagic序列化依据D2Common ITEMS_SerializeItem；multibuy依据原0x32的15位mode加1位标志。先迁用master旧单机规则，参考代码只作证据，不提交。

原MPQ格子、图标、flippy、提示与声音共用公共表现。无形库存图标50%alpha；原始染色／逐组件透明仍未认证。拾取反馈必须核对请求前归属／数量及回包，单独移除或Updated不证明成功。显示限制见[SUPPORT](../gameplay/items/SUPPORT.md)，参数仅维护在[调试管道](../development/DEBUG_PIPE.md#联网物品操作)。

## 尚未完成与验证边界

第一幕六任务已提供任务物品、个人资格和奖励生产者，包括马勒斯授予灌注资格，详见[ACT1](../gameplay/quests/ACT1.md)。任务物品拾取沿ItemMode::sub_6FC425F0／sub_6FC428F0复验已完成记录与携带互斥；树皮／译文、马勒斯、腿与方块已开放，仓库不计互斥，尸体计入，其他任务拾取仍明确暂缓。其他幕完整任务资格／特殊固定奖励及牛王记录依赖P7／P8。近队友NoDrop／玩家交易依赖P3，完整佣兵与其余职业充能／触发执行依赖P6，Pandemonium内容依赖后续世界。完整原版事件命中条件、所有随机分支及原99／9A客户端专用ItemCltEffect表现仍待核对。原0x22分堆调用为空，不自造数量协议。耳朵／身体部件、Realm段与Tempered已支持原D2S布局，规则和未运行边界见[SAVES](SAVES.md)；这不代表PvP耳朵来源已经实现。缺失真实身份或随机值明确拒绝，不生成少属性物品。

这些边界不因导入原表或有stub而成为完成。构建与本批有限运行证据统一维护在[基线](../../BASELINE.md#当前运行包与有限冒烟)，保存约束见[SAVES](SAVES.md)；未覆盖的三难度、多人争用／背压、全部随机分支和原服逐整数报价不标为验收。
