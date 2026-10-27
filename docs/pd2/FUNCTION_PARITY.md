# PD2与D2X逐函数等价性审查

本页维护人工审查口径、F编号索引和旧链接兼容入口；完整审查记录分别归[物品与公共数值](parity/ITEMS.md)、[技能与客户端](parity/SKILLS.md)、[怪物与伙伴](parity/MONSTERS.md)、[地图与事件](parity/MAPS.md)。新增或修正审查时更新负责记录和本页索引，不重排编号；原函数契约归[逐函数说明](functions/README.md)，实施顺序归[补齐计划](GAP_PLAN.md)。默认核心为[BUILDS](BUILDS.md)的17d5分析基线，F57–F61明确使用41d49，旧核心及其他模块按条目单独注明。目录摘录不替代正文的输入、版本及未闭合条件，也不代表逐函数场景验收。

## 判断口径

“公共”指D2X跨原版／PD2或跨多个玩法复用的算法及状态能力，“专用”指PD2规则或特定程序；不等同PE导出／非导出。原生包装器、CRT和内存补丁无需逐字移植，但必须由固定配置、内容准备或领域执行实现对应契约。函数返回值相同不等于随机流、副作用、顺序及失败行为相同。

结论分为：**局部对应**（给定明确输入及依赖时算法对应）、**存在差异**（源码可指出不同分支或数值）、**证据不足**（依赖／调用者未闭合）、**无需移植**（平台安装机制由D2X架构替代）。没有全库完成率；未列出的函数尚未在本页逐项审查。

## F编号索引

原函数契约归[逐函数说明](functions/README.md)，以下只索引D2X差异审查。版本列为条目分析基线，其他DLL及跨版本映射须读正文；不按摘要将局部对应升级为全链等价。

| 编号／主题与入口 | 版本／模块定位 | 领域正文 |
| --- | --- | --- |
| [F01 暴击种类判定3048E0](#f01-暴击种类判定3048e0) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f01-暴击种类判定3048e0) |
| [F02 百分比helper37A6F0](#f02-百分比helper37a6f0) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f02-百分比helper37a6f0) |
| [F03 生命写入门槛2FC030](#f03-生命写入门槛2fc030) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f03-生命写入门槛2fc030) |
| [F04 充能回调3452D0](#f04-充能回调3452d0) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f04-充能回调3452d0) |
| [F05 暴击倍率应用3049C0](#f05-暴击倍率应用3049c0) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f05-暴击倍率应用3049c0) |
| [F06 随机包装器3A0040及D2Game1480](#f06-随机包装器3a0040及d2game1480) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f06-随机包装器3a0040及d2game1480) |
| [F07 武器参数层37F760](#f07-武器参数层37f760) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f07-武器参数层37f760) |
| [F08 动态主人查询2FAA20](#f08-动态主人查询2faa20) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f08-动态主人查询2faa20) |
| [F09 公共nativeAdd对x86 ADD](#f09-公共nativeadd对x86-add) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f09-公共nativeadd对x86-add) |
| [F10 地图说明36C9B0／37AF70与排序36A210](#f10-地图说明36c9b037af70与排序36a210) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f10-地图说明36c9b037af70与排序36a210) |
| [F11 PoisonEggSpawn：3A9710／3B1090](#f11-poisoneggspawn3a97103b1090) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f11-poisoneggspawn3a97103b1090) |
| [F12 掌握3067B0与实际武器入口](#f12-掌握3067b0与实际武器入口) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f12-掌握3067b0与实际武器入口) |
| [F13 皇家火客户端3A8BE0](#f13-皇家火客户端3a8be0) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f13-皇家火客户端3a8be0) |
| [F14 皇家火SrvDo143／3AE960](#f14-皇家火srvdo1433ae960) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f14-皇家火srvdo1433ae960) |
| [F15 皇家火依赖的原CltDo6／B92B0](#f15-皇家火依赖的原cltdo6b92b0) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f15-皇家火依赖的原cltdo6b92b0) |
| [F16 传送门控36B570与阻挡物件37E170](#f16-传送门控36b570与阻挡物件37e170) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f16-传送门控36b570与阻挡物件37e170) |
| [F17 Guardian决策35A470与SrvHit6／3A5360](#f17-guardian决策35a470与srvhit63a5360) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f17-guardian决策35a470与srvhit63a5360) |
| [F18 客户端子飞弹技能身份3A44A0／3A46B0](#f18-客户端子飞弹技能身份3a44a03a46b0) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f18-客户端子飞弹技能身份3a44a03a46b0) |
| [F19 远端玩家基础速率与MonProp公告](#f19-远端玩家基础速率与monprop公告) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f19-远端玩家基础速率与monprop公告) |
| [F20 怪物Charge37的原版委托](#f20-怪物charge37的原版委托) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f20-怪物charge37的原版委托) |
| [F21 FingerMage技能Clt57与飞弹Clt15](#f21-fingermage技能clt57与飞弹clt15) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f21-fingermage技能clt57与飞弹clt15) |
| [F22 充能技能源D2Common_10954与隐藏ItemEffect](#f22-充能技能源d2common_10954与隐藏itemeffect) | 17d5核心基线；原版／旧核例外见正文 | [ITEMS](parity/ITEMS.md#f22-充能技能源d2common_10954与隐藏itemeffect) |
| [F23 普通地图首领预设替换3899D0](#f23-普通地图首领预设替换3899d0) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f23-普通地图首领预设替换3899d0) |
| [F24 Kanemith Brute357730与Radament GreaterMummy35DFE0](#f24-kanemith-brute357730与radament-greatermummy35dfe0) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f24-kanemith-brute357730与radament-greatermummy35dfe0) |
| [F25 Megademon357550与Cantor357640](#f25-megademon357550与cantor357640) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f25-megademon357550与cantor357640) |
| [F26 地图SiegeBeast35E120](#f26-地图siegebeast35e120) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f26-地图siegebeast35e120) |
| [F27 弓箭手首领357330](#f27-弓箭手首领357330) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f27-弓箭手首领357330) |
| [F28 OblivionKnight35AE60](#f28-oblivionknight35ae60) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f28-oblivionknight35ae60) |
| [F29 FlyingScimitar356FC0与原生SQ技能标记](#f29-flyingscimitar356fc0与原生sq技能标记) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f29-flyingscimitar356fc0与原生sq技能标记) |
| [F30 地图全局7／8／9／15的当前消费者](#f30-地图全局78915的当前消费者) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f30-地图全局78915的当前消费者) |
| [F31 宝藏沉沦魔35A180与重复TC调用](#f31-宝藏沉沦魔35a180与重复tc调用) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f31-宝藏沉沦魔35a180与重复tc调用) |
| [F32 SharpTooth35DA60与怪物Do124](#f32-sharptooth35da60与怪物do124) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f32-sharptooth35da60与怪物do124) |
| [F33 Summoner358350的普通前置](#f33-summoner358350的普通前置) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f33-summoner358350的普通前置) |
| [F34 Westmarch BaalMinion357EE0](#f34-westmarch-baalminion357ee0) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f34-westmarch-baalminion357ee0) |
| [F35 St68委托原St32的怪物调用](#f35-st68委托原st32的怪物调用) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f35-st68委托原st32的怪物调用) |
| [F36 怪物St57／Do121 Rabies](#f36-怪物st57do121-rabies) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f36-怪物st57do121-rabies) |
| [F37 BaalMinion357EE0的固定门槛及失败后继](#f37-baalminion357ee0的固定门槛及失败后继) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f37-baalminion357ee0的固定门槛及失败后继) |
| [F38 影子出生技能、PD2战士筛选与跟随](#f38-影子出生技能pd2战士筛选与跟随) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f38-影子出生技能pd2战士筛选与跟随) |
| [F39 原生st计数与当前局镜像](#f39-原生st计数与当前局镜像) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f39-原生st计数与当前局镜像) |
| [F40 场景32的死亡阶段、指定奖励及落点](#f40-场景32的死亡阶段指定奖励及落点) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f40-场景32的死亡阶段指定奖励及落点) |
| [F41 怪物St23／Do170及Do51共享执行](#f41-怪物st23do170及do51共享执行) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f41-怪物st23do170及do51共享执行) |
| [F42 多层地图实例绑定与挡路物件](#f42-多层地图实例绑定与挡路物件) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f42-多层地图实例绑定与挡路物件) |
| [F43 PD2 Willowisp共用包装](#f43-pd2-willowisp共用包装) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f43-pd2-willowisp共用包装) |
| [F44 场景31／35的随机暗金与公共物品后处理](#f44-场景3135的随机暗金与公共物品后处理) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f44-场景3135的随机暗金与公共物品后处理) |
| [F45 Warlord阶段与共用守卫属性](#f45-warlord阶段与共用守卫属性) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f45-warlord阶段与共用守卫属性) |
| [F46 怪物通用St32接触及旋风来源](#f46-怪物通用st32接触及旋风来源) | 17d5核心基线；原版／旧核例外见正文 | [SKILLS](parity/SKILLS.md#f46-怪物通用st32接触及旋风来源) |
| [F47 PD2物件序号及成对预置出生](#f47-pd2物件序号及成对预置出生) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f47-pd2物件序号及成对预置出生) |
| [F48 Ureh场景35源码准入](#f48-ureh场景35源码准入) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f48-ureh场景35源码准入) |
| [F49 Demon Pillar及场景31房间人口](#f49-demon-pillar及场景31房间人口) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f49-demon-pillar及场景31房间人口) |
| [F50 有序奖励池、强制词缀及通用Do47](#f50-有序奖励池强制词缀及通用do47) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f50-有序奖励池强制词缀及通用do47) |
| [F51 Rathma尖塔的相邻实体资格](#f51-rathma尖塔的相邻实体资格) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f51-rathma尖塔的相邻实体资格) |
| [F52 原生物件门户、Zhar机关及直接音效](#f52-原生物件门户zhar机关及直接音效) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f52-原生物件门户zhar机关及直接音效) |
| [F53 Imperial Palace的两类死亡奖励](#f53-imperial-palace的两类死亡奖励) | 17d5核心基线；原版／旧核例外见正文 | [MAPS](parity/MAPS.md#f53-imperial-palace的两类死亡奖励) |
| [F54 非战斗NPC的场景编排](#f54-非战斗npc的场景编排) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f54-非战斗npc的场景编排) |
| [F55 野生怪物的原生Do49召唤](#f55-野生怪物的原生do49召唤) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f55-野生怪物的原生do49召唤) |
| [F56 大祭司家族的PD2生命阈值包装](#f56-大祭司家族的pd2生命阈值包装) | 17d5核心基线；原版／旧核例外见正文 | [MONSTERS](parity/MONSTERS.md#f56-大祭司家族的pd2生命阈值包装) |
| [F57 Outer Void的出生增强及三阶段门户](#f57-outer-void的出生增强及三阶段门户) | 41d49核心；配套模块见正文 | [MAPS](parity/MAPS.md#f57-outer-void的出生增强及三阶段门户) |
| [F58 Djinn的双端标记及分帧TC](#f58-djinn的双端标记及分帧tc) | 41d49核心；配套模块见正文 | [MAPS](parity/MAPS.md#f58-djinn的双端标记及分帧tc) |
| [F59 地图首领的公共死亡奖励后继](#f59-地图首领的公共死亡奖励后继) | 41d49核心；配套模块见正文 | [MAPS](parity/MAPS.md#f59-地图首领的公共死亡奖励后继) |
| [F60 随机事件的选择、房间出生和祭坛入口](#f60-随机事件的选择房间出生和祭坛入口) | 41d49核心；配套模块见正文 | [MAPS](parity/MAPS.md#f60-随机事件的选择房间出生和祭坛入口) |
| [F61 专属41–45控制器的出生与事件依赖](#f61-专属4145控制器的出生与事件依赖) | 41d49核心；配套模块见正文 | [MAPS](parity/MAPS.md#f61-专属4145控制器的出生与事件依赖) |
| [F62 属性浮点累计与查询边界](#f62-属性浮点累计与查询边界) | 17d5／41d49及原版公共链 | [ITEMS](parity/ITEMS.md#f62-属性浮点累计与查询边界) |
| [F63 41d49核心计数映射](#f63-41d49核心计数映射) | 41d49核心／37E630 | [ITEMS](parity/ITEMS.md#f63-41d49核心计数映射) |
| [F64 狂乱层数与完整等级比例](#f64-狂乱层数与完整等级比例) | 17d5 306960／41d49 306AF0 | [SKILLS](parity/SKILLS.md#f64-狂乱层数与完整等级比例) |
| [F65 诅咒缩放的拒绝条件与特殊状态](#f65-诅咒缩放的拒绝条件与特殊状态) | 17d5 36C020／41d49 36D940及实际安装点 | [SKILLS](parity/SKILLS.md#f65-诅咒缩放的拒绝条件与特殊状态) |

## F01 暴击种类判定3048E0

[ITEMS审查正文](parity/ITEMS.md#f01-暴击种类判定3048e0)。条件、种类、回绕加法及同种子百分比随机为局部对应；F02已核定消费者的百分比差异及当前技能选手已修正并构建，不能据此认证整条暴击链。摘要仅用于定位，版本、条件和完整结论见正文。

## F02 百分比helper37A6F0

[ITEMS审查正文](parity/ITEMS.md#f02-百分比helper37a6f0)。独立PD2函数、已核定飞弹／近战及Teleport消费者已修正，已构建。摘要仅用于定位，版本、条件和完整结论见正文。

## F03 生命写入门槛2FC030

[ITEMS审查正文](parity/ITEMS.md#f03-生命写入门槛2fc030)。玩家飞弹吸血在准备生命写入时通过effects.unitStatValues重新读取实时lifedrain_percentcap，不再直接使用totals.character.combat中的旧cap；怪物仍从当时的effects.combatModifiers取值。摘要仅用于定位，版本、条件和完整结论见正文。

## F04 充能回调3452D0

[ITEMS审查正文](parity/ITEMS.md#f04-充能回调3452d0)。正速率、原生保存列表和既有事件路径已补上述差异并统一构建；Demonlimb单层的施放／恢复、交易同位置回滚、对手镜像、交换及保存重入已有实测；新GUID路径另复验19→20镜像、取消回19及F8重绑、新时钟回20、成交20与双方新进程恢复，多列表、地面背压和全部跨区交错仍需覆盖。摘要仅用于定位，版本、条件和完整结论见正文。

## F05 暴击倍率应用3049C0

[ITEMS审查正文](parity/ITEMS.md#f05-暴击倍率应用3049c0)。非完整等价，P0先修经核定的数值依赖，P1再闭合武器和属性投影。摘要仅用于定位，版本、条件和完整结论见正文。

## F06 随机包装器3A0040及D2Game1480

[ITEMS审查正文](parity/ITEMS.md#f06-随机包装器3a0040及d2game1480)。暴击所需公共随机算法局部对应，不需要替换成PD2事件随机；原生单位+20的种子与D2X combatRandom在全部出生／继承／提交顺序是否一致仍是调用链门槛。摘要仅用于定位，版本、条件和完整结论见正文。

## F07 武器参数层37F760

[ITEMS审查正文](parity/ITEMS.md#f07-武器参数层37f760)。筛选条件局部对应，输入语义证据不足，P1。摘要仅用于定位，版本、条件和完整结论见正文。

## F08 动态主人查询2FAA20

[ITEMS审查正文](parity/ITEMS.md#f08-动态主人查询2faa20)。正常数值部分局部对应，实际查询时点和装备资格证据不足，P1。摘要仅用于定位，版本、条件和完整结论见正文。

## F09 公共nativeAdd对x86 ADD

[ITEMS审查正文](parity/ITEMS.md#f09-公共nativeadd对x86-add)。所审ADD用途可复用，不需要PD2版重复实现；只返回结果，不提供CPU CF／OF。摘要仅用于定位，版本、条件和完整结论见正文。

## F10 地图说明36C9B0／37AF70与排序36A210

[MAPS审查正文](parity/MAPS.md#f10-地图说明36c9b037af70与排序36a210)。代码已构建；最终需覆盖重复组、跨层同值、15／16条、零值、同优先级、负值、函数5／19／20／21、UTF16尾部截断及多行颜色。摘要仅用于定位，版本、条件和完整结论见正文。

## F11 PoisonEggSpawn：3A9710／3B1090

[SKILLS审查正文](parity/SKILLS.md#f11-poisoneggspawn3a97103b1090)。完成当前回调及调用者的静态接线，已构建，尚未逐场景运行对照；完整首领场景、原随机交错、非标准序列速度与同帧生命周期仍未闭合。摘要仅用于定位，版本、条件和完整结论见正文。

## F12 掌握3067B0与实际武器入口

[ITEMS审查正文](parity/ITEMS.md#f12-掌握3067b0与实际武器入口)。飞弹接触已按当前技能WeapSel／旋风切手标志／动作序列时钟选择，并与37F760的库存登记GUID分开；新改动已统一构建。摘要仅用于定位，版本、条件和完整结论见正文。

## F13 皇家火客户端3A8BE0

[SKILLS审查正文](parity/SKILLS.md#f13-皇家火客户端3a8be0)。只接已核定客户端入口，已构建，尚未逐场景运行对照；未知公式与非正步长拒绝，原生异常无限循环不移植。摘要仅用于定位，版本、条件和完整结论见正文。

## 待审依赖

掌握、属性聚合及全部原生调用者仍需分别审查；技能／地图／AI等分册已有规则不自动计入本页已审范围。全库范围见[分析入口](README.md#全库分析的未完成范围)。

## F14 皇家火SrvDo143／3AE960

[SKILLS审查正文](parity/SKILLS.md#f14-皇家火srvdo1433ae960)。已补直接技能入口，已构建；当前表568／574分别5／4枚，依赖981→982及997→998→999须继续审完整推进、碰撞、NextDelay及客户端一致性。摘要仅用于定位，版本、条件和完整结论见正文。

## F15 皇家火依赖的原CltDo6／B92B0

[SKILLS审查正文](parity/SKILLS.md#f15-皇家火依赖的原cltdo6b92b0)。CelFile=null已有无图像程序消费者，不列为新增修复。摘要仅用于定位，版本、条件和完整结论见正文。

## F16 传送门控36B570与阻挡物件37E170

[MAPS审查正文](parity/MAPS.md#f16-传送门控36b570与阻挡物件37e170)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F17 Guardian决策35A470与SrvHit6／3A5360

[MONSTERS审查正文](parity/MONSTERS.md#f17-guardian决策35a470与srvhit63a5360)。普通NU／S1、无经验／无掉落及完整目标位有消费者；特殊预设、附属物件、其他标志／模式及部分目标位仍明确拒绝。摘要仅用于定位，版本、条件和完整结论见正文。

## F18 客户端子飞弹技能身份3A44A0／3A46B0

[SKILLS审查正文](parity/SKILLS.md#f18-客户端子飞弹技能身份3a44a03a46b0)。已构建；其他回调的全部技能覆盖语义、非常数CHitCalc1及完整出生／接触／结束时序仍需核对。摘要仅用于定位，版本、条件和完整结论见正文。

## F19 远端玩家基础速率与MonProp公告

[SKILLS审查正文](parity/SKILLS.md#f19-远端玩家基础速率与monprop公告)。不新增协议字段或额外0x20；不发送怪物隐藏库存或把主人属性填入伙伴。摘要仅用于定位，版本、条件和完整结论见正文。

## F20 怪物Charge37的原版委托

[MONSTERS审查正文](parity/MONSTERS.md#f20-怪物charge37的原版委托)。普通地图怪物及复活家族共用原输入；不接特殊Lucion114。摘要仅用于定位，版本、条件和完整结论见正文。

## F21 FingerMage技能Clt57与飞弹Clt15

[SKILLS审查正文](parity/SKILLS.md#f21-fingermage技能clt57与飞弹clt15)。新源码已构建，实际父子弹表现尚待验证；视觉SEED初态与原客户端逐随机认证独立保留，不以线性移动代替原回调。摘要仅用于定位，版本、条件和完整结论见正文。

## F22 充能技能源D2Common_10954与隐藏ItemEffect

[ITEMS审查正文](parity/ITEMS.md#f22-充能技能源d2common_10954与隐藏itemeffect)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F23 普通地图首领预设替换3899D0

[MAPS审查正文](parity/MAPS.md#f23-普通地图首领预设替换3899d0)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F24 Kanemith Brute357730与Radament GreaterMummy35DFE0

[MONSTERS审查正文](parity/MONSTERS.md#f24-kanemith-brute357730与radament-greatermummy35dfe0)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F25 Megademon357550与Cantor357640

[MONSTERS审查正文](parity/MONSTERS.md#f25-megademon357550与cantor357640)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F26 地图SiegeBeast35E120

[MONSTERS审查正文](parity/MONSTERS.md#f26-地图siegebeast35e120)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F27 弓箭手首领357330

[MONSTERS审查正文](parity/MONSTERS.md#f27-弓箭手首领357330)。固定H抗性按原MOVZX读取，不按反编译short误作有符号；状态8不自造持续时间。摘要仅用于定位，版本、条件和完整结论见正文。

## F28 OblivionKnight35AE60

[MONSTERS审查正文](parity/MONSTERS.md#f28-oblivionknight35ae60)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F29 FlyingScimitar356FC0与原生SQ技能标记

[MONSTERS审查正文](parity/MONSTERS.md#f29-flyingscimitar356fc0与原生sq技能标记)。本批当前Do-only序列实际只有标记1或2，没有借该修改新增标记4的重复执行程序。摘要仅用于定位，版本、条件和完整结论见正文。

## F30 地图全局7／8／9／15的当前消费者

[MAPS审查正文](parity/MAPS.md#f30-地图全局78915的当前消费者)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F31 宝藏沉沦魔35A180与重复TC调用

[MONSTERS审查正文](parity/MONSTERS.md#f31-宝藏沉沦魔35a180与重复tc调用)。完整场景仍待原版对照；异步等待后的目标重采样、受击／死亡交错、地图额外TC和完整逃跑轨迹仍需核对。摘要仅用于定位，版本、条件和完整结论见正文。

## F32 SharpTooth35DA60与怪物Do124

[MONSTERS审查正文](parity/MONSTERS.md#f32-sharptooth35da60与怪物do124)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F33 Summoner358350的普通前置

[MONSTERS审查正文](parity/MONSTERS.md#f33-summoner358350的普通前置)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F34 Westmarch BaalMinion357EE0

[MONSTERS审查正文](parity/MONSTERS.md#f34-westmarch-baalminion357ee0)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F35 St68委托原St32的怪物调用

[SKILLS审查正文](parity/SKILLS.md#f35-st68委托原st32的怪物调用)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F36 怪物St57／Do121 Rabies

[SKILLS审查正文](parity/SKILLS.md#f36-怪物st57do121-rabies)。完整场景仍待原版对照，完整命中／格挡、感染传播、入场模式／打断及原生随机轨迹仍需验证。摘要仅用于定位，版本、条件和完整结论见正文。

## F37 BaalMinion357EE0的固定门槛及失败后继

[MONSTERS审查正文](parity/MONSTERS.md#f37-baalminion357ee0的固定门槛及失败后继)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F38 影子出生技能、PD2战士筛选与跟随

[MONSTERS审查正文](parity/MONSTERS.md#f38-影子出生技能pd2战士筛选与跟随)。完整场景仍待原版对照；原PetMove房间探针背压、AiControl移动参数与实际路径的完整生命周期、宠物池最近一次原生SetMax与装备重算之间的历史差别、全部交叉职业／装备及失败时钟仍待闭合。摘要仅用于定位，版本、条件和完整结论见正文。

## F39 原生st计数与当前局镜像

[MAPS审查正文](parity/MAPS.md#f39-原生st计数与当前局镜像)。完整场景仍待原版对照；其他计数项的业务触发和统计面板／周期上报不因字段保留而完成。摘要仅用于定位，版本、条件和完整结论见正文。

## F40 场景32的死亡阶段、指定奖励及落点

[MAPS审查正文](parity/MAPS.md#f40-场景32的死亡阶段指定奖励及落点)。St23／Do170与Do51依赖沿F41补入后，183已在源码准入程序32；完整场景仍待原版对照。摘要仅用于定位，版本、条件和完整结论见正文。

## F41 怪物St23／Do170及Do51共享执行

[SKILLS审查正文](parity/SKILLS.md#f41-怪物st23do170及do51共享执行)。完整场景仍待原版对照；目标在起手后失效、满层清理的全部失败顺序、客户端渐进事实、极端等级／速度、复活及随机交错仍待验证。摘要仅用于定位，版本、条件和完整结论见正文。

## F42 多层地图实例绑定与挡路物件

[MAPS审查正文](parity/MAPS.md#f42-多层地图实例绑定与挡路物件)。完整场景仍待原版对照。摘要仅用于定位，版本、条件和完整结论见正文。

## F43 PD2 Willowisp共用包装

[MONSTERS审查正文](parity/MONSTERS.md#f43-pd2-willowisp共用包装)。完整场景仍待原版对照；原生辅助调用已按当前表重新解析为Common 10331／10210及Game EF650，不能使用旧核心的thunk地址对应。摘要仅用于定位，版本、条件和完整结论见正文。

## F44 场景31／35的随机暗金与公共物品后处理

[MAPS审查正文](parity/MAPS.md#f44-场景3135的随机暗金与公共物品后处理)。局部对应与未闭合依赖须结合完整调用链判断。摘要仅用于定位，版本、条件和完整结论见正文。

## F45 Warlord阶段与共用守卫属性

[MONSTERS审查正文](parity/MONSTERS.md#f45-warlord阶段与共用守卫属性)。完整场景仍待原版对照，31经F49已有自研主链运行观察，完整SC／SQ动作及当前各技能释放组合仍待复验。摘要仅用于定位，版本、条件和完整结论见正文。

## F46 怪物通用St32接触及旋风来源

[SKILLS审查正文](parity/SKILLS.md#f46-怪物通用st32接触及旋风来源)。完整场景仍待原版对照，SQ速度、Do158二次接触、旋风中途停止、延迟死亡及附魔／复活组合仍待统一复验；31另经F49已有自研主链运行观察，该范围不证明全部怪物技能都可执行。摘要仅用于定位，版本、条件和完整结论见正文。

## F47 PD2物件序号及成对预置出生

[MAPS审查正文](parity/MAPS.md#f47-pd2物件序号及成对预置出生)。完整场景仍待原版对照；目前显式配置消费者是冻结Pd2Local资源挂载，其他资源挂载未借用该配置。摘要仅用于定位，版本、条件和完整结论见正文。

## F48 Ureh场景35源码准入

[MAPS审查正文](parity/MAPS.md#f48-ureh场景35源码准入)。已有自研前置／终局及奖励主链运行观察；完整原版整场、多人跨层／回城／重入、地面落点、计数保存和失败顺序仍需复验。摘要仅用于定位，版本、条件和完整结论见正文。

## F49 Demon Pillar及场景31房间人口

[MAPS审查正文](parity/MAPS.md#f49-demon-pillar及场景31房间人口)。完整场景仍待原版对照。摘要仅用于定位，版本、条件和完整结论见正文。

## F50 有序奖励池、强制词缀及通用Do47

[MAPS审查正文](parity/MAPS.md#f50-有序奖励池强制词缀及通用do47)。完整场景仍待原版对照；这里只接已核定的单个首选前／后缀，不宣称完整三个强制稀有前／后缀数组。摘要仅用于定位，版本、条件和完整结论见正文。

## F51 Rathma尖塔的相邻实体资格

[MONSTERS审查正文](parity/MONSTERS.md#f51-rathma尖塔的相邻实体资格)。完整场景仍待原版对照。摘要仅用于定位，版本、条件和完整结论见正文。

## F52 原生物件门户、Zhar机关及直接音效

[MAPS审查正文](parity/MAPS.md#f52-原生物件门户zhar机关及直接音效)。ZharLibrary的负DS1序号已按F47的真实连续表索引接到601–604消费者，原版及PD2客户端／服务端共用同一翻译，未新增坐标或预置单位。摘要仅用于定位，版本、条件和完整结论见正文。

## F53 Imperial Palace的两类死亡奖励

[MAPS审查正文](parity/MAPS.md#f53-imperial-palace的两类死亡奖励)。完整场景仍待原版对照；1168的NPC包装已沿F54接入，出生与野生Do49沿F55接入；192／194已源码准入，当前仍未完成整场景复验。摘要仅用于定位，版本、条件和完整结论见正文。

## F54 非战斗NPC的场景编排

[MONSTERS审查正文](parity/MONSTERS.md#f54-非战斗npc的场景编排)。完整场景仍待原版对照。摘要仅用于定位，版本、条件和完整结论见正文。

## F55 野生怪物的原生Do49召唤

[MONSTERS审查正文](parity/MONSTERS.md#f55-野生怪物的原生do49召唤)。完整场景仍待原版对照；192／194入口已沿F53收尾源码开放。摘要仅用于定位，版本、条件和完整结论见正文。

## F56 大祭司家族的PD2生命阈值包装

[MONSTERS审查正文](parity/MONSTERS.md#f56-大祭司家族的pd2生命阈值包装)。完整场景仍待原版对照；Fn052的原有普通分支保持原执行器，完整召唤压力、状态恢复、移动失败与随机交错待统一复验。摘要仅用于定位，版本、条件和完整结论见正文。

## F57 Outer Void的出生增强及三阶段门户

[MAPS审查正文](parity/MAPS.md#f57-outer-void的出生增强及三阶段门户)。180已源码准入，完整场景、首领池全部技能、多人死亡交错、压缩恢复及原生随机轨迹仍留待全部源码完成后的统一验证；不以此条关闭其他控制器。摘要仅用于定位，版本、条件和完整结论见正文。

## F58 Djinn的双端标记及分帧TC

[MAPS审查正文](parity/MAPS.md#f58-djinn的双端标记及分帧tc)。197／198／199已准入，多Warp按原房间反链选择返回位置；1195／1196各自沿本体TC及额外60件的分帧任务执行，属性链接失败不再拒绝合法奖励。摘要仅用于定位，版本、条件和完整结论见正文。

## F59 地图首领的公共死亡奖励后继

[MAPS审查正文](parity/MAPS.md#f59-地图首领的公共死亡奖励后继)。多人、失败与原版随机时序的完整对照仍未闭合；此条不覆盖其余特殊场景的专用死亡回调。摘要仅用于定位，版本、条件和完整结论见正文。

## F60 随机事件的选择、房间出生和祭坛入口

[MAPS审查正文](parity/MAPS.md#f60-随机事件的选择房间出生和祭坛入口)。开图现沿同一CRT调用选择器，强制事件500交原候选分派；异步内容／容量等待保留原MapInstance与已选控制器，不重抽。摘要仅用于定位，版本、条件和完整结论见正文。

## F61 专属41–45控制器的出生与事件依赖

[MAPS审查正文](parity/MAPS.md#f61-专属4145控制器的出生与事件依赖)。41–45的出生、阶段、奖励及返回链由上述消费者执行；MonPreset使用当前编译记录身份，Lucion及子体保留原生模式唤醒、城镇622异步寻位与宝库623转换。摘要仅用于定位，版本、条件和完整结论见正文。

## F62 属性浮点累计与查询边界

[ITEMS审查正文](parity/ITEMS.md#f62-属性浮点累计与查询边界)。公共op3／5／7保留整和转换，缓存子列表零行保留；通知helper的op1不推广到权威计算。

## F63 41d49核心计数映射

[ITEMS审查正文](parity/ITEMS.md#f63-41d49核心计数映射)。逐项核定新版选择表、地图代码及85／86接收，Normal20261026复用已核对的current映射；其他未知摘要仍保留未映射数据。

## F64 狂乱层数与完整等级比例

[SKILLS审查正文](parity/SKILLS.md#f64-狂乱层数与完整等级比例)。PD2 Do009以Calc4限制层数，完整等级属性按层数／上限缩放；原版与怪物Do109继续沿各自原链。

## F65 诅咒缩放的拒绝条件与特殊状态

完整审查见[技能与客户端](parity/SKILLS.md#f65-诅咒缩放的拒绝条件与特殊状态)。
