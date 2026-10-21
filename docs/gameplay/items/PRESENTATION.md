# 原物品协议、图形与反馈

更新：2026-10-21。本页唯一维护原位流／公开信息、原图、声音及客户端反馈限制；权威规则按[物品目录](README.md)归档，存档归[SAVES](../../modules/SAVES.md)。两种服务端共用一套客户端，原请求参数见[命令](../../development/DEBUG_PIPE.md#联网物品操作)。

## 原包与成功条件

RemoteInventory消费9C／9D、0A、3E、2A、58、77、97；UI预览位置、Cursor及GUID／revision，发送前复验连接／游戏／区域／交互代次。Pending／Updated不等于全部成功；买卖维修等待2A，任务加工等58，未知不自动重试。

原网络物品位流没有JM头／seed／Realm尾，所有权来自原包；孔内首次指派保留真实宿主和顺序，不按GUID重排。未知／截断位流decoded=false且禁止操作，不补少属性物品。原品质映射和静态索引规则见[DATA](DATA.md)，服务端D2S编码不能直接作为网络负载。

原2A买卖／维修回执成功码依据本地D2MOO SCmd.cpp与SUnitNpc.cpp：买入result=0、卖出=1、修理=2；不能将一个成功码套给所有请求，库存／金币变化仍消费对应原包。

装书按原0x29发送卷轴GUID／书本GUID。D2MOO PlrMsg::Rcv0x29与ItemMode::sub_6FC49AE0要求卷轴为Cursor／地面，目标为本人stored书本；公共拖放从背包／已授权存储先请求Take，收到真实Cursor后才发送0x29，并重新检查来源、目标revision、Books配对及Misc.maxstack。满书不拿起卷轴；书本拖到另一书本走普通交换，不把原0x29伪装成书页互转。UI的视觉拿起不等于服务端Cursor。

## 已接显示与反馈

其他玩家的公开9D装备投影保留真实鉴定标志、品质、属性及孔内数据，仅包含装备容器；不把已鉴定装备伪装成未鉴定来隐藏数值。首次可见按59指派玩家→20基础属性→9D装备→AA状态发送。20公开velocitypercent／attackrate／level／strength／dexterity；基础速度／攻速百分比按原PlrSave2初始化为100，等级及MPQ职业初值＋已分配属性由权威角色提供，不把装备后的总属性再次叠加到原客户端。角色变更时比较公开字段并同步，离开视野后重新建立基线。

依据为D2MOO SUNITMSG_FirstFn／PlrMsg::sub_6FC82830／PLAYER_SynchronizeItemsToClient，以及原1.13c D2Client RVA 0x86540（9D/action6）、0x82FE0、0xAED80（20）和D2Common ordinal10244（RVA 0x26DB0）。原客户端装备资格需要IDENTIFIED及有效的玩家基础属性；即使法杖本身无力量需求，零力量／敏捷也不能通过原检查。D2X身体外观按ITEMS_CanBeEquipped跳过BROKEN／NOEQUIP，修理／属性回包以新标志重建；此标志处理与保留真实鉴定数据均有原版依据，保留修正。

2026-10-13两方向对照中，D2X Join原版1.13c Host时，原服先指派房主ShotSorceress，再发送五项20，随后发送装备；公开装备保留真实IDENTIFIED标志。原版Join D2X Host时，曾在缺少玩家20的情况下看不到已保留IDENTIFIED的装备，因此仅处理鉴定标志不足以解释该问题。当前源码已补齐基础属性及发送次序，完整原版客户端表现仍待对齐。

衣服原资源说明：当前MPQ Armor.qui的rArm／lArm／Torso／Legs均为0（lit），rSPad／lSPad为1（med），女巫原肩部DCC存在且与lit不同；这件衣服主要改变肩部，不替换成其它胸／腿图形。装备资格读取当前BROKEN／NOEQUIP，不能用修理前损坏解释修理后的显示问题。

原版Join D2X Host的自动穿戴对照中，D2X观察者曾先收到9D/action6、mode1、躯干槽3的装备包，随后收到同GUID的0A/type4；原版拾取者收到装备后没有收到该移除。两端收包不同，不能由此推断原版会忽略相同乱序包。

源码现将普通帧与事件基线的地面同步移到公开玩家装备之前，遵循D2MOO Clients::sub_6FC33670先移除房间单位、再同步单位／库存的顺序。公开装备不再全部0A删除后重发：离开装备栏且未投影到地面的物品按原SCmd的9D/action8卸装，缓存上一公开投影的主物品卸装包；离开玩家视野仍由玩家单位移除清理。客户端0A/type4保留已由9D确认归属其他玩家的mode1装备；远端action8仅清理该玩家的旧装备，不创建其光标物品，也不删除已转为地面的同GUID。自动装备规则未改变，双客户端下完整装备／地面生命周期仍待原版对齐。

普通物品、品质／词缀、数量、耐久、防御、孔数、孔内／套装／符文之语列表、自动词缀及个性化名字按原位流和当前MPQ读取。面板和地面共用item_display；已鉴定暗金／套装才使用真实fileIndex及特殊原图，未鉴定不推测第0行。库存图形、染色与镶孔叠层由下节维护；世界身体组件仍保留真实MPQ外观，不从图标混色推定逐组件透明已经实现。网络品质显式转换；Tempered、耳朵及身体部件的D2S布局现由服务端支持，编码与样本认证边界统一见[SAVES](../../modules/SAVES.md)。

地面flippy按原25Hz播放一次停末帧；action=2／新mode=5触发，静态快照和属性更新不重播，mode=5不可拾取。怪物／物件新生掉落和手动丢弃使用同一事实与原包入口。原item_flippy起始声、dropsound／dropsfxframe落地声由SoundCatalog和SceneAudio消费；缺声组或帧给诊断，不使用替代WAV。

地面图形由`item_appearance.cpp::groundColorTransform`准备底表`Transform`组及暗金／套装`chrtransform`颜色；词缀、第一颗宝石和自动词缀的优先级与下方库存染色共用，不把`InvTrans/invtransform`套给地面。`SceneAssets`使用当前MPQ八张21×256索引变换和当前幕调色板上传每个flippy帧，缓存区分原图及颜色组／颜色行，换幕清理当前地面缓存。同底材不同染色不会共用未染色原图。地面普通绘制使用CelDraw mode 5语义，选中使用当前幕原调色板高亮变换（mode 7），不再用RGB约三倍增亮；无形物品在地面不套库存的TRANS50。

地面名称与提示共用`itemTextFontIndex`及当前MPQ Sky PL2字体变换，不再用手写RGB乘原字体像素。魔法／稀有／套装／暗金／Crafted／Tempered按原品质颜色，符文为橙色、已鉴定符文之语为金色、普通镶孔／无形为灰色；名称色调由共同物品描述的首行提供。当前PL2灰色RGB为(105,105,105)，原地面路径使用的(128,128,128)已撤出。

上述地面染色依据本地D2MOO `ITEMS_GetColor`的`nTransType=0`分支；该参考没有D2Client绘制实现，因而定点核对同一1.13c D2Client RVA `6CF9B–6CFE0`：物品分支以NULL玩家和变换类型0取色，普通／选中分别使用5／7。当前MPQ帽子`cap/uap`的Transform=2、InvTrans=8，`The Gnasher`只有chrtransform=dyel，证明地面与库存参数不能互用。五幕光照、所有品质及选中效果的原版画面对照仍待完成。

拾取反馈核对请求前同GUID、所有权、钱包或合堆／装书两侧数量变化；单独移除、Updated、分包未对齐或未知属性不提前报告。初始快照、死亡、切区、中断不延续旧跟踪。连续通知有界排队；金币多人分配或缺基线不推测成功。

拖放等待期间的原格隐藏与Cursor恢复统一由[包裹手势](INVENTORY_UI.md#操作)维护，不预测目标库存或用Updated代替服务端安置。

## 库存物品图形

PD2沿相同只读投影显示装备、背包、腰带及地面物品。RemoteDisplay的loadRemoteItemDisplayData只准备服务器索引所需的暗金／套装／词缀／劣质名称和特殊图形、符文之语名称，不套原版spawnable／enabled／lvl生成过滤，不准备掉落、配方或随机属性。未鉴定物品继续省略未下发的特殊索引；镶孔子项保留原所有权及指派顺序。CN preview标志和扩展storage不会当作普通库存。完整网络分支、compact独立列与拒绝条件见[PD2位流](../../resources/PD2_CN_DLL_ANALYSIS.md#完整物品位流版本与字段)，面板及受限移物操作见[包裹界面](INVENTORY_UI.md#pd2只读显示)。

PD2提示只描述已解码的名称、数量、耐久及已支持的原表属性描述，不借用原版程序推导最终攻防／装备资格、激活套装层。符文之语附加列表按服务器值保留；镶孔修饰未齐全时明确提示。属性描述函数覆盖、腐化／旧格式混合、全部特殊图及地面落地表现仍待目标会话对照；本项代码接通不代表完整物品兼容。

共同入口为`content/items/item_appearance.cpp`、`client/remote_ui_clients.cpp`、`presentation/scene_assets.cpp`及`presentation/inventory/inventory_view.cpp`。只读投影携带原图、占格、颜色变换和按原指派顺序排列的孔内图形，不增加玩法状态或存档字段。图标按当前幕调色板缓存，使用当前MPQ DC6的原尺寸／偏移，撤出额外4像素边距、自造菱形占位图、格子描边及数量角标；装备栏按占格居中及原部位偏移，资料片副武器组保留原手槽(+4,-4)偏移，Cursor按DC6尺寸居中。金币按原100／500／5000阈值选择帧，阈值是客户端程序常量。

染色读取底表InvTrans、Colors、MagicPrefix／MagicSuffix／AutoMagic.transformcolor、UniqueItems／SetItems.invtransform及Gems.transform，使用当前MPQ`data/global/items/palette/*.dat`的21×256索引变换。Magic／Rare先查后缀、再查前缀；暗金／套装用已知真实行；其余品质只查第一颗孔内物品，第一颗必须是宝石才染底材，符文／珠宝不向后扫描寻找宝石；最后才回退自动词缀。未鉴定原包没有的词缀／特殊索引不推测。原0x00400000无形标志使用当前幕PL2的TRANS50，不再以RGBA alpha近似。

鼠标悬停已鉴定、孔内指派完整且带0x800的物品时，先画宿主，再按原1–6孔半格坐标逐孔绘制`panel/gemsocket.dc6`和实际嵌入物原图；未填孔保留孔底，填入物保留自己的变体与染色。孔底用原CelDraw mode 0／PL2 trans[2]，宿主无形不使嵌入物一起半透明。符文之语不另行隐藏孔内图形；移开鼠标或拖拽物品时不画镶孔叠层。格距读取Inventory.txt，孔位表属于客户端算法常量，不是自制资源。背包／仓库／方块／交易、装备、货架与佣兵共用绘制路径。

占格背景以当前幕PL2 DrawBox mode 0混色：可用蓝色、不可用红色、悬停绿色；装备与腰带平时不额外铺蓝底。需求沿用共同物品描述的只读数值，缺完整指派／revision时暂缓平时底色。原客户端对特殊使用技能、人物变形及双持资格的额外判定尚未全部投影，不声称这些条件下底色完全一致；佣兵只接已核实的悬停底色。PL2背景反查沿用公共实现，重复RGB索引取首项，受既有RGBA背景的量化限制。

依据：本地1.13c D2Common ordinal10893／RVA251C0（品质／词缀／第一颗宝石染色）；D2CMP RVA150D0（8组原变换文件）；D2Client RVA6B5A0（金币帧、原图与无形模式）、95140／D0A98（鉴定门控、孔位与叠画）、959F7／9567E（仅悬停显示镶孔）、953CA／9565D–95672／ABF50（装备及副武器组偏移）、169B8（Cursor）、8C600／8C6B0（原底色）；D2DDraw RVA6A10（mode 0使用trans[2]）。D2MOO `ITEMS_GetColor`辅助核对，实际1.13c导出编号单独确认。当前MPQ孔底和8张变换资源均存在；DLL版本摘要见[资料来源](../../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)，原资源／DLL不纳入提交。

全部孔数、宝石／符文／珠宝变体、魔法／无形／套装特殊图、各容器与五幕光照下的最终画面仍待原版逐项对照。

## 物品属性提示

客户端共同入口为`content/items/item_display.cpp`、`item_descriptions.cpp`和`presentation/inventory/item_tooltip.cpp`，只消费原包物品、人物与已观察装备。字体大小及既有溢出处理保持原实现；文字使用当前MPQ sky/pal.pl2的原字体索引变换，覆盖白／红／绿／蓝／金／灰／橙／黄；符文名称按1.13c使用橙色。Unicode字体共用语言加载入口；现有轮廓字体后端启用时，从同一PL2读取字体RGB并与绘制颜色相乘，分段提示基线与该后端的整行布局一致。增强伤害、防御、格挡和物品IAS的速度说明支持白色标签与蓝色数值分段，黑底对应D2Win的TRANS75（当前RGBA alpha近似，不声称逐像素PL2混色一致）。

基础标签、品质组合名、需求、药剂spelldesc、宝石四种宿主效果、护身符与使用提示读取当前MPQ。药剂Points复用职业恢复纯计算；CharStats的Expansion分隔行不占原职业编号，职业／技能树词缀通过真实sourceRow取文案。属性按ItemStatCost.descpriority及原stat ID排序；完整且等值的dgrp按原组描述合并，全抗／全属性不重复拆列。物理／元素范围、相等元素伤害、毒率四舍五入、增强伤害、修复耐久、技能树、触发、灵气、充能、单技能／本职业oskill上限、复活与怪物类型描述使用对应原程序。未核实的descfunc17／18／25／26不自造文本。

原版没有的腰带容量、手写药剂治疗／持续时间和凯恩石柱顺序文字列表已撤出。孔内物品不逐个列成蓝色属性；符文之语显示原符文缩写，镶孔／无形合为末尾蓝色说明。已确认装备的套装显示实际已穿／缺失部件、当前物品已激活层和确定的全局奖励；装备观察不完整时不推测套装激活，随机且未指派的全局属性不取最小值冒充实际值。

依据：本地1.13c D2Client RVA `54E10`（descfunc／dgrpfunc）、`54320–54666`（伤害合并）、`54744`（钝器固有50%对不死伤害）、`551A5`（耐久恢复）、`55869–55A26`（技能／oskill）、`56330`（宝石／符文宿主）、`8D600`（spelldesc）、`8E180`（无形／镶孔）、`8F9E0`及其`E0EF0／E0F78／E10E0`静态表（武器类别／速度）、`8FB80／8FDC0／8FF20`（防御／格挡／伤害）、`928C0–9327C`（套装）、`94240–943C0`（符文名称及宝石颜色）；D2Common RVA `22210／22260`核对药剂职业倍率，D2MOO `ITEMS_GetWeaponAttackSpeed`与D2WinFont的framed text核对动画公式和背景。数值、动画、文案及字体仍读当前MPQ，原DLL不成为运行依赖。

物品提示尚缺原版截图作完整像素对照。神圣之盾等临时技能对盾面板的额外数值、全部双持／变身动画选择、随机套装奖励和特殊任务／耳朵说明仍待核对。

## 限制

- 原特殊名称、全部说明函数、染色／逐组件透明、孔位叠层及组合提示尚有缺口；库存无形图标的PL2变换仍待原版逐像素对照。
- 普通／赌博买卖、维修和Shift批量请求使用原协议；完整原服报价逐整数一致性及全部组合未认证。
- 充能来源／扣费、六类装备触发事件及任务加工接口已接；专用ItemEffect／目标／客户端ItemCltEffect、前置任务奖励、佣兵消费者及玩家交易仍有限或未接，边界分别维护在[物品技能](../skills/ITEM_SKILLS.md)、[任务物品](QUEST_ITEMS.md)与[经济](ECONOMY.md)。属性列表能解码不等于效果执行。
- 原0x22不提供自选分堆；缺原图明确报告。
- 音频时序、三难度、多人及全部随机分支仍待原版对齐。

原服协议观察见[联网记录](../../modules/NETWORK.md#既有有限证据)，独立编码拒绝范围见[存档](../../modules/SAVES.md)；当前实现与缺口归对应专题，不维护自研运行和包身份台账。
