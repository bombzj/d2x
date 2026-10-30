# 客户端接入

PD2连接身份、规则组合、远端副本与客户端专用接入边界；公共模块的状态所有权不在此重复定义。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="客户端接入连接与账号"></a>

## 客户端接入：连接与账号

来源：[docs/modules/NETWORK.md](../modules/NETWORK.md)。

PD2使用PlayPD2及共用主菜单，启动时绑定本进程资源集合，兼容更新不受整包摘要白名单限制，建立连接前按本地接入或账号认证选择协议；Single Player／TCP/IP复用EmbeddedRealm与原生角色流程，Battle.net仍由CN远端裁决。跨协议只在断开后的Idle边界重建网络状态并递增代次，不依据收到的包猜测切换；资源、原生保存及限制见[PD2连接基线](PROTOCOL_AND_SAVES.md#统一入口与连接生命周期)。当前CN服务已接受版本认证、账号登录、Realm启动和既有Ladder角色选择，可显示角色原图、进入大厅、取列表并返回选角；列表与游戏环境按角色Ladder状态核对。CN建房在原字段后追加GS／lootAllocation两字节；已获MCP建房和加入成功回复，按BH证据改用4001后，已完成游戏握手／初始同步并实际显示罗格营地、本人、NPC和HUD，正常退局返回选角；已核对营地与鲜血荒地往返，其他区域和持续战斗未认证。CN证明采用合法原MPQ的key／owner来源及独立编码，原配置保留旧认证语义。新账号注册尚未重新联调；建角按原MCP 0x02（可选专家级0x04、天梯0x40）、删角按原MCP 0x0A编码开放、成败以该服务回复为准，房间详情仍关闭，PD2库存、城镇交互及基础战斗输入按当前确认的来源／上下文开放；属性加点已按CN控制分界开放，学习和属性分配均等待目标值增加与余点减少。未接入操作继续拒绝，已知分帧但未接入语义的世界消息记录未消费诊断并继续会话。范围见[兼容设计](CLIENT_INTEGRATION.md#当前代码接入范围)，算法、MPQ来源限制及原客户端大厅崩溃证据见[DLL取证](../resources/PD2_CN_DLL_ANALYSIS.md)。

<a id="客户端接入库存协议边界"></a>

## 客户端接入：库存协议边界

来源：[docs/modules/INVENTORY.md](../modules/INVENTORY.md)。

固定PD2 CN在网络物品解码和内容准备处选择版本，复用同一只读投影／界面；本人背包／Cursor／地面、装备及腰带的基础移物沿公共请求，扩展容器按[物品界面](ITEM_UI.md)的独立权限与确认规则开放，未支持操作仍拒绝。能力门控由online_items.hpp统一查询，UI、RemoteInventory与RealmSession各自复验；CN装备／腰带请求按最终模式、部位／格子和交换两侧状态确认，不把相关属性更新当作安置完成。边界见[PD2兼容](CLIENT_INTEGRATION.md#当前代码接入范围)。

共用9D投影已区分普通卸装与`action8/ONCURSOR/IFLAG_QUANTITY`装备销毁，不将销毁回执误造为Cursor；当前实际包依据与运行复测来自零售1.13c破珠，未据此认证CN的全部销毁／扩展容器组合，见[装备](../gameplay/items/EQUIPMENT.md)。

<a id="客户端接入人物加点"></a>

## 客户端接入：人物加点

来源：[docs/modules/CHARACTER.md](../modules/CHARACTER.md)。

CN属性0–3与扩展3A控制从0x10开始的分界已核实，普通属性分配不再整体禁用。公共确认与数值投影见[人物模块](../modules/CHARACTER.md)。

<a id="客户端接入选角预览"></a>

## 客户端接入：选角预览

来源：[docs/modules/CHARACTER.md](../modules/CHARACTER.md)。

当前CN首次进场前的33字节版本4头像可按原图绘制，组件全FF时只表示无装备预览，不补造起始装备。

<a id="客户端接入肖像组件边界"></a>

## 客户端接入：肖像组件边界

来源：[docs/modules/CHARACTER.md](../modules/CHARACTER.md)。

当前CN普通帽子的编号61须解析为cap，不能误作gth并导致完整合成失败。非FF染色字节（14–24）与游戏内单位预览同样按基础组件图合成、不套逐组件调色板，不再整体丢弃肖像；染色外观与全部PD2新增组件编号映射仍未完成，与世界装备组件可绘制性分开。

<a id="客户端接入显示缓存"></a>

## 客户端接入：显示缓存

来源：[docs/modules/CLIENT.md](../modules/CLIENT.md)。

PD2使用独立RemoteDisplay缓存，不准备原版宿主程序、装备派生或面板几何，未登记的飞弹程序与公式显示不可用。

<a id="客户端接入技能来源初始化"></a>

## 客户端接入：技能来源初始化

共用RemoteCombat入口接收普通UI的displayOrigin或正常本地路径坐标，用于本地施法射程和地图绑定；缺少同代次投影时才使用服务器位置样本。该输入提示不改写远端副本或编码来源，最终施法仍由对应profile和服务端裁决。原版第三幕单机已使用该入口；CN的稀疏采样、换区及连续战斗组合尚未复测。

来源：[docs/modules/NETWORK.md](../modules/NETWORK.md)。

已有PD2新角色收包中，右手技能70／owner=FFFFFFFF先于左右保存槽的owner=0到达；客户端不能仅因后到就把不存在的物品来源认作当前技能。此修正共用于原版和PD2，两种模式的新建／重入／充能组合仍待实机回归。

<a id="客户端接入成长请求编码"></a>

## 客户端接入：成长请求编码

来源：[docs/modules/NETWORK.md](../modules/NETWORK.md)。

CN单点学习追加数量BYTE=1，普通属性0–3仍用原3A布局，与低字节0x10起的扩展控制分开。

<a id="客户端接入协议配置身份"></a>

## 客户端接入：协议配置身份

来源：[docs/modules/NETWORK.md](../modules/NETWORK.md)。

CN定义仅为固定2026-10-20 Normal样本的准备，不能凭该枚举认定资源匹配或允许实际入局。

<a id="客户端接入架构与规则组合"></a>

## 客户端接入：架构与规则组合

来源：[docs/architecture/OVERVIEW.md](../architecture/OVERVIEW.md)。

PD2区分当前连接的CN远端与自有宿主协议，启动入口和主菜单统一。`app/frontend`共用RealmFrontend／RealmSession、EmbeddedRealm和Remote*；Single Player／TCP/IP使用本地宿主，Battle.net使用远端权威。PD2协议只在断开后的新连接入口选择，不随未知包或资源目录猜测切换。内容准备分为OriginalRules、RemoteDisplay、Pd2TownHost，后者保留现有内部命名，当前已准备普通五幕、七职业技能程序及PD2扩展预设布局／人口；普通地图开图与词缀消费者已有源码接入，特殊控制器、事件和完整掉落仍未齐，验证边界见[地图词缀基线](MAP_PROPERTIES.md)。PD2客户端元数据与宿主战斗程序分开；本地角色目录与原版隔离，编码沿用原生PD2格式并保留扩展段。差异准备位于`content/pd2`、`hosting/pd2`及`persistence/pd2_save_profile`，地图纯规则位于`gameplay/maps/pd2`，大块技能差异执行位于`server/systems/{skills,combat,missiles,effects}/pd2`，公共领域继续复用现有内核，未知程序明确拒绝。固定构建、认证和边界见[PD2兼容设计](../architecture/PD2_COMPATIBILITY.md#自有技能宿主)，技能／物品／地图／怪物等规则、原生依据及边界统一见[PD2功能差异基线](README.md)。

## 陷阱与尸体组合

PD2专用陷阱的全部效果与实服尸体组合尚未认证。公共尸体目标与物件交互见[联网模块](../modules/NETWORK.md)。

<a id="客户端接入快速施法"></a>

## 客户端接入：快速施法

来源：本机PD2安装的配置与两份BH设置界面源码；核心行为未核对。

PD2客户端提供快速施法（Quick Cast）开关。官方BH的General页含“Quick Cast”勾选与热键（[GameSettings.cpp](../../reference/pd2-bh/BH/Modules/GameSettings/GameSettings.cpp)）；历史BH-CN同位置为“快速施法”（[GameSettings.cpp](../../reference/pd2-cn-bh/BH/Modules/GameSettings/GameSettings.cpp)，界面文案按CN汉化记录，不提升为通用规则）。BH.json存有quick_cast的enabled+hotkey，核心侧ProjectDiablo.json的pd2_game_settings.quick_cast为同名开关；两份BH只提供设置界面，热键同步与实际施法触发由PD2核心消费，本地没有该核心的源码证据，按键后施放哪个技能、目标与鼠标语义、与现有技能选择键的互斥及关闭时是否保持原选择行为均未核实，实现前不按界面字面自造。

D2X当前未实现该功能：技能热键只选择技能并提交SelectMouseSkill，施放仍由鼠标键触发，见[手势与输入](../gameplay/ui/CLASSIC_HUD.md#手势与输入)与[技能菜单](../gameplay/ui/CLASSIC_HUD.md#技能菜单)。实现边界：物理键沿`app/input.cpp`换算FrameInput语义位，开关与热键按[客户端偏好](../modules/SAVES.md#客户端偏好凭据与探索)保存；表现层只读状态并提交既有选择／施法命令，不改服务端权威或本地预结算。剩余任务与完成条件见[实施步骤](GAP_PLAN.md#实施步骤)的T16。

## 当前代码接入范围

选角卡片按服务器返回的HC／死亡标志显示名称颜色与原TBL Hardcore标签，存活HC可继续选择，死亡HC仍拒绝入场；未把这项显示修正当成死亡幽灵造型或全部称号的还原。已用当前CN服务的既有1级HC天梯死灵法师核对GS1普通难度建房、罗格营地原图、城镇走动、正常退局及新进程重新入局；心跳及CN周期报告正常收发，角色退局后保持存活HC。该范围不包含HC实服死亡／尸体许可、共享仓库分组或野外战斗。CN建房新增原资源GS1–GS9选择，每次建房通过类型化参数传入MCP扩展；原版保留原表单与包布局。服务器名称取CN副本的原中文TBL，GS编号不转换为固定IP或端口。

分支为`pd2`，普通启动默认原版。CN远端已迁到共用frontend，不实例化宿主；已核实当前CN服务的登录、既有Ladder角色选择、角色原图、大厅列表与返回选角；PD2登录默认选择服务器返回的第一个Realm，原版选择流程保留。建房编码补齐两字节后，MCP创建／加入均获成功回复，4000连接拒绝后按BH证据修正为4001，已收到首批角色／物品／房间同步。游戏握手、初始同步、本人／NPC／HUD及罗格营地原图已实际显示，正常退局返回选角。已核对基础城镇显示及走动，并用普通角色完成营地与鲜血荒地往返；其他跨区、持续攻击及完整玩法仍待覆盖。新账号注册未重测；建角按原MCP 0x02（可选专家级0x04、天梯0x40）、删除按原MCP 0x0A编码开放，成败由该服务回复裁定，房间详情关闭。创建页天梯复选行仅CN profile显示，用原版字符串10927天梯人物与10928赛季确认框，行位沿既有复选行距，待与原版对照。

| 代码入口 | 已接入范围 | 尚未完成的边界 |
| --- | --- | --- |
| [启动绑定](../../src/app/client_setup.cpp)／[PlayPD2](../../PlayPD2.cmd) | 配置绑定原副本／pd2目录；资源、CN认证与BH报告使用实际文件，不以旧样本摘要拦截兼容更新，不装载native DLL | 未知核心只保留未映射计数，未知认证模块／公式及不支持的格式仍拒绝；实际协议或格式变化仍需适配，见[资源加载](RESOURCE_LOADING.md#更新与兼容性检查) |
| [共用前端](../../src/app/frontend.cpp)／[RealmSession](../../src/network/realm_session.hpp) | 每个连接固定协议，共用Remote*；建角（可选专家级／天梯）、删角按原编码提交，由CN服务裁定；CN连接不使用本地角色或传档；前端的EmbeddedRealm服务本地菜单分支 | 自有pd2-local无窗口配置已有源码入口，端到端边界独立核对；CN无窗口及房间详情仍未开放，函数级任务见[补齐计划](GAP_PLAN.md) |
| [认证](../../src/network/protocol/auth.cpp)／[MPQ资源](../../src/network/protocol/native_auth_resource.cpp) | CN ASCII小写BSHA1、独立Realm摘要、MPQ来源key／owner和0x51证明获当前服务接受；秘密仅内存使用 | 非MPQ来源、未知校验模块、D2X重新注册与native逐字节比对未完成 |
| [MCP](../../src/network/realm_session_mcp.cpp) | 角色／Ladder、带filter列表和两字节列表扩展；建房追加GS／loot分配字节；创建和加入成功 | 已通过建房后入场／城镇显示及退局；公开Join已获票据并建立GS，完整Join场景组合另验 |
| [角色原图](../../src/content/character/realm_portrait.cpp) | 已知版本4／10／13；按原连续前缀规则构造组件编号，支持当前CN无装备及基础装备的职业预览 | 非FF染色、新PD2全部装备组件未完成 |
| [分帧](../../src/network/protocol/d2gs_stream.cpp)／[副本](../../src/client/remote_world.cpp) | 固定目标双向长度；按角色核对Ladder；允许连接与加载路径；未知CN世界消息明确报错 | 已收到初始同步，全部原包处理器、初始状态和Warden仍未认证；扩展物品storage不降级解释 |
| [内容准备](../../src/content/classic_data.cpp) | 独立RemoteDisplay缓存与入场前准备，不准备原版宿主技能程序／掉落／合成；职业背包／装备及仓库／方块几何读取目标Inventory表；准备名称／特殊原图及消耗品元数据 | 已接初始装备、10×8背包、腰带、10×15个人仓库和4×4方块原图；全部图形／分辨率及像素对照、交易与公会界面未完成 |
| [物品](../../src/client/remote_inventory.cpp) | CN完整分支按format选择9／10位stat ID及S12／S13／当前Save列；compact按独立原入口使用当前列；独立0x3E九位增量及严格尾位检查；个人移物／消耗品／存储请求与公会数据隔离 | 初始法杖、药水、卷轴及丢地／拾回已接通；指定消耗品、装书／鉴定／普通堆叠、切组与个人仓库／方块已接受限请求；全部品质／旧格式、复杂双手／增量／存储缓存未认证；公会写入与共享金币转账仍关闭，共享余额仅同步显示；个人金币、方块合成、佣兵及本人交易容器已沿确认上下文开放，实测边界见城镇分项 |
| [基础交互](../../src/network/realm_session_commands.cpp) | NPC与物件点击共用原02／04靠近、13交互；NPC沿27对白／31已阅／30关闭，传送点沿63菜单／49选择或关闭；删除早期按PD2配置整体拒绝NPC和非仓库物件的限制 | CN商店／赌博、版本化候选报价、买卖／维修／鉴定及任务／旅行原请求已接；Charsi菜单、货架、出售／购回及维修回执已实服核对。未知费用不提交；后续幕、佣兵和奖励待用户准备角色实测，双客户端交易未认证 |
| [诊断](../../src/app/debug/online_commands.cpp) | 既有online-*、UI输入、截图、公开状态及包ID／计数；可请求建房／加入 | 不记录SID／MCP证明或票据；D2GS日志对0x68脱敏，不是完整认证抓包 |

当前原客户端大厅崩溃在D2GL同址复现；D2X不加载该模块，已独立显示基础城镇。当前已接装备／腰带、受限消耗品和个人扩展容器请求，最终资格与库存结果由服务端裁决；完整native资格、复杂装备和全部存储格式仍待覆盖。后续先补足前三项的未覆盖组合，再推进移动／战斗、角色业务及保存重连；未核实交互继续明确拒绝。操作方法见[PlayPD2](OPERATIONS.md#运行与部署cn账号与大厅)。

物品显示复用RemoteInventory → RemoteUiClients → 公共InventoryView／地面原图链路。CN职业行提供10×8、29像素格与装备几何，面板原图读取有符号DC6偏移；原版继续使用原布局、九位属性ID与原请求／存档边界。RemoteDisplay准备名称、特殊图形及只读属性元数据，不通过原版的生成资格过滤已由服务器指派的暗金／套装／词缀；不启用PD2宿主程序或本地保存。个人物品操作开放Take／Place／Swap／Drop／Pickup／Equip／Unequip／BeltPlace／BeltSwap／BeltAuto、指定Use／Stack／Book／Identify／Socket／SwitchWeapons及个人存储请求，由原回包更新身份与位置；UI、RemoteInventory及RealmSession复验来源、目标、page与revision，不在本地预提交成功。详细布局及操作边界见[包裹界面](ITEM_UI.md#物品界面布局与cn容器)，位流依据见[DLL专题](../resources/PD2_CN_DLL_ANALYSIS.md#完整物品位流版本与字段)。

当前只投影完整解码、已知基底及合法容器坐标的物品；普通装备、背包、腰带和地面共用原图、染色、镶孔子项身份及落地动画。preview标志不投影成本人库存，0x9C/action19–1A及0x36公会展示独立隔离，不投影为可写个人库存。未解码原因保留在online-snapshot，未知尾部不当作解码成功。只读提示不计算PD2最终攻防或装备资格；孔内物品的已指派列表与目标Gems固定属性合并显示，实际装备集合选择原套装条件层，未知随机值仍明确提示。完整位流、图形、名称和真实会话的组合对照仍待完成，不能由源码接通声明全部物品正确。

CN周期报告与普通移动／6D心跳分开，原版不启用CN专用报告；逐类诊断、布局、原生周期及未闭合拒绝条件见[周期报告证据](../resources/PD2_CN_DLL_ANALYSIS.md#原客户端抓包确认的周期上报与候选修复)。资源整包摘要不再作为启动白名单，核心摘要只选择已核实的计数映射；实际认证挑战、未知格式拒绝与更新缓存规则统一见[资源加载](RESOURCE_LOADING.md#更新与兼容性检查)。旧核心定点结论不自动认证新版全部行为。
