# BH客户端增强与内部能力接入

## 身份与范围

本页维护BH源码结论、D2X现状与接入边界；实现状态以源码为准。地图揭示放在D2X内部地图模块，PD2 CN入口负责开关与调用，不建立独立BH库或第二套地图算法。当前范围为本人所在的连续步行区域，不展开整个Act／Game；投影、模式隔离和开图验证分别见下文。

| 来源 | 固定身份 | 采用范围 |
| --- | --- | --- |
| 官方Project-Diablo-2/BH | `92e9d319f20113b5fbd382ad0fd3df9480116d04`，2026-10-19；`reference/pd2-bh` | 当前公开模块、过滤／通知／配置算法；MapNotify::OnLoop为空，没有地图全揭示调用链 |
| 历史weili0677/BH-CN | `68f7343342eed668190d464b7111849a6cc0ce89`，2022-12-27；`reference/pd2-cn-bh` | RevealGame／Act／Level／Room的直接源码依据；S6历史分支，不认证当前CN或官方BH.dll行为 |
| 本机BH.dll | 摘要及样本归[DLL取证](../resources/PD2_CN_DLL_ANALYSIS.md) | 尚未证明与上述任一提交一致，不能仅凭同名函数套用源码结论 |

两个源码仓库的LICENSE.md均为AGPL-3.0文本，另有Apache／ThirdParty文件；实际采用代码时保留对应文件声明、固定提交及修改说明，不擅改项目许可证。来源统一见[资料来源](../resources/THIRD_PARTY.md)。不把reference、原DLL、注入补丁或导出资源编入产品。

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

历史CN另注册ItemMover和AutoTele，不注册StashExport；MultiStash目录存在但注册未核实。ItemMover有按类型选药、一键补腰带、回城／回城后进入门户、回城书余量提醒及快捷鉴定／容器移动；D2X已有普通使用、腰带列快捷键和原版单物品入带，不等于这些组合快捷操作。其消费者应沿现有库存／技能／交互命令，PD2增强显式隔离，不把额外转移手势重新混入原版规则。

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
