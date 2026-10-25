# 物品界面

PD2物品外观、数值提示、容器及CN共享／材料仓库的客户端行为；权威物品规则见[物品与属性](ITEMS.md)。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="物品界面布局与cn容器"></a>

## 物品界面：布局与CN容器

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

固定CN配置的RemoteDisplay也准备七职业Inventory布局，按invTop／invBottom校验自身面板高度，投影panelTop／panelHeight、backpackLayout和equipmentSlots。当前表的职业行均为invTop=32、invBottom=492、10×8格；背包相对坐标(15,217)，格距29。原版仍保留60／432的布局边界，不因PD2放宽原版约束。

invchar6右半四帧按左半原图提供的行基线、各帧尺寸及有符号x／y偏移绘制；CN右下帧高246、y=35，与原版176高不同，不能继续按固定256像素顶边拼接。装备、格子命中及物品原图共用目标表投影；具体资源摘要见[DLL与位流证据](../resources/PD2_CN_DLL_ANALYSIS.md#远端显示准备的当前边界)。当前已显示初始法杖、底部两行卷轴与腰带药水，原客户端逐像素及全部分辨率对照尚未完成。

远端提示读取Gems固定属性、孔内服务器列表和实际装备激活的套装保存层；SetItems／Sets元数据与原版共用导入，随机奖励缺值时仍未知。未装备的同名套装副本不借已装备件的条件层，原符文之语列表不重复叠加；完整PD2最终攻防与装备资格仍不由本地计算。

PD2 InventoryView标记personalItemActionsOnly：开放本人背包、Cursor、地面、当前装备及腰带的移物，位置与GUID／revision由原回包确认。穿脱及同部位换装沿用目标MPQ的类型／部位／职业检查；最终属性需求和装备生效由服务器决定，不用原版派生值假定CN资格。0x1B／1E组合核对另一只手的GUID／revision，双交换还检查腾挪背包空位；W走原0x60请求并等待0x97确认。腰带允许取出、指定格放置、交换和Shift自动入带，容量由当前Belts表及已指派腰带准备，药水通过当前表的pSpell／useable和类型白名单提交，不执行原版本地药效公式。

stash／cube按当前表读取10×15／4×4和原DC6，收到0x77才开放物品访问。CN候选已改用mdll1的个人／账号共享控制链，切类型及相对前后翻页等待对应类型与页号确认，页号保留32位；物品按类型和页隔离。读取原PlugY/StashBtns.dc6的类型切换、前后箭头和页码，支持滚轮逐次翻页；缺图不显示或命中按钮，不沿用核心十页签。首次打开发一次原3A/1B初始化；确认前不猜测个人／共享域、不显示仓库物品位置，仍允许切共享首页取得确认；换页等待禁止移物，精确页请求超时撤回仓库访问；索引／边界无变化且后续GS流量正常时保留上次确认页，标记无ACK，不伪造新页。核心普通缓存几何只用于尚未进入CN扩展域的已知映射，未知缓存或detach隐藏位置。已通过实服个人／共享切换、共享1→2→1及原有卷轴存入、跨页隐藏、返回重现和取回；原版卷轴存取及关闭按钮回归通过。已选首页可不重复回确认；返回已有页可使用按域和页隔离的已知缓存，不要求每次重发全部物品。跨角色与HC边界仍待验证。0x2F的+6是detach目标页，不能当作公会选择器。已开放确认交易会话中的本人报价容器、七槽佣兵装备、个人金币、方块合成与NPC服务；对方报价、公会写入和账号共享金币仍不可写，共享余额独立只读，双客户端交易及佣兵实服组合待验；指定消耗品使用、普通品质合并、装书、鉴定和镶孔沿现有身份及回包链开放，复杂容量／品质组合仍保守拒绝。UI预览和意图、RemoteInventory及RealmSession均复验来源、目标、当前部位与revision；不能通过原包入口绕过界面限制，不产生本地库存成功。CN拿取／卸装须等本人Cursor，穿戴须等指定部位，腰带安置须等指定格；换装／腰带交换还须等被换物品进入Cursor，相关属性或0x47／48刷新不单独确认完成。存储放置／交换需等精确容器与坐标；鉴定等待源的0x3F后提交目标，并等目标identified标志；装书、镶孔和堆叠分别核对其结果身份。I／原底栏可开关，ESC可关闭。CN背包／仓库的关闭按钮按当前原图底部凹槽及准备后的面板高度定位，绘制与命中共用几何；个人金币显示在原图栏位，个人仓库已确认同步且钱包余额已知时允许存入；银行余额已知才允许取出。CN可省略初始零银行属性，存入时只用真实钱包限定输入，不补造银行零值或剩余容量，由服务端裁决容量；共享余额未知时留空且不提交金币事务。仓库原StashBtns提供上方个人／共享切换、前后索引与前后页；Shift+页箭头跳首页／最后非空页，Shift+索引跳主索引。底部buysellbtn 28／30帧切共享材料页，使用TradeStash第4–7帧；缺原图不开放切换。材料按独立代码计数显示，名称／尺寸／图标读取当前MPQ；左键取1到鼠标，右键取1由服务器自动安置（药剂可先入腰带），Ctrl+右键5，Shift+右键50（rv药剂20）。手持普通材料点击材料页提交存入，等待服务器移除物品及更新计数；不生成虚构GUID，不本地扣材料。材料模式隐藏普通页物品和落点。原stashmats_transmute_btn提供3:1转换箭头，普通／Ctrl／Shift分别请求消耗3／15／150个源材料；只开放原生rvs→rvl、skl→skz、gzv→gpv及gl*→gp*（不含glv）配对，等待源扣减与目标增加事实。Ctrl+Shift取出进入方块，回复药剂仍可由服务器优先安置腰带；要求本人持有真实可访问的方块，避免物品进入暂不能打开的容器。此前把CSL／CSR解释为地面转移不正确；它们不是地面请求。材料批量存放与索引标记已接，页改名与共享金币转账仍不开放。确认页名／默认页号显示在原图第二字段。此为D2X适配布局，未宣称与原CN全部分辨率逐像素相同；切组覆盖图仍待对照。原版操作按下节原有流程保留。

<a id="物品界面属性与特殊外观"></a>

## 物品界面：属性与特殊外观

来源：[docs/gameplay/items/PRESENTATION.md](../gameplay/items/PRESENTATION.md)。

物品交互及属性描述中的MonStats身份已按版本区分：固定PD2使用编译记录顺序匹配服务器classId和descfunc23的怪物参数，原版保留既有hcIdx匹配。入口为RemoteInventory的NPC服务身份查询和describeItemStats的怪物类型文案；沿用[TXT／BIN身份依据](../resources/PD2_CN_DLL_ANALYSIS.md#地图准备中的txt与bin语义)，不因TXT hcIdx缺号误选商店／任务NPC或词缀名称。仅静态核对，未运行验证。

当前目标表的腐化（corrupted）、亵渎（desecrated）用descfunc3，deep_wounds用1，地图词缀大量使用6–9；这些编号已有公共描述路径，不能仅因属性新增就推断需要新算法。item_shiny_appearance的descfunc30文案已接：以真实物品记录索引查核心23项配置，读取当前TBL的StrAnniversaryEdition／StrSpecialEdition，按原属性优先级排序；无配置不输出猜测文本。两版索引逐项相同，证据见[特殊文本选择](../resources/pd2-core/ITEM_FUNCTION_REVIEW.md#2ecfa0特殊文本选择)。22种静态特殊版库存原图已接：已鉴定暗金且服务器属性270／参数0为正时，在普通暗金图标之后选择核心配置的原图；文案与图名共用item_appearance中的配置，当前pd2assets.mpq包含全部22张DC6。未匹配、未鉴定及原版不覆盖；地面动画保持原路径。Ormus动态选择及特殊调色见下文，实际画面对照仍未完成；BH腐化星号受范围显示设置／Ctrl限制，不作为默认核心属性标签移植。

已接lifedrain_percentcap（当前表ID488）的显示变换：普通属性计算值先取`100 - value`，再按目标ItemStatCost的descfunc7、descval2和原TBL文案生成提示；不改网络值、StatList或战斗计算。依据为旧核心SHA-256 `9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f`的D2Client调用位移54B3B／54E65→39E760→2ED0C0，及更新核心SHA-256 `17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57`的2EED70：均比较ID0x1E8后用100减原计算返回值。实现限定remote-display-txt-v1及目标属性名称，原版路径不变；已构建，尚未实物截图核对。

Phasing SelfAura的专用物品提示已接：固定核心要求属性191、技能参数605且值为1，使用当前TBL的`StrPhasingStatDesc`，而不是普通等级灵气格式；其他技能／等级和原版保留公共路径，缺专用字符串时保留普通描述。依据为旧核心39E720→2ECFA0及更新核心2EEC50，均比较BF／25D／1；更新版字符串RVA449DE0为该文本键。605是目标核心特判的技能身份，不用本地化技能名称匹配；不改变灵气执行或物品属性。只完成静态核对，已构建，未逐项运行验证。

Ormus动态库存原图已接：魔法属性列表的首条107单技能按参数排序确定，从当前Skills读取EType，闪电／冰冷分别选择invormus_l_se／invormus_c_se，其余有效技能选择invormus_f_se；缺单技能、参数0或无效表记录保留普通图。三张DC6均在当前pd2assets.mpq。原生列表来源及两版分支见上述逐函数证据，实际画面对照仍未完成。

特殊调色已接：命中特殊版时库存保留原图色，不叠普通暗金染色；地面按核心配置，Ormus按火／雷／冰使用颜色8／13／5。核心库存颜色FFFFFFFF低五位为31，但原D2CMP对>=21返回空调色指针，不能当作DAT第31行。染料属性transform_dye（477）在未命中特殊版时覆盖普通染色，正值1–21对应颜色0–20，其他正值取0；组来自当前基础表，原版保持不变。原生定位及资源边界见[调色证据](../resources/pd2-core/ITEM_FUNCTION_REVIEW.md#2f9580特殊版与染料调色)，实际画面对照仍未完成。

ID489显示已接可选主人上下文：只在明确归属且收到主人属性184时，按原32位乘法回绕和有符号上限40计算，不从生命值猜missing_hp；未知事实保留原值，地面／下落物品不借残留owner。它不修改属性或战斗效果。主人属性同步覆盖及腐化装饰仍待核对，不能据此宣称全部PD2物品完成。外观和动态属性修改已编译核对，未做游戏内验证。

充能恢复item_replenish_charges（422）在当前表为descfunc3／descval0，已有通用路径直接输出ModStrRepCharge1。当前英文patchstring.tbl记录2156为完整文本“Replenish 1 Charge in 3 Seconds”，不含格式占位符；记录2157的ModStrRepCharge2为“Charges per 100 seconds (do math)”，不是该程序必须追加的后缀。不另造按数值计算的恢复提示，也不修改服务器恢复计时；其他语言的实际文字和显示仍待对照。此前将此项列为必需专用格式缺口的判断已撤回。

<a id="物品界面远端显示准备"></a>

## 物品界面：远端显示准备

来源：[docs/gameplay/items/PRESENTATION.md](../gameplay/items/PRESENTATION.md)。

PD2沿相同只读投影显示装备、背包、腰带及地面物品。RemoteDisplay的loadRemoteItemDisplayData只准备服务器索引所需的暗金／套装／词缀／劣质名称和特殊图形、符文之语名称，不套原版spawnable／enabled／lvl生成过滤，不准备掉落、配方或随机属性。未鉴定物品继续省略未下发的特殊索引；镶孔子项保留原所有权及指派顺序。CN preview标志和扩展storage不会当作普通库存。完整网络分支、compact独立列与拒绝条件见[PD2位流](../resources/PD2_CN_DLL_ANALYSIS.md#完整物品位流版本与字段)，面板及受限移物操作见[包裹界面](ITEM_UI.md#物品界面布局与cn容器)。

<a id="物品界面提示边界"></a>

## 物品界面：提示边界

来源：[docs/gameplay/items/PRESENTATION.md](../gameplay/items/PRESENTATION.md)。

PD2提示只描述已解码的名称、数量、耐久及已支持的原表属性描述，不借用原版程序推导最终攻防／装备资格、激活套装层。符文之语附加列表按服务器值保留；镶孔修饰未齐全时明确提示。属性描述函数覆盖、腐化／旧格式混合、全部特殊图及地面落地表现仍待目标会话对照；本项代码接通不代表完整物品兼容。

<a id="物品界面交易范围"></a>

## 物品界面：交易范围

来源：[docs/gameplay/items/PLAYER_TRADE.md](../gameplay/items/PLAYER_TRADE.md)。

固定Normal／TCP配置沿[公共交易流程](../gameplay/items/PLAYER_TRADE.md)的原邀请、金币报价、确认及取消请求；只有服务端确认Open且本人尚未确认／无挂起响应时，允许本人native page3报价容器取放。对方page2镜像仍只读，关闭、锁定、代次过期和未知位置不得提交移物，不从NPC货架复用交易资格。尚未使用两个可同时联机且同模式的角色实测CN双方报价、满格拒绝、取消、断线和保存；原版权威与持久边界不变。

<a id="物品界面cn材料向方块转移"></a>

## 物品界面：CN材料向方块转移

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN材料向方块转移的宿主选择已补跨页边界：只接受完整解码、位置已知的本人背包方块或当前已确认仓库域／页中的方块。另一页缓存中的方块不能授予访问资格，发送层再次复验GUID、revision、当前页及同步状态；依据为既有CN按域／页隔离与真实可访问方块前置条件。本项仅静态核对，未运行验证。

<a id="物品界面cn材料页已打开时"></a>

## 物品界面：CN材料页已打开时

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN材料页已打开时，Ctrl点击原材料页开关可批量存放背包中的已解码普通材料，复用逐件原生存入请求；这是D2X的批量入口，不是新增批量协议。每件均复验身份、版本和实际计数，收到扣物与计数增加才继续；超时、拒绝、关闭仓库、换区或死亡停止余下队列，不自动重试。3:1升级等待源扣减与目标增加均符合本次转换量。

<a id="物品界面cn普通仓库页名栏"></a>

## 物品界面：CN普通仓库页名栏

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN普通仓库页名栏点击依次切普通／索引／主索引，等待服务器flags确认。页名只读，Shift点击和`/rp`明确拒绝改名，不发送聊天或改名消息：目标服务在两次清空名称请求后紧随断流，兼容性尚未闭合，不能由原生C580静态入口推定线上支持。共享余额与个人goldbank分开，只读共享／材料页的真实余额；固定CN模块共享转账分支为空操作，因此不开放共享存取。

<a id="物品界面普通药剂之外固定cn配置"></a>

## 物品界面：普通药剂之外，固定CN配置

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

普通药剂之外，固定CN配置还接受当前表pSpell8经验药剂、pSpell9草药及pSpell13的原无限鉴定／回城书右键使用。无限鉴定书由服务器批量鉴定，不进入普通卷轴的目标光标；无限书不靠“消耗源物品”确认，原请求记SentNoAck，物品与传送门仍只读服务器结果。技能目录及无限回城快捷入口已接，编号读取当前Skills，固定PD2要求服务端非零物品技能数量，不以普通技能等级代替来源。地图卷轴pSpell12、未知使用程序、全部复杂品质／旧格式组合及跨角色保存继续保留明确边界；上述特殊物品使用尚未完成实服验收。

<a id="物品界面远端物品技能目录已补"></a>

## 物品界面：远端物品技能目录已补

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

远端物品技能目录已补当前Skills中InGame=1、srvdofunc113、scroll=1且有SkillDesc的非职业记录：目标表为四种原书卷及无限鉴定／回城书。内容目录与RemoteCombat不再只准入CharStats公共动作，选择／快捷绑定仍由服务器itemSkillQuantities决定可用性，未收到数量或数量为零时不能使用；不加入固有技能、不伪造等级、不开放地图技能。名字和图标来自当前资源，右键无限书路径保持不变。此项不认证服务端何时授予／撤销无限书来源或全部施放行为，实服闭环仍待验证。

<a id="物品界面pd2普通品质合堆"></a>

## 物品界面：PD2普通品质合堆

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

PD2普通品质合堆的保守属性比较不再依赖网络列表顺序：按ID、参数和值比较完整多重集合，重复项数量仍须一致；两端必须完整解码并匹配当前GUID／revision，源数量必须非零。D2MOO `ITEMS_AreStackablesEqual`按属性值查询而非序列化顺序；当前尚未据此放宽PD2的整组属性相等要求或扩展品质准入，最终合并由服务端决定。

<a id="物品界面cn材料初始化"></a>

## 物品界面：CN材料初始化

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN材料初始化按内容层准备的原98项代码集合核对，不再仅比较计数字典大小；目录基底必须存在于所选MPQ，网络层只消费准备后的代码，不读取资源。未知代码明确报协议错误，增量没有对应绝对基线时不自动从零计算；收到原初始化结束记录后，完整代码集合存在才开放材料操作。该机制不适用于核心85／86的196槽，也不推测其他版本材料目录。已构建，未逐项运行验证。

<a id="物品界面pd2方块合成绑定"></a>

## 物品界面：PD2方块合成绑定

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

PD2方块合成绑定提交时真实方块的GUID／revision，发送层再次核对与当前打开源相同；仓库中的方块要求已确认的当前域／页。内容层要求方块完整解码，所有已报告方块材料位置已知、完整解码、非preview／赌博项且占格在目标MPQ布局内；发送层复验位置与标志。未知物品不视作可参与合成的已知输入，但不在客户端判断配方、腐化概率或产物。此项只补请求准备与竞态边界，已构建，未逐项运行验证。

<a id="物品界面cn发送层与界面"></a>

## 物品界面：CN发送层与界面

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN发送层与界面共同限制个人金币只用于已确认且同步的个人仓库，排除共享页、材料页及翻页等待；不将原个人金币请求当共享转账。材料模式切换／存取也排除翻页等待，存入前复验提交时的服务器计数基线仍相同，变化后要求重新准备请求，不使用过期计数确认成功。未增加网络消息或重试，本项仅静态核对。

<a id="物品界面pd2合堆请求"></a>

## 物品界面：PD2合堆请求

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

PD2合堆请求的容量检查已补目标物品实际`item_extra_stack`属性，按当前ItemStatCost名称识别零参数项，加目标基底maxstack后封顶511；非正容量拒绝，不猜测词缀范围。UI拖放预判通过原兼容性检查后，同样继续使用完整物品的已知属性计算容量，不再提前按基础maxstack返回。依据为D2MOO `ITEMS_GetTotalMaxStack`，不扩大当前普通／劣质／超强品质与兼容属性列表的准入范围；最终数量仍由服务端确认。本项仅静态核对，已构建，未逐项运行验证。

<a id="物品界面pd2方块宿主"></a>

## 物品界面：PD2方块宿主

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

PD2方块宿主的访问租约随物品副本失效：普通9C／9D更新把宿主移到Cursor、地面、其他所有者或不可访问页，或2F扩展位置／重建使其位置未知时，清除已打开及待打开方块、增加交互代次并中断Pending物品请求。只接受本人背包或已确认同步的当前仓库域／页，保留服务器实际物品，不做本地回滚。77打开确认、CN页回执及核心页回执后共用同一宿主复验；先前已撤销的待打开状态不会因迟到确认重新开放。入口为remote_world.cpp的invalidateCubeHostPd2，原版不变；本项已纳入客户端构建，相关失效／迟到确认组合尚未实服验证。

<a id="物品界面cn材料存入"></a>

## 物品界面：CN材料存入

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN材料存入的物品回包和计数增量回包共用相同确认条件：同代码计数必须高于提交时基线，且源已移除或已收到更新revision的原移除动作；仅收到负增量不足以确认。计数曾回退后再增加但未超过基线时，不删除缓存源、不结束Pending请求。本项已纳入客户端构建，计数回退及双回包交错尚未实服验证；Updated仍表示相关服务器更新而非所有业务组合成功。

<a id="物品界面基础武器速度编译"></a>

## 物品界面：基础武器速度编译

来源：[docs/gameplay/items/EQUIPMENT.md](../gameplay/items/EQUIPMENT.md)。

当前PD2 weapons.bin为319行、步长0x1A8，2hs行33的+D8为0；非空但无效的数字仍保持未知。

<a id="物品界面类型继承与职业资格"></a>

## 物品界面：类型继承与职业资格

来源：[docs/gameplay/items/EQUIPMENT.md](../gameplay/items/EQUIPMENT.md)。

PD2的knif可继承asm用于类型匹配，但不因此把普通匕首／飞刀限制为刺客专属；远端穿脱与宿主内容准备共用此边界。

<a id="物品界面材料取出确认"></a>

## 物品界面：材料取出确认

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

CN材料取出在发送时记录计数与同代码物品revision，等待最终计数扣减及目标容器内的新物品／已有堆叠刷新共同确认，两类回包顺序不限；方块宿主刷新不算材料到达。依据为mdll1 1E670先补已有堆叠、再经26D40创建／安置、循环结束发送27；回复药剂允许优先入腰带。空间不足的部分取出仍按实际扣减确认，不补造剩余物品；UPL升级继续使用双计数确认。尚未实服验证本项时序修改。

<a id="物品界面方块关闭位置"></a>

## 物品界面：方块关闭位置

来源：[docs/gameplay/items/INVENTORY_UI.md](../gameplay/items/INVENTORY_UI.md)。

PD2关闭位置由固定核心补丁准备，原版保留自己的位置。
