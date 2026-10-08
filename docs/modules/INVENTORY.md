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

合堆校验基底、品质、file index、无形、物理伤害及无孔条件；普通／超强／劣质可合并，魔法品质不合并。最大数量取原基础加item_extra_stack并复验9位上限。书本使用Books配对／charges，根数量仍为1；新生成页数按原minstack／spawnstack选取，网络和磁盘数量映射为页数。容量不足保持整笔原库存；数量归零按原品质／item_throwable规则保留或移除。

普通TC、正负Picks、层级升级、品质／MF／GF、暗金一次生成限制、失败暗金／套装耐久倍数与装备架路径共用旧单机迁入的生成器。NoDrop采用原有效人数指数和怪物生成时人数上限；当前未建立队伍，因此没有近队友的额外人数贡献。普通箱桶及怪物生成的新物品发布GroundDropFact，手动丢弃同样发布；原9C action=2驱动现有flippy／声音，静态地面同步用action=0不重播。

全品质1–8、词缀、稀有名、Crafted、暗金／套装、无形、自然孔、StaffMods、AutoMagic、孔内和符文之语保留实际值。AutoMagic原包／D2S为表内一基索引，报价与需求使用同一身份；不能按后缀／前缀合并索引解释。条件套装随机值在生成时冻结，激活不重掷。基础最大耐久和属性修饰分开，读写／提示／维修共用实际最大耐久，避免保存后重复加成。

镶嵌收原0x28，Cursor中宝石／符文／珠宝加入合法已鉴定宿主；固定宿主GUID、真实子项和顺序，按当前Runes核对符文之语。方块打开收pSpell=7，原77确认；原4F子24提交合成、子23关闭。仅持有背包方块且授权有效时访问page=3；普通CubeMain输入、品质、等级、难度、职业、升级、重置、修复／充能、打孔／去孔和Crafted使用原表，输出与全部材料一次提交。限制见[方块](../gameplay/items/CUBE_AND_GOLD.md)。

商店共享有限／永久库存；卖出原件进入回购货架，买入分配新根／子GUID，保留已分配属性及套装规则。全部玩家离开原城镇后刷新普通货架；240000ms周期标志策略尚未接。赌博为各玩家独立预生成14件隐藏货品，实际品质不泄漏给客户端；原交易类型2购买。原0x32 mode高位multibuy支持Shift购买：药剂填腰带、卷轴填书、堆叠物填可兼容堆叠剩余容量，资金不足按可买数量处理；赌博不批量购买。服务端重算报价，不信客户端金额。

普通成功近战4%武器耐久损耗、防御命中10%加权防具损耗、Impale专有数量消耗、投掷数量和充能维修已接；不可摧毁／无形等条件沿原规则。耐久变化发原3E基础属性及必要资格更新；自修复／补充数量由25Hz领域时钟推进，破损物品不自行修复。原充能施法和装备触发执行仍未完成。

## 客户端与原版依据

RemoteInventory消费9C／9D、0A、3E、2A、77、97；UI预览位置、Cursor及GUID／revision，发送前复验连接／游戏／区域／交互代次。Pending／Updated不等于全部成功；买卖维修等待2A，未知不自动重试。孔内首次指派顺序不按GUID重排；截断位流decoded=false并禁止操作。

itemTradePrice／itemGamblePrice两端共用，只对完整已知值报价。自动词缀、未鉴定、StaffMods、词缀、孔内、符文之语、触发／充能、按时间属性、耳朵／身体部件、自修复与投掷售价按原字段计算。未知孔内掷值不以seed=0重掷。原1.13c D2Common MD5 EE1238806EF6D6D9801D12A09D128FE1提供整数顺序补证：RVA 0x29947–0x2996f自修复，0x29beb–0x29bfb投掷售价，0x258d2–0x25934充能成本；书本按Books索引核对配对及CostPerCharge。

本地D2MOO证据：Items的NoDrop／品质／装备架／StaffMods／ethereal／耐久，ItemsMagic的词缀与赌博，SUnitNpc／SUnitProxy的货架及刷新，PlrTrade与CubeParser的材料和输出，ItemMods的真实属性与套装层，SCmd及D2PacketDef的原包。AutoMagic序列化依据D2Common ITEMS_SerializeItem；multibuy依据原0x32的15位mode加1位标志。先迁用master旧单机规则，参考代码只作证据，不提交。

原MPQ格子、图标、flippy、提示与声音共用公共表现。无形库存图标50%alpha；原始染色／逐组件透明仍未认证。拾取反馈必须核对请求前归属／数量及回包，单独移除或Updated不证明成功。显示限制见[SUPPORT](../gameplay/items/SUPPORT.md)，参数仅维护在[调试管道](../development/DEBUG_PIPE.md#联网物品操作)。

## 尚未完成与验证边界

任务灌注／打孔／署名、特殊任务／首杀／固定奖励掉落、牛门／Uber配方及Token资格，装备触发／充能施法、佣兵装备、队伍／玩家交易权威、世界掉落自恢复／过期及完整周期刷新仍需相应领域接入。原0x22分堆调用为空，不自造数量协议。耳朵／Realm／Tempered的D2S布局仍拒绝；网络显示能解码不代表可保存。全套缺失真实随机值明确拒绝，不生成少属性物品。

这些边界不因导入原表或有stub而成为完成。构建与本批有限运行证据统一维护在[基线](../../BASELINE.md#当前运行包与有限冒烟)，保存约束见[SAVES](SAVES.md)；未覆盖的三难度、多人争用／背压、全部随机分支和原服逐整数报价不标为验收。
