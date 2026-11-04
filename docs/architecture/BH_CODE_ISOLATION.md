# BH客户端增强与内部能力接入

## 身份与范围

本页维护BH源码结论、D2X现状与接入边界；实现状态以源码为准。地图揭示放在D2X内部地图模块，PD2 CN入口负责开关与调用，不建立独立BH库或第二套地图算法。当前范围为本人所在的连续步行区域，不展开整个Act／Game；投影、模式隔离和开图验证分别见下文。

| 来源 | 固定身份 | 采用范围 |
| --- | --- | --- |
| 官方Project-Diablo-2/BH | `92e9d319f20113b5fbd382ad0fd3df9480116d04`，2026-10-19；`reference/pd2-bh` | 当前公开模块、过滤／通知／配置算法；MapNotify::OnLoop为空，没有地图全揭示调用链 |
| 历史weili0677/BH-CN | `68f7343342eed668190d464b7111849a6cc0ce89`，2022-12-27；`reference/pd2-cn-bh` | RevealGame／Act／Level／Room的直接源码依据；S6历史分支，不认证当前CN或官方BH.dll行为 |
| 本机BH.dll | 摘要及样本归[DLL取证](../resources/PD2_CN_DLL_ANALYSIS.md) | 尚未证明与上述任一提交一致，不能仅凭同名函数套用源码结论 |

公开源更新核对（2026-11-04）：[weili0677/BH-CN](https://github.com/weili0677/BH-CN)仅有main分支，仍指向上述68f7343，提交时间为2022-12-27，消息为“S6最新”；最新已发布标签[v1.1_20221007](https://github.com/weili0677/BH-CN/releases/tag/v1.1)于2022-10-08发布，比main旧。六个直接fork中，[JerryHol/BH-CN](https://github.com/JerryHol/BH-CN)的main为2023-11-07的83e89c6e3c78d66b79dd731bd2b1dd2bcd50942c；[与上游比较](https://github.com/weili0677/BH-CN/compare/main...JerryHol:main)仅领先一个提交、修改Constants.h一行，将SETTINGS_TEXT从“ZYLPD2设置”改为“Settings”，无新增功能。其余直接fork的分支头相同或更旧。GitHub仓库检索及定向网页搜索未找到更新的功能性CN源码；这是已搜索公开范围的结论，不证明不存在私有或未索引的后续版本，也不认证当前CN DLL。保留本地68f7343作为功能分析基线，不因仓库updated_at较新而改标代码版本。

两个源码仓库的LICENSE.md均为AGPL-3.0文本，另有Apache／ThirdParty文件；实际采用代码时保留对应文件声明、固定提交及修改说明，不擅改项目许可证。来源统一见[资料来源](../resources/THIRD_PARTY.md)。不把reference、原DLL、注入补丁或导出资源编入产品。

## 当前CN二进制功能盘点

盘点对象为2026-11-04安装BH.dll，SHA-256 `f7f8c71047e02266ffeabbe0458979356beefae6c4346e65245a330d8c676a5a`，2,801,152字节；比较基准是官方公开main `66acd03d6d073ec378fed014c4d8269b3ddcc603`。本地官方源码固定在92e9d319，两者净变化已核只有Constants.h、D2Helpers.cpp及Item.cpp三处，不涉及本节候选功能。完整身份见[版本基线](../pd2/BUILDS.md#2026-11-04安装身份)，自动拾取执行证据见[物品界面](../pd2/ITEM_UI.md#cn自动拾取的模块归属)。

计数单位为用户可区分的功能组，不把同一功能的颜色、热键、数值阈值、物品清单和不同目的地分别累加。配置／UI入口、当前DLL执行代码、外部模块协作及实机可用性是不同证据层级。官方注释掉的旧设置不算当前有效功能，但由ProjectDiablo核心或D2GL提供的能力也不能仅因官方BH没有实现就算整个官方客户端缺失。历史BH-CN源码只辅助定位，不替代当前DLL证据；RTTI类型或错误字符串存在不证明模块已注册或对应路径可达。

以下盘点只涉及静态功能差异，不衡量代码改动比例，不确认当前CN基于哪个官方提交，也不声称其包含官方全部最新修复。

### 分组计数与执行证据

本轮按下表口径确认12组扩展有当前DLL的处理代码，另4组只闭合到触发或补丁分派；所有项目均未做本轮实机验收。这是静态盘点下限，不是全库新增功能总数，也不是相对整个官方PD2客户端的缺失数量。下表地址均为该BH模块RVA，不能套用到mdll1或其他BH版本。

| 编号 | 扩展功能组 | 当前DLL证据与限定 |
| --- | --- | --- |
| A01 | 自动拾取地面材料 | `1AC2F0`筛选、`1ACECE`构造0x16包、`1ACF1C`发送；战网／单机范围见[专条](../pd2/ITEM_UI.md#cn自动拾取的模块归属) |
| A02 | 自动地图揭示 | `1AC140`检查`296A64`并按模式调用`1B0CC0/1B0D30/1B0ED0`；后者遍历房间并记完成位，跳过161–163层，不能称所有扩展地图完整揭示 |
| A03 | 非物品地图叠层 | 怪物`1ADE8C`、飞弹`1AF4CC`、未开启物件／宝箱`1AF731`分别读开关并处理实体与坐标；合并为一组，不按怪物颜色、敌我关系或每种箱子累加 |
| A04 | 导航目的地／引导线 | AutoTele构造`95C20`由`9EBA7`调用，虚表`258DEC`；绘制路径`12C448`读总开关`29680C`，循环5类目的地并投影坐标。这里只计导航显示，不据模块名计自动传送 |
| A05 | 按药剂类型快捷用药 | `1A5A3C`比较红／蓝／紫快捷键后遍历库存，`1A5C39`与`1A5CAC`分别构造0x26／0x20用药包；解毒药另分派到`1A2230`。合为一组，最终用药结果由原引擎／服务器决定 |
| A06 | 快捷使用回城书 | `1A5D82`按键分派，扫描代码rtp，`1A5ED6`构造0x20并在`1A5F34`发送；只确认开门请求，不计自动走入门户或返门 |
| A07 | 批量快捷鉴定 | `1A2B53/1A2BC4`读`296BC4`，循环物品列表，`1A2B74`构造0x27请求；开关关闭时退出循环，开启时继续。资源／权限和回包仍由原流程处理 |
| A08 | 佣兵保护与低血提示 | `1433CB`读`2968A4`，生命比例下降及`2968B0*1000`间隔检查后调用`143010`选药；`143294`构造0x26且佣兵标志1、`1432DD`发送。佣兵吐槽与红／紫药选择不拆成独立新增 |
| A09 | 掉落弹幕显示 | `12FB95`控制提示条目处理，`130607`按`296884`调用显示路径`130050`；相对官方普通聊天掉落通知是展示扩展，不把过滤／通知本身再次计数 |
| A10 | 局次／掉落追踪文件输出 | `1C1368`按`296924`登记掉落，`1C201A`调用`1C21E0`展开配置路径／模板并打开输出流，`1C236D`等输出表头／记录；这是CSV式运行记录，不是官方既有StashExport物品导出 |
| A11 | 分类击杀统计显示 | `1406B0/1407B5/14095E`分别读取chaos／monster／room开关，格式化并绘制对应计数；只认证显示消费者，计数来源、归属和服务端同步未全部审完，不等于官方已有MonstersRemaining剩余数 |
| A12 | 腰带各列剩余数量叠加 | `142D77`按`296BF4`调用`142830`，遍历腰带物品并按0/4/8/12等槽位分列统计后显示；不把它与官方D2GL物品数量选项或核心腰带状态功能混作整个客户端独占能力 |

以下4组不能升级为完整已确认功能，但不是只有字符串：

| 编号 | 功能组 | 已核范围／缺口 |
| --- | --- | --- |
| B01 | 光照／天气／黑夜／震屏控制 | `1A8B22/1A8B7E/1A8BAE/1A8BD3`读各开关并调用`1C82F0`安装或`1C8460`撤销补丁；具体补丁目标、兼容门控和最终效果尚未逐项核实，4个开关合为一组 |
| B02 | CPU／FPS性能补丁 | `1A8C28/1A8C4D`有实际补丁分派，受其他模式门控；目标字节和性能收益未认证，CPU与FPS合为一组 |
| B03 | 跳过NPC任务对白 | `1A8C7D`按`296B44`切换4个补丁对象；未恢复所有目标及剧情例外，不宣称全部对白可跳过 |
| B04 | SC死亡自动回城触发 | `1A60C6`读`296904`并作模式检查，随后提交WM_KEYDOWN/VK_ESCAPE；菜单文字注明仅SC。已确认自动按键触发，尚未认证事件前置条件和最终回城结果 |

### 不计入确认数的范围

| 项目 | 当前结论 |
| --- | --- |
| 自动施放传送／返门 | AutoTele已构造、有虚表及路径／传送错误字符串，但完整施法可达链未闭合；尤其当前`use_tp_tome_back`在`1A5FCB`调用`10E4D0`，后者节流后向窗口发0x10关闭消息，不能因配置名把它解释成返门 |
| 快捷移物／补腰带／合成／赌博刷新 | ItemMover RTTI存在；`item_move_shotcut`本轮只查到配置读写。`1A5F72`补腰带键有分派，但其中UI分支调用原包装器的身份未闭合；transmute／gamble配置及菜单不等于最终动作链。官方核心也可能已有同类QoL，暂不累加 |
| 独立生命条 | `1A6B59`把show_lifebar投影到`285C5C`；最终绘制归属未闭合，不能据此断言由BH独立实现 |
| 分辨率设置 | `1A6B94`取得SGD2FreeRes.dll，查询ordinal8并调用；这是明确的外部模块集成，不计BH独立渲染功能 |
| 共享／材料仓库、收藏／分类 | mdll1承担已核的存储与分类；BH对接不等于BH新增整套仓库，沿[CN材料界面](../pd2/ITEM_UI.md#cn当前分类与待适配范围)的独立边界 |
| DPS、伤害数字、快速施法、技能栏、状态计时器 | 当前DLL存在菜单或ProjectDiablo补丁线索；其中多项官方核心／渲染模块也可能提供。逐模块归属与运行路径未完成，不列为官方客户端缺失 |
| 荣誉光环、排行榜击杀、弹幕／聊天附加入口 | 有CN菜单／字符串，权限及服务端依赖未闭合；光环的不同类别不逐项计数 |
| 单机PP、掉落倍率、金币与远程开箱等规则补丁 | 有`.pp`、SC／HC倍率、金币上限、死亡金币、远程箱／盒子等明确提示和动态补丁诊断。只作下一步定位线索，尚不能把文案承诺当作当前模式下已生效规则 |
| 过滤器别名开关、未鉴定提示、物品编码等 | 有配置／UI候选；与官方过滤器表达式、发现状态及名称功能的净差异尚未逐项映射，不按数十个aliasSwitches累加 |
| 入局开地图、场景名称、怪物抗性／附魔信息 | 已有部分配置与运行引用，合并归相关地图／显示范围；完整链未恢复，不另增数字 |

明确不算新增的共有能力包括：基础掉落过滤／通知／物品地图点、物品等级／孔数／无形／符文编号、属性范围、扩展人物属性、自动组队、硬核尸体拾取授权、房间列表筛选及局名记忆。中文界面不单独计为玩法。12组A项加4组B项只表示本轮有处理或分派代码的16组差异条目；候选表不是已确认新增总数的组成部分，不据此估算修改百分比。

## 首要边界

BH::Initialize注册GameSettings、ScreenInfo、Gamefilter、Bnet、Item、Party、StashExport、MapNotify、ChatColor；StatsDisplay另建。实际入口为[BH.cpp](../../reference/pd2-bh/BH/BH.cpp)、[Module.cpp](../../reference/pd2-bh/BH/Modules/Module.cpp)及[D2Handlers.cpp](../../reference/pd2-bh/BH/D2Handlers.cpp)。ModuleManager按规范化名字的std::map加载，不等于构造顺序。上游CMake还列有不存在的源文件，不能直接add_subdirectory当作可用库。

采用所属领域内部实现、窄接口调用：地图归client／world，物品事实归RemoteInventory，显示归presentation，IO归app。BH专有语法／决策才放BH文件。D2Ptrs、裸UnitAny指针、Patch、WNDPROC、MSVC事件、GameThread及GPU闭包全部由既有D2X入口替代；不重复协议解码、权威结算或D2S保存。

## 按模块分析

路径相对官方提交的`BH/`；表内为已读关键契约，不代表全部函数或当前DLL已复刻。

| 模块／证据入口 | 必须保留的语义 | D2X归属与剩余边界 |
| --- | --- | --- |
| Item，Modules/Item/Item.cpp、ItemDisplay.cpp | 条件树AND／OR短路；规则按顺序逐次Apply，stopProcessing才停止；名称、描述、地图动作分别求值；过滤等级0的空结果不覆盖原名 | 读取RemoteInventory及现有内容目录，输出展示结果。全部token、未鉴定、动态主人值、镶孔层及发现／拥有查询仍未逐项映射，不能宣称已有完整过滤引擎 |
| MapNotify，Modules/MapNotify/MapNotify.cpp | 官方版只遍历实际Room1物品；filterLevel／pingLevel、ITEM_NEW及通知去重；声音排除loop与SoundGroup2；边框／填充／点／像素图层独立 | 复用automap_view与scene_audio；通知状态不借用玩法单位位。读取lineColor不代表实际绘制连线。历史CN的地图揭示另见下节 |
| Gamefilter，Modules/Gamefilter及ParsedFilterString.cpp | ASCII大写；gs:逗号列表、gs+1显示；其他词合并为名称子串；难度筛选在解析器外 | 唯一MCP解码后过滤值记录；gs缺失保持未知，全量源列表与选中身份保留 |
| Bnet，Modules/Bnet/Bnet.cpp | 成功入退局后保存表单；非公会局递增末尾数字，15字符边界可能回1 | 复用realm前端；失败入局不算退局，密码不进入普通配置／日志／导出 |
| Party，Modules/Party/Party.cpp | Roster一致性、最低有效partyId与邀请状态；HC尸体权限独立；循环计数节流 | 已有手动组队／关系操作，尚无BH式循环自动邀请／接受策略；应输出既有社交意图并等回包，默认开关及确认状态独立，不把已发送当已接受 |
| ScreenInfo，Modules/ScreenInfo | 经验增量／速率、时间、区域与模板；部分窗口标题／警告代码已注释 | D2X已有区域名、当前等级经验进度及腰带数量HUD，尚无完整局次统计、经验速率／升级预估和Run Tracker文件输出。历史CN的AddDrop／OnGameExit／WriteRunTrackerData包含已提醒掉落与可配置列记录；应复用角色投影与时钟，不把未观察掉落计入记录，也不照搬旧经验表 |
| GameSettings，Modules/GameSettings | 实际OnKey为UI_GAME、无Ctrl、抬键切换；大段UI及部分功能仅注释 | 复用client_preferences和输入焦点；热键、聊天与重复键冲突需核对，不将注释列为已支持功能 |
| ChatColor，Modules/ChatColor | 已解码BNCS0F事件4、发送者匹配、原代码页与颜色控制 | 复用chat_view；历史消息保留，不把聊天文本当格式串 |
| StashExport，Modules/StashExport | OnLoad设置被注释，但OnKey仍可导出；物品递归、合并、JSON／Mustache与名称排除规则 | 只读已同步库存，应用层文件／剪贴板IO；未知仓库页不猜、不为导出移动物品 |
| StatsDisplay，Drawing/Stats/StatsDisplay.cpp | 本人／佣兵行顺序、原值与已投影值区分、难度修正只算一次 | 既有人物／佣兵面板及部分FCR等投影不等于BH集中扩展属性面板；MF、FCR／FHR／IAS和档位列表可按已观察值逐项接入。历史CN含硬编码档位与职业别名，不直接套当前PD2；全部公式／字体／布局未认证，展示结果不替代战斗规则 |
| Config.cpp、Formula.h、RuleLookupCache.h | 首冒号分割并保留规则顺序；float AST与resolver不是技能整数公式；原GUID缓存不足以覆盖全部依赖 | 内容、语言、规则、物品、人物及发现revision按实际依赖失效；错误重载保留旧规则，unknown不按0隐藏或授权操作 |

GetItemDiscoveryState的发现／拥有及未鉴定候选集合、GetItemStorageType的存储域、PlaySoundWithCustomVolumeOrPriority均需窄适配；不建立GetAnyStat／FindAnyUnit／SendRawPacket式万能接口。现有能力不是空白，字段完整也不代表全部BH条件已实现。

## 历史CN模块单列

两个固定提交的BH::Initialize分别注册9个官方模块与10个历史CN模块，均另建StatsDisplay。以下比较的是公开源码，不代表同年代分支差异或当前CN二进制的完整功能。

| 大模块／组件 | 官方固定提交 | 历史CN固定提交的差异 |
| --- | --- | --- |
| Item | 过滤、名称／描述、公式表达式、发现／拥有及存储域条件 | 保留旧式过滤与物品提示；不能假定兼容官方新版Formula／Discovery条件 |
| MapNotify | 实际加载物品的过滤通知、声音及地图图元；OnLoop为空 | 增加Game／Act／Level／Room揭示、怪物／箱子／飞弹、出口文字与LK线路；版本限制见后文 |
| GameSettings | App配置与通用热键切换，旧设置页及部分操作已注释 | 旧CFG、实际创建的设置窗口及独立功能热键；配置项存在不代表其引擎功能由BH实现 |
| ScreenInfo | 地图／局次／经验信息与模板 | 增加Run Tracker、已通知掉落记录、可配置文件输出及卷轴余量检查 |
| Gamefilter | ParsedFilterString支持名称、gs:列表，另有难度按钮 | BuildGameList按大写名称子串过滤；可显示难度／服务器，不等于支持新版查询语法 |
| Bnet | 表单记忆、局名递增，含inGuildGame例外 | 保留旧式局名／密码／描述记忆与递增；配置入口不同 |
| Party | 自动邀请／接受、尸体拾取权限及循环节流 | 同类旧版策略，使用CFG开关；不据共同模块名宣称所有边界相同 |
| ChatColor | 私聊发送者颜色 | 还安装中文输入／Unicode转换、状态监视、生命／法力、伙伴头像与基础防御提示补丁；Damage Show通过MDLL1存在检查后写外部模块字节，不是BH独立伤害系统 |
| StashExport | 已注册，递归物品导出、JSON／Mustache | 源码保留，但Initialize中的构造被注释，不作为已启用功能 |
| ItemMover | 无对应注册模块 | 已注册；快捷容器移动／鉴定、药剂选择、补腰带、回城及返门 |
| AutoTele | 无对应注册模块 | 已注册；Next／Other／WP／Prev目标、地图加载、路径、施法与交互 |
| StatsDisplay | 单独创建的扩展属性显示组件 | 同类组件，含历史职业别名／档位表；不是ModuleManager注册模块 |
| MultiStash | 无对应目录 | vcxproj列入分页、界面、扩展保存及客户端／服务端更新源码；未找到MultiStash::init的调用，不能标为已激活 |

交叉证据入口：双方[BH.cpp](../../reference/pd2-bh/BH/BH.cpp)／[CN BH.cpp](../../reference/pd2-cn-bh/BH/BH.cpp)的Initialize；官方[ItemDisplay.cpp](../../reference/pd2-bh/BH/Modules/Item/ItemDisplay.cpp)、[ParsedFilterString.cpp](../../reference/pd2-bh/BH/Modules/Gamefilter/ParsedFilterString.cpp)；CN[ChatColor.cpp](../../reference/pd2-cn-bh/BH/Modules/ChatColor/ChatColor.cpp)的OnLoad／OnLoop、[Gamefilter.cpp](../../reference/pd2-cn-bh/BH/Modules/Gamefilter/Gamefilter.cpp)的BuildGameList、[ScreenInfo.cpp](../../reference/pd2-cn-bh/BH/Modules/ScreenInfo/ScreenInfo.cpp)的AddDrop／WriteRunTrackerData及[MultiStash.cpp](../../reference/pd2-cn-bh/BH/Modules/MultiStash/MultiStash.cpp)的init。

支撑层不计入上述注册数量：两者都有模块分派、原客户端Hook／Patch、Drawing／UI、异步任务、配置和缓存。官方使用App JSON配置、启动器所选过滤器路径及D2GL／PD2检测；CN使用ProjectDiablo.cfg，并通过LoadMPQData异步读取pd2data.mpq（缺失时回退Patch_D2.mpq）、MPQReader／MPQInit／TableReader后分派MpqLoaded，另有BitReader供ItemMover解析物品包。历史多页源码不能证明当前CN共享／材料仓库属于BH；须另查对应MDLL1与协议消费者。

历史CN另注册ItemMover和AutoTele，不注册StashExport。ItemMover有按类型选药、一键补腰带、回城／回城后进入门户、回城书余量提醒及快捷鉴定／容器移动；D2X已有普通使用、腰带列快捷键和原版单物品入带，不等于这些组合快捷操作。其消费者应沿现有库存／技能／交互命令，PD2增强显式隔离，不把额外转移手势重新混入原版规则。

AutoTele涉及额外地图加载、路径、施法及线程，尚未逐分支恢复；D2X已有独立调试导航／任务AI，不宜为它另建重复导航器。上述模块不作为地图显示的前置功能，也不沿用旧包解析／格子尺寸／线程或自动授予移动能力。MultiStash不能仅因源码目录存在就当作可独立提供的本地多页仓库，仍需核对注册、服务端与保存消费者。

## 小地图底图、投影与BH叠层

BH没有独立实现原版的小地图底图、城镇拼图、Fade或原生裁剪；D2Handlers::GameAutomapDraw只分派OnAutomapDraw，Drawing::Hook读取原客户端的Divisor、Offset及GetAutomapSize。底图继续以已核实1.13c和当前MPQ为准，BH源码用于精确核对附加图元；不能把整个普通地图改成历史BH-CN的增强显示。

| 证据／契约 | 当前落实与剩余差异 |
| --- | --- |
| Drawing/Hook.cpp::ScreenToAutomap | 原生整数x／y先乘32，X依次除2／divisor、Y除4／divisor，再减地图Offset，加(8,-8)；小图另加(-1,+5)。新增AutomapProjection::bhPoint独立实现该顺序，箱子、连线、怪物及附加文字共用，不重复补偏移 |
| 原版地形投影与显示 | AutomapProjection::cell保留原相机和图元分别截断、大小图除10／20、平移及单侧栏原点；DC6原偏移、PL2 Fade、地图裁剪和单位锚点接纳仍由automap_view／automap_assets执行，不借BH常量替换原版 |
| Drawing/Basic/Crosshook | 公开官方／历史CN源码都是13点、12段、X±8／Y±4轮廓。NPC及其他原生单位在所有模式保留原路径，任务点保留既有十二段轮廓；仅PD2怪物使用半径4像素的一像素“+”。“+”是按用户CN截图选择的显示样式，不把截图中的白色点推定为修改NPC的依据，也不声称公开Crosshook就是该样式；怪物仍受PD2能力与Show Monsters双门控 |
| Drawing/Basic/Boxhook、Linehook | 传原矩形端点或一条直线；历史CN箱子以坐标减1、size2调用。现有3×3绘制暂保留，原D2Gfx端点／混色及BH裁剪仍待像素对照 |
| 历史CN MapNotify::OnAutomapDraw | 怪物、附魔／免疫文字、箱子及LK线路均先ScreenToAutomap。实际文字偏移有活动的-20／-8分支，不能引用已注释的-14作为已实现规则；当前文字基线／配置与图层仍待核对 |
| 官方MapNotify与历史CN边界 | 官方固定提交只有物品通知／图元；完整怪物／箱子／出口／LK线路来自历史CN。既有D2X引导线的三类颜色、端点圆点和文字排布尚未找到同版本依据，不将其称为准确BH复刻 |

代码首先分离纯投影：[automap_projection.hpp](../../src/presentation/world/automap_projection.hpp)／[cpp](../../src/presentation/world/automap_projection.cpp)只依赖值类型，不依赖GPU、Win32、网络或宿主；公共地图绘制继续在automap_view，BH图元已沿同一投影拆为[bh_automap_view.cpp](../../src/presentation/world/bh_automap_view.cpp)中的私有绘制成员，保留原调用顺序，不复制底图／字体／资源缓存。

app现在按实际PD2资源／协议选择设置会话能力bhEnabled，该能力不写偏好。原版仍可保留用户的BH设置值，但BH面板、附加HUD／标记／引导线、常显物品、额外光照、入局自动地图及额外揭示调用均受门控，不会因旧设置值自行执行。原版Tab、大小／左右／平移、Names／Party和Fade继续走原有入口。BH面板打开时优先处理其Enter／Esc，避免聊天输入先消费；静态物件标记保留实际收包坐标，不再套用不拥有移动位置的动画缓存。运行覆盖见下文；旧探索缓存的来源分离仍按下文限制处理。
## 地图揭示的源码契约

直接依据历史CN的[MapNotify.cpp](../../reference/pd2-cn-bh/BH/Modules/MapNotify/MapNotify.cpp)及[MapNotify.h](../../reference/pd2-cn-bh/BH/Modules/MapNotify/MapNotify.h)：

| 调用 | 实际行为与边界 |
| --- | --- |
| OnLoop／ReadConfig | 有玩家且Auto Reveal开启时，按Reveal Mode选择Game／Act／Level；构造默认Act，配置Reveal Map缺省true。不是按Tab是否显示来决定生成 |
| RevealGame | 未揭示时依次处理1–5幕，非资料片1–4幕，然后设置revealedGame |
| RevealAct | 校验1–5；用本人实际地图seed、难度及资料片标志LoadAct；按`{1,40,75,103,109,137}`的区间遍历，GetLevel／必要时InitLevel；完成后恢复本人当前层、UnloadAct并置幕标记 |
| RevealLevel | 切到该层；按Room2链遍历。缺Room1时AddRoomData，实际有Room1才RevealAutomapRoom(...,TRUE,layer)，再RevealRoom预设；仅释放本次添加的数据，最后置层标记 |
| RevealRoom | 读取静态预设：部分怪物／物件专门cell，其余限定范围内读Objects.AutoMap；UNIT_TILE通过RoomTile编号匹配目标层，记录出口位置。没有生成服务器NPC／怪物或授予交互GUID |
| InitLayer／GetLevel | 原Levels层映射用于自动地图，不把levelId当layerId；优先已有有效Level，缺失才GetLevel |
| OnGameJoin／ResetRevealed | 清game／act／level完成标记及出口／提示集合；同局已完成的Game／Act不会因后来出现扩展地图自动重扫，相关代码仅为注释 |

预设坐标为`preset + room*5`，物件图元使用`((x-y)*16)/10+1`、`((x+y)*8)/10-3`，沿原整数截断；不从屏幕像素反推位置。普通AutoMap和出口文字／连线是不同输出，尚未匹配RoomTile的出口不能填一个猜测目标。

历史特例必须按实际可达分支解释：怪物256／745；物件580仅Lower Kurast、371、152、460、402在46层置0、376；其余Objects.AutoMap回退仅ID≤572。最终仅插入`0<cell<1258`，因此460分支赋1468在该函数不会画出，不能照注释认定已显示安雅。

该分支跳过161–163层，层号检查却允许255而完成数组只有255项；不能复制越界。旧Act区间到136为止，不能据此声称137以上PD2地图全幕自动揭示。特例身份、cell范围、排除层和实例种子须结合当前MPQ／版本核对；不把旧S6数字推广到当前CN。部分房间加载失败仍可能置层／幕完成位，这是上游限制，不能把该布尔值当全图成功凭证。

## D2X现状与差距

| 当前入口 | 已有能力 | 边界 |
| --- | --- | --- |
| [PD2 CN策略](../../src/client/pd2/automap.cpp) | PD2模式下从普通前端循环调用内部服务，使用bhRevealMap偏好 | 同时适用于pd2-local；不在原版调用，不展开整个Act；不复用历史Act区间或未核实的161–163旧排除 |
| [AutomapReveal](../../src/client/automap_reveal.cpp) | 独立生成会话，按本人当前level及已解析Vis／Warp=-1闭包分步导出全部房间 | 不跨楼梯、洞口warp或门户；不按当前相机／已指派房间裁剪，但也不保证带特殊机关的每个格实际可达 |
| [NativeMapGenerator::visitAutomapRoom](../../src/world/native_map.cpp) | 沿既有prepare／initialize导出原瓦片和预设；只释放本次临时创建的房间数据 | 不增加实时nativeMap_视野根、不安装CN房间回调；原自动初始化的房间保留至专用会话销毁 |
| [RetailTileRoom](../../src/world/retail/tile_materialization.hpp) | 导出静态预设值与匹配到真实warp目的房间的出口记录 | 预设描述不是玩法实体，不分配GUID、不调用人口生成、不提供NPC／任务交互能力；出口记录尚无独立文字绘制消费者 |
| [RemoteTown](../../src/client/remote_town.cpp)及[探索保存](../gameplay/world/AUTOMAP.md#跨进程探索保存) | 独立叠层仅在显示时与自然图元合并，自然图元优先 | 不进入explored_、原.map／.maN、导航索引或live Map／Grid。旧版本已经混入普通缓存的开图内容无法自动无损拆分 |

当前BH面板和偏好已有13个开关，不能继续写成“BH尚无任何D2X消费者”；这些显示开关也不能当作公开BH模块已完整移植。

## 内部实现与BH调用边界

当前实际调用链：

```text
frontend（仅PD2模式）→ client/pd2/automap::updateAutomapRevealCn
  → RemoteTown::updateAutomapReveal → AutomapReveal（当前连续步行区域）
  → 专用NativeMapGenerator::levelRooms／visitAutomapRoom
  → 原AutomapCatalog选cel、静态预设／已匹配出口
  → 显示叠层合并 → 既有automap_view
```

内部服务不读取BH开关或协议profile，调用者提供预设cel选择函数；CN函数保留历史RevealRoom的特例与最终`0<cel<1258`过滤，再由内部服务校验当前MPQ帧数。普通地形继续使用当前Automap表。原版可复用内部能力，但普通模式没有调用者；入局打开地图和开图是不同开关。

每次调用最多分配一个level布局或导出一个房间，完成后销毁专用生成器，只缓存纯值图元。布局分配本身仍是同步操作，不能据房间预算宣称毫秒硬时限。相同身份下完整结果可复用；关闭会立即撤去叠层并取消未完成会话，重开时未完成部分重算。游戏／区域代次、实际地图／物件seed、幕及难度变化失效；内容与profile由持有它的固定Archives／RemoteTown生命周期隔离，不按level编号跨实例复用。

`AutomapReveal::revision`另供公共自动地图静态投影缓存判断叠层变化：新结果、分步导出、完成结果切换及取消／重置推进修订，重复读取同一完成结果不推进；失败前已经产生的partial输出同样失效。消费者`RemoteTown::updateAutomapBase`在源修订变化时重新按自然图元优先合并，关闭／切换后不会保留旧叠层。该修订不参与CN回调计数、原协议、地图身份或自然／导航探索保存；这一缓存消费者改动尚未构建／运行，历史开图回归不认证新源码。

沿BH的加载→地形→预设→仅释放临时数据顺序，不进入GameHost／population／单位身份分配。独立房间会话内部仍计算原碰撞和预设描述，均不合入实时地图、寻路或服务器人口。缺资源或房间失败保留partial及明确reason，不能当完成；原CN房间上报仅来自原实时重建路径。

范围按用户要求改为当前连续步行区域，区别于历史CN缺省Act。进入楼梯／门户后的新区域才重新选择范围；没有Act／Game扩展。相同seed并不证明全部房间／随机顺序与原客户端相同，逐房间及逐像素对照仍是独立验收项。

显式`debug-reveal-map`也调用同一内部服务，action为on／off／auto／status；on缺省打开地图，show=false可只准备叠层。它可用于原版调试，但不会开启BH模式或怪物标记；原版预设仅读普通Objects.AutoMap，PD2走CN预设选择。覆盖只在本局有效，不改BH偏好；auto恢复模式／偏好控制。诊断入口与完成判定见[调试管道](../development/DEBUG_PIPE.md)。

### 小地图功能盘点

下表按固定官方与历史CN源码逐项检查；面板开关存在不等于整个BH已经移植。当前安装的BH.dll并未全函数恢复，用户截图只能证明其中可见样式，不能作为隐藏规则依据。

| 功能／源码 | 当前状态 |
| --- | --- |
| 原底图、大小／左右、平移、Fade、Names／Party | 共用原版路径；BH只提供投影与叠层，不替换底图程序 |
| RevealRoom／Level、静态预设、房间借还 | 已接内部服务；本次只选当前步行闭包。Act／Game不属于用户选择的范围 |
| Show Monsters、按品质颜色、抵抗／附魔文字 | 有消费者，PD2模式才显示；细线“+”依据当前用户CN截图。完整Hidden／Monster Color／Super Unique Color／Monster Line配置及版本化黑名单尚未接齐，文字排版仍待原客户端对照 |
| Show Chests、IsObjectChest | 已接未打开／Selectable0／OperateFn筛选；小框的原矩形端点、BTHighlight混色仍未逐像素认证 |
| LK Chest Lines | 有消费者，当前读取实时已分配预设160并使用缺省色105；完整配置、与旧580特殊标记的版本关系仍需核对 |
| Display Level Names、正确古墓提示 | 当前Area Name只是本区HUD；已导出的warp出口记录尚无目标名称／正确古墓文字消费者，不能记为完成 |
| Show Missiles、GetRelation、Missile Color | 未接小地图飞弹框及本人／中立／队伍／敌对四色选择；已有主场景飞弹显示不等于本功能 |
| 官方MapNotify的物品地图框／通知 | 尚缺完整Item规则输入与消费者：按规则顺序、stopProcessing、filter／ping等级、ITEM_NEW／去重、border／fill／dot／pixel顶层绘制及声音条件；不能用硬编码品质图标替代过滤规则 |
| 当前D2X Path Line、端点圆点 | 已有对实际已知目标的引导，风格尚无对应公开BH同版本依据；不据当前CN截图猜出所有颜色／目标选择规则 |
| 自动入局地图、开关及模式隔离 | 有消费者；客户端偏好缺省关闭。PlayPD2读取项目根client-settings.json，Ctrl+Z设置Reveal Map，Tab独立控制是否显示 |

因此本轮交付的是内部开图、CN调用、调试入口及指定“+”样式，**不是BH小地图全部功能完成**；上表缺项保留为可定位的后续工作，不新增无依据的替代功能。

## 实施顺序与待验收

| 顺序 | 交付与完成条件 |
| --- | --- |
| 1 | 会话能力门控、独立投影、PD2 CN调用及自然探索／显示叠层分离已接入 |
| 2 | 分步临时房间导出已接入；当前范围、借还平衡、无人口／兴趣副作用及实际显示按运行结果验收 |
| 3 | 核对当前MPQ的预设／出口、特殊cell及实例重入；不把历史Act／Game和旧排除层列为本次待实现范围 |
| 4 | 补齐生命周期和有限运行对照，再继续Item过滤→名称／可见性→MapNotify；其他模块按事实／命令契约逐项接入 |

复用现有游戏、资源工具及调试入口，不新增测试脚本、用例或专用程序。默认按协作约定等待授权再构建／运行。

### 当前构建与运行覆盖

2026-10-29经用户授权完成Windows MinGW Release的`cmake --build build --config Release --target all --parallel 3`，全部目标成功，编译／链接零警告；新增调试／“+”及无图元拒绝分支均在修改后重编译并复测。最终EXE SHA256为`2F365C0E487B6ACFE04E94EE46664FAF02A38703D392B7446600CE707BEA81DC`。当前开图日志、各阶段EXE身份、原包、状态和截图位于忽略目录`artifacts/automap-reveal-20261029/`；此前投影／面板／箱子回归保留在`artifacts/automap-bh-20261029/`，不把历史图形结果当作新开图的依据。

| 条件／操作 | 实际结果与边界 |
| --- | --- |
| 原版MPQ、隔离DebugSorceress，保留开启BH偏好 | 不自动开图，BH面板打不开，野外不显示PD2怪物标记；显式debug-reveal-map可完成当前步行闭包，auto恢复disabled，未开启BH能力 |
| 当前PD2资源、隔离SkillSorceress，自动／手动开启 | 营地闭包只有1／2／3／4／17，333房间完成，临时借还298／298，生成器销毁后activeRooms=0。没有展开8号洞窟、5号黑暗森林或整Act |
| 正式PlayPD2脚本、项目根配置、单机菜单选角 | 原配置Reveal Map=false时无额外揭示；Ctrl+Z→Enter→Esc开启，Tab显示，333房间完成且无会话错误；与隔离pd2-local入口相同，使用副本角色及独立HostSaves |
| 冻结宿主、开图前后隔离对照 | tick=985保持不变；怪物11、物件22、物品8、原生身份43及分配游标相同；客户端单位34、07房间9、cachedAreas=1／2相同。自然探索仍为2337格，显示图元731→3804；Save前后原.map／.maN的SHA256完全一致 |
| 关闭／重开及调试覆盖 | 关闭后图元回731，完成缓存重开无需再借房间；on可覆盖关闭的BH偏好，auto恢复偏好；show=false完成开图仍不显示地图。新137场景running期间off取消并释放会话，重新on后从布局重新开始 |
| 不同生成类型／换区 | 8号洞窟仅本层27房间；40号城镇步行闭包仅40–45，447房间；140号PD2预设仅本层361房间、2505图元，借还平衡。均为管理旅行准备条件，不作为正常地图开启、战斗或掉落流程证明 |
| 137号UberDiabloLvl无图元边界 | 当前MPQ LevelType=26、Layer=98；18房间导出零图元。返回partial及“Current MPQ automap rules produced no artwork…”原因，不缓存为完成、不自造替代地图；仍需原CN特殊自动地图入口依据 |
| 当前CN用户截图／PD2野外实际怪物 | 按品质着色的PD2怪物“+”已有运行观察；NPC现恢复所有模式共用原生十二段标记，未将截图白点作为NPC改形依据。本次恢复需另以新EXE核对，不沿用此前NPC“+”画面作为验收；原版不显示PD2怪物点 |
| 1280×800图形客户端，城镇及Blood Moor | 大小图、左右切换、每键16像素正／负平移、Home、背包／人物侧栏避让、Names／Party开关均可操作；大图No→Center→Everything→Auto、小图No→Everything→Auto，正常Automap Options中Center转小图回No。仅验证所列操作和画面，没有原版同分辨率像素金图 |
| 实际收包怪物及Objects.Id=5容器 | 修复后大小图出现怪物十字及箱子白框，负平移可用；Show Chests关闭撤框，重开恢复。容器由宿主管理生成，只用于准备显示条件，不证明自然生成／交互／掉落；文字基线、矩形端点和边缘裁剪仍未完成原版对照 |

以上为自研单机宿主经原协议的客户端回归，`online.error`未报错；测试客户端恢复宿主后正常退出，观察到C2S69→S2C05／06及进程结束。不代替原版Host、远端PD2／CN或Linux验证。日志仍有既有字体资源、原版音乐缺失及并行客户端音频设备提示，未验收音频；截图和角色副本不纳入源码。

待验收：所有五幕／扩展区域与特殊图元；原版／PD2跨进程偏好及缓存往返、各幕光照／常显物品对照；BH文字／连线与640／800布局、边缘及全部侧栏组合的像素对照；同区动态新实例、同seed的房间／随机与cell首差；更广泛缺资源、取消／切幕／退局交错；远端CN上报次数对照。历史161–163旧排除、1468被旧cell界限过滤、255旧数组边界及当前BH.dll的真实来源仍不因当前有限回归而获得认证。其余功能缺项以上面的盘点为准。
