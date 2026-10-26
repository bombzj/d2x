# 客户端接入

PD2连接身份、规则组合、远端副本与客户端专用接入边界；公共模块的状态所有权不在此重复定义。 目标身份与证据口径见[版本与证据](BUILDS.md)。

<a id="客户端接入连接与账号"></a>

## 客户端接入：连接与账号

来源：[docs/modules/NETWORK.md](../modules/NETWORK.md)。

PD2使用PlayPD2及共用主菜单，启动时绑定本进程资源集合，兼容更新不受整包摘要白名单限制，建立连接前按本地接入或账号认证选择协议；Single Player／TCP/IP复用EmbeddedRealm与原生角色流程，Battle.net仍由CN远端裁决。跨协议只在断开后的Idle边界重建网络状态并递增代次，不依据收到的包猜测切换；资源、原生保存及限制见[PD2连接基线](PROTOCOL_AND_SAVES.md#统一入口与连接生命周期)。当前CN服务已接受版本认证、账号登录、Realm启动和既有Ladder角色选择，可显示角色原图、进入大厅、取列表并返回选角；列表与游戏环境按角色Ladder状态核对。CN建房在原字段后追加GS／lootAllocation两字节；已获MCP建房和加入成功回复，按BH证据改用4001后，已完成游戏握手／初始同步并实际显示罗格营地、本人、NPC和HUD，正常退局返回选角；已核对营地与鲜血荒地往返，其他区域和持续战斗未认证。CN证明采用合法原MPQ的key／owner来源及独立编码，原配置保留旧认证语义。新账号注册尚未重新联调；建角按原MCP 0x02（可选专家级0x04、天梯0x40）、删角按原MCP 0x0A编码开放、成败以该服务回复为准，房间详情仍关闭，PD2库存、城镇交互及基础战斗输入按当前确认的来源／上下文开放；属性加点已按CN控制分界开放，学习和属性分配均等待目标值增加与余点减少。未接入操作继续拒绝，已知分帧但未接入语义的世界消息记录未消费诊断并继续会话。范围见[兼容设计](../architecture/PD2_COMPATIBILITY.md#当前代码接入范围)，算法、MPQ来源限制及原客户端大厅崩溃证据见[DLL取证](../resources/PD2_CN_DLL_ANALYSIS.md)。

<a id="客户端接入库存协议边界"></a>

## 客户端接入：库存协议边界

来源：[docs/modules/INVENTORY.md](../modules/INVENTORY.md)。

固定PD2 CN在网络物品解码和内容准备处选择版本，复用同一只读投影／界面；本人背包／Cursor／地面、装备及腰带的基础移物沿公共请求，扩展容器按[物品界面](ITEM_UI.md)的独立权限与确认规则开放，未支持操作仍拒绝。能力门控由online_items.hpp统一查询，UI、RemoteInventory与RealmSession各自复验；CN装备／腰带请求按最终模式、部位／格子和交换两侧状态确认，不把相关属性更新当作安置完成。边界见[PD2兼容](../architecture/PD2_COMPATIBILITY.md#当前代码接入范围)。

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

当前CN普通帽子的编号61须解析为cap，不能误作gth并导致完整合成失败。非FF染色字节及全部PD2新增组件映射尚未支持，与世界装备组件可绘制性分开。

<a id="客户端接入显示缓存"></a>

## 客户端接入：显示缓存

来源：[docs/modules/CLIENT.md](../modules/CLIENT.md)。

PD2使用独立RemoteDisplay缓存，不准备原版宿主程序、装备派生或面板几何，未登记的飞弹程序与公式显示不可用。

<a id="客户端接入技能来源初始化"></a>

## 客户端接入：技能来源初始化

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
