# 客户端调试任务AI

本功能把一次高层目标或目标队列转为正常客户端操作。它属于app/debug，默认不启动；只在图形客户端调度，headless入口不执行该AI，不构造另一套战斗、掉落、任务或存档规则。现有单目标行走仍见[调试管道](DEBUG_PIPE.md#客户端自主行走)。Windows Release已构建并执行有限实机检查，实际覆盖与未验收范围见文末，不沿用早期debug-walk桥头往返及39传送点结果。

## 输入与组合

不需要逐步给坐标、GUID、施法目标、门或NPC消息编号。入口通过现有Send-D2XCommand.ps1发送，任务在游戏主循环中持续执行；命令返回只表示计划已接收。当前位置、目标、进度和失败原因通过status观察。当前支持原版1.13c客户端配置，不将零售任务配方套用到PD2。

一次安排完整的“交易的工具”流程：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-start -Arguments @{
    scenario = 'tools-of-the-trade'
    combat = @{ skill = 'Frozen Orb' }
}
```

也可组合高层目标；已知铁匠、恰西和马勒斯的位置／来源提示会自动补全：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-start -Arguments @{
    goals = @(
        @{ action='hunt'; monster='smith' }
        @{ action='acquire'; item='hdm' }
        @{ action='return-town' }
        @{ action='deliver'; quest='tools-of-the-trade' }
    )
    combat = @{ skill='Frozen Orb' }
    goalTimeoutSeconds = 600
}
```

区域使用当前MPQ的Levels.Name（可省略“Act 1 - ”一类前缀），单位使用MonStats.Id、物件使用Objects.Name，物品使用原代码，技能使用Skills.skill；匹配不区分ASCII大小写，歧义与未知资源明确拒绝。也接受已存在的数值身份。没有位置提示的目标可给`area='Barracks'`等粗粒度区域；不要求调用方指定路径。这里是结构化目标接口，不解析自由文本自然语言。

| action | 目标及参数 | 实际完成条件 |
| --- | --- | --- |
| travel | 必填area | 目标区域的原生场景已就绪 |
| find | monster／npc／object三选一；可给area；NPC可interact=true | 真实单位已指派并接近；要求交谈时观察到该NPC对话 |
| hunt | monster；可给area | 指定真实身份出现死亡模式；已存在的尸体可满足“目标已死”，不声称个人击杀归因 |
| acquire | item；可给area及source物件名 | 对应物品实际归本人所有，包含原任务查询允许的根库存／cursor范围 |
| return-town | 无必填参数 | 本幕真实城镇场景已到达 |
| deliver | quest语义配方；当前只有tools-of-the-trade | 对应物品已被消费，且真实角色任务奖励位已更新 |

整个计划最多32项目标，每步默认600秒，可用goalTimeoutSeconds或各goal.timeoutSeconds设置10–3600秒。没有完成当前目标不会执行后续目标。一次请求先完整解析／校验，再接收计划；append同样先校验整批，不部分插入。

| 命令 | 行为 |
| --- | --- |
| debug-ai-start | 接收scenario或goals；接管独立debug-walk；已有活动计划须先cancel或使用append |
| debug-ai-append | 给现有running／paused计划追加goals；继承原战斗／移动策略，不接受新的combat／mobility配置 |
| debug-ai-status | 只读状态、当前goalIndex、完整队列、phase／reason、决策及请求计数、实际目标和最近128条事件 |
| debug-ai-pause | 停止本AI输入及技能动作，保留队列／已发请求的观察状态；不暂停宿主 |
| debug-ai-resume | 同一游戏、区域及区域代次内恢复；暂停时间不计入本步期限；暂停中换区须取消并建立新计划 |
| debug-ai-cancel | 终止计划并沿正常客户端Stop停动作；不撤销已经发生的拾取、死亡或交付 |

人工移动／施法点击、技能与装备切换、饮药、ESC、显式独立debug-walk、修改客户端状态的online-*请求、退出或换游戏接管控制。Tab、小地图平移／显示及只读观察不取消计划；人物、包裹、任务等观察面板保留计划并暂停AI输入，关闭后继续。管道ui-input同样按实际意图判断，不因观察帧取消。最小化、表现暂停及游戏菜单阻止AI输入。计划只在内存中保存，不写D2S，不在新游戏恢复旧GUID、目标或请求。死亡停止任务，不添加自动复活。

## 执行分层

`DebugAi`负责队列、目标完成条件、旅行／探索阶段、战斗优先级与有界观察记录；`DebugAutoWalk`负责单次接近与0.3秒路径重规划。两者都在app主循环运行，和正常输入共用RemoteControl、RemoteCombat、RemoteInventory、RealmSession原操作入口，不直接编码另外一套游戏消息。

元数据在第一次start／append时按当前挂载惰性读取，未启动AI不额外加载这些表。房间、碰撞、单位、库存、技能和任务状态均来自正常客户端副本／重建；不读取GameHost、server-snapshot、服务端隐藏位置或本地存档，不调用管理旅行、生成、伤害、授予技能、回血或解锁。

旅行按本幕已解析的Vis连接规划区域路线，允许使用角色实际已解锁且能缩短路线的传送点；尚未收到0x63历史时先正常打开当前可用传送点。步行连接先到真实开口，再提交普通跨界移动；warp用当前实际指派单位交互。公共接近流程会处理附近可接近的已指派关闭门，包含探索、战斗接近、地面物品、NPC、架子、门户和传送点；开门接近保留当前目标直到子导航完成或明确失败，避免移动中在两扇门之间反复改选；门按GUID及指派代次记录待确认操作，等待实际模式变化后才记为已操作；同一指派持续关闭12秒则明确失败，不把accepted当作开门成功，也不盲目重复切换。门操作不能误记成物品来源或传送成功。服务端继续校验钥匙、任务及碰撞。已在身边的门户／物品／NPC交互先完成，不被远处开门抢占。子导航沿前端已按本地DynamicPath选择的场景绑定，不再无位置重选到服务器稀疏采样所在的另一侧。区域与生成代次更新后重新绑定，不将旧child导航的取消误当成旅行成功。当前没有NPC跨幕交通规划，也不补造未解锁的跨幕通道。

已知地图是客户端累计探索的完整区域，不受屏幕、小地图窗口、缩放或当前渲染范围裁切。已知地形与本任务已搜索覆盖分开记录；RemoteTown复用原AutomapExploration层及NativeMap，按游戏／区域／地形修订和新增探索更新只读房间／连接索引，并按const引用复用。AI不另建地图生成器，不每次重构地图。每次最多对8个候选房间详细寻路，不可达候选按碰撞修订缓存；已访问房间和观察过的接近点持续保留。

NPC、物件及出口的位置线索只从实际客户端单位分配记住，撤出当前兴趣范围后仍可作为最后已知位置接近；重新收到实际单位才交互或判断完成。已指派但中间地形尚未物化的warp先接近已知可达前沿，再精确交互。新游戏和同幕地图种子改变清相应线索。不读取NPC预设、未见NPC的位置或宿主隐藏状态。

探索只对当前已物化碰撞提交精确移动，未物化房间以可达前沿接近；探索点在进入观察范围后结束，默认不补走墙前最后16子格，不把接近前沿误记为进入未加载房间。门、拾取和实际关卡连接保留必要的精确接近。普通移动带来后续房间消息；正常跨区替换子导航时重新绑定，不把取消当旅行成功。不调用完整地图生成、房间激活或revealAll接口。无可达新房间时等待正常更新并保留原因，最后受目标期限约束。

移动／战斗每0.3秒判断，不要求角色先停下。任务被战斗打断后重新开始必要的接近，不重复已经提交的物品／剧情操作；已提交传送点或warp交互时先等真实区域结果；已提交蓝门创建时也保留施法输入，避免战斗切换技能打断开门。蓝门创建超时且未实际使用门户时只记录未确认结果，不重发开门或假定卷轴已消费，回退正常步行／已解锁传送点；其他超时仍明确失败。死亡、拾取、回城及交付分别使用实际死亡模式、归属、城镇场景和任务位作完成依据；accepted、Updated或acknowledged都不单独表示成功。目标消失不能当作死亡。

## 战斗策略

未指定combat.skill时，按当前实际掌握的主动技能依次选择Frozen Orb、Fire Ball、Zeal、Attack；女巫因此优先冰封球，也可显式固定其他技能。普通攻击配置沿右手选择与施放入口执行；战斗光环存在时，右手选择光环、左手使用攻击技能，攻击技能须支持左手。结束时保留最后选中的技能，不自动加点、装备或替换技能规则。指定技能不可用时保留正常客户端拒绝原因，最终受本步期限约束；不赋予法力、忽略免疫或伪造命中。目标被墙隔开时先接近／开门，不持续对墙施放。

探索／赶路默认只处理贴身威胁，不清除所有看见的怪物；接近指定hunt目标后，才考虑为击杀处理其附近敌人。优先级为近处拥挤后撤、与当前目标有关的战斗、当前高层目标。当前策略参数均是调试输入策略，不是原版玩法数值：

| combat参数 | 默认 | 用途 |
| --- | --- | --- |
| enabled | true | 是否在任务期间自动战斗；hunt要求开启 |
| skill | 按已学技能选择 | Frozen Orb优先；MPQ技能名／ID仍须当前角色可用 |
| scope | defensive | 赶路仅处理crowdRadius内威胁，hunt目标附近可接敌；clear才处理整个engageRadius |
| aura | 自动 | 优先已掌握的Fanaticism／Concentration／Might；也可显式指定已学会的原光环 |
| engageRadius | 18 | 处理当前已知敌对单位的范围 |
| range | 16 | 远程接近距离；Attack及MPQ h2h技能使用近战接近距离3 |
| crowdRadius／crowdCount | 6／4 | 范围内达到指定敌人数才后撤 |
| retreatDistance | 8 | 在已知可达地形选择更安全的位置 |

后撤从16个方向的已知可达点选择距当前敌群更安全的位置，最多保持2秒，至少4秒间隔后才再次发起后撤，保留攻击窗口；无安全路线时继续按正常技能资格处理。敌对资格复用RemoteCombat，不攻击中立NPC、自己或盟友的宠物。固定技能最多每600毫秒尝试一次独立施放，正常技能／动画／资源与请求限流仍生效。

## 职业移动策略

默认`mobility.mode='auto'`按当前已学技能选择：女巫优先Teleport；有Charge的圣骑士使用左手Charge，并在右手选择已学Vigor；只有Vigor时保持普通行走。配置`mode='walk'`关闭传送／冲锋，`vigor=false`可同时关闭移动光环。也可选`teleport`或`charge`，技能未知、城镇禁用、落点不合适或正常请求不允许时仍回退步行，不授予技能和法力。`distance`默认24、范围8–30，为单次调试输入距离。

传送方向沿当前已知步行路径的后续进度选择，不能只按到终点的直线距离判断；落点须能继续连接选定路径，避免反复传进弯墙凹角再步行退出。同一步对同一目标／落点最多尝试两次。原生寻位返回整数格，检查、碰撞余量与实际发送均使用同一个整数落点，不对其减半格后再次向下取整。前进及侧向候选比较2／4／6子格碰撞余量，优先留出内部观察距离，不反复贴地图边界。允许跨过墙壁，但不生成隐藏地图；读取当前Levels.Teleport并保留原服务端规则裁决。提交后等待本人原0x15位置不连续通知，再按真实路径位置继续搜索，不把普通行走进度算作传送完成。连续传送不插入人为等待期让旧行走补齐墙前坐标。冲锋只选普通碰撞及原整数射线都畅通的方向，等待实际移动进度。两种施放期间暂停子行走输入；选择技能等待确认，连续请求受正常Pending门控与节流；无位置进度则等待3秒并暂退普通行走。短距离的门／NPC／拾取仍正常接近。

进入战斗范围后停止移动策略；右手由Vigor切换到战斗光环，再用左手攻击；敌群清除后恢复移动策略。调用示例仍是一条完整任务，而不是逐技能指令：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-start -Arguments @{
    scenario='tools-of-the-trade'
    combat=@{skill='Zeal'; aura='Fanaticism'}
    mobility=@{mode='auto'}
}
```

原调用依据为D2MOO `SKILLS/SkillSor.cpp::SKILLS_SrvDo027_Teleport`（Levels.Teleport和原飞行碰撞分支）、`SKILLS/SkillPal.cpp::SKILLS_SrvSt31_Charge`／`SKILLS_SrvDo067_Charge`及`SKILLS_SrvDo065_BasicAura`。现有实现及本机1.13c证据见[女巫](../gameplay/skills/SORCERESS.md)、[圣骑士](../gameplay/skills/PALADIN.md)。自动选择／距离／探索是调试输入策略，不是原版自动玩家功能。

## 可复用女巫基准档

已用现有`d2x_assets save-recipes`准备独立`DebugSorceress`：99级、正常难度、全新任务，分配490属性点（力量70、敏捷50、体力320、精力50）；98基础技能点包含Teleport20、Fire Bolt20、Fire Ball20、Fire Mastery20，以及Frozen Orb8和所需前置、Cold Mastery／Static Field／Warmth。入局实际面板上限为生命1039、法力982，四抗75。初始左右手为Fire Ball／Teleport，AI默认选择冰封球。未修改现有角色。

装备来自当前MPQ原UniqueItems：Harlequin Crest、Skin of the Vipermagi、Wizardspike、Lidless Wall、Magefist、Arachnid Mesh、Silkweave、Mara's Kaleidoscope及两枚Bul Katho's Wedding Band，另带10张回城和2张鉴定卷轴，保留背包空间。Wizardspike避免The Oculus随机传送干扰定位；基础力量80／敏捷75满足这批原装备需求，装备词条沿现有生成器掷定，没有自造装备属性或无限资源。高等级／装备是用户授权的调试准备，不是自然成长或掉落证据，也不能绕过原免疫规则。

可重现的装配输入为[debug-sorceress.json](fixtures/debug-sorceress.json)。二进制与清单放忽略目录`artifacts/debug-ai-characters-20261029/`：`baseline-balanced/DebugSorceress.d2s`及manifest为只读基准，`active-balanced/DebugSorceress.d2s`为独立工作副本，`preparation-balanced.json`记录资源工具、配方与D2S的SHA-256。现有资源工具完成原codec编码／重读，并以独立副本多次实际入局、正常保存退出；不认证原版客户端准入。基准D2S SHA-256为`547B27AEB3D0697F41C5046E9EEB03393E327B31B9E203EA3DAA56D2B388500A`。面板法力上限982，而同档宿主快照约843.9，两者投影差异尚待核对；不能将面板数值当作服务端实际可用法力。

重复任务时先正常退出并关闭使用工作目录的进程，再把基准复制到一个新的独立角色目录，清除该副本的只读属性，以该目录启动新游戏。新游戏同时重建本局怪物、架子／掉落及任务运行态；不只清角色任务位后继续使用已完成任务的旧游戏。这样保留固定装备、技能及地图种子，同时回到全新任务资格。需要其他幕的前置状态时另建合法基准，不向运行中的客户端增加改任务入口；当前只准备全新正常难度基准。

## 交易的工具配方

当前MPQ中，MonStats的smith、charsi分别对应402、154，Barracks为28，Objects.Malus为108／OperateFn21，马勒斯代码为hdm；实现通过名称／表记录动态解析，不把这些数值写成调试目标坐标或生成参数。

依据本地D2MOO `QUESTS/ACT1/A1Q3.cpp::OBJECTS_OperateFunction21_HoradrimMalus`：操作真实架子按当前人物资格创建hdm；击杀铁匠不是其掉落来源。配方按用户目标额外先满足铁匠已死，再操作架子／拾取；已拥有hdm可跳过重复取得。原版至少8级、本局及个人任务资格仍由实际任务链裁决，AI不修改资格。

同文件的ScrollMessage 163在实际持有hdm时删除物品并设置PRIMARYGOALDONE／REWARDPENDING。交付器仅确认恰西实际提供的自动文本及该配方主题，不向未提供的消息编号发伪造确认；原任务槽来自公共questDefinition。原查询和消费范围见[第一幕任务](../gameplay/quests/ACT1.md#原规则证据)。完成表示马勒斯已交回、灌注资格可用，AI不替玩家选择物品消耗灌注。

## 实现入口与待验收范围

[ai.cpp](../../src/app/debug/ai.cpp)负责解析／队列／绑定，[ai_navigation.cpp](../../src/app/debug/ai_navigation.cpp)负责旅行／探索／门，[ai_actions.cpp](../../src/app/debug/ai_actions.cpp)负责战斗／物品／回城／交付，[ai_mobility.cpp](../../src/app/debug/ai_mobility.cpp)负责职业移动／技能确认。[RemoteTown](../../src/client/remote_town.hpp)只读暴露已经重建的房间和连接；[frontend](../../src/app/frontend.cpp)持有唯一输入所有权及调度。

2026-10-29，Windows／MinGW GCC13.2、Release，`cmake --build build --config Release --target all --parallel 3`完成全目标编译／链接及末次修正复编，成功日志无编译／链接警告或错误。最终d2x.exe SHA-256为`04D4027CEF11DACF04A12666A76ED0DA6A9E32E32CB055B301285E2958CB3C1A`。Linux、Debug和PD2未构建／运行；没有新增测试脚本、用例或专用程序。

运行证据在忽略目录`artifacts/debug-ai-acceptance-20261029/`，各目录保留EXE摘要、PID、独立存档、C2S／S2C及完整快照；使用原版assets/mpq2、正常难度、seed557488535。有限覆盖如下，不能合并成同一版本完整通关：

- 较早版本run-04实际从营地连续经2／3／4／10／5／6／7到26；跨区绑定修复后继续正常旅行到军营28。该结果早于最后的传送／warp修正。run-04／05实际寻找Akara并交谈，run-05验证Tab开图保留计划、队列追加及暂停恢复。出生检查同seed、未移动时的实际单位／小地图标记没有Akara；这只是D2X观察，不是原版客户端对照。
- 版本`DC89A302…`的run-08从原循环落地处（管理旅行到地下通道10、本地94,158）自主离开并继续探索；最终C2S69及S2C05／06正常离局，不能当作整条任务完成。此前run-05的重复落点与左走／右传循环已留原包及截图。
- 版本`7FC66D02…`的barracks-05从军营本地37,295准备点，以完整任务配方自主开门、冰封球战斗、观察铁匠死亡、操作真实Malus架子并拾取hdm。后续进入战斗且蓝门未出现而超时，未认定回城／交付通过；最终源码已保护这段施法输入，后续取样没有当前门户，但取样晚于AI移动，不能据此判定创建从未发生。最终另修正身边门户／物品／NPC交互被远处开门抢占的问题。管理旅行仅准备现场，不证明营地到军营的自主旅行。
- 版本`DC89A302…`的ui-01以实际鼠标输入点击恰西本体打开交谈、私人箱本体打开仓库；丢下鉴定卷轴ID21后点击地面原图，同一ID恢复本人背包。判定框只覆盖这三个具体操作，全部动画／重叠目标仍待验收。

版本`BB48F10A…`的loop-verified从原循环现场实际跨过地下通道出口到石块旷野4，同时验证Tab、暂停／追加return-town／恢复；其后蓝门未获确认而停止，不认定返城通过。最终版本final-door使用此前正常保存、实际持有hdm的独立副本，在营地正常寻找恰西并确认实际提供的163：同一马勒斯消失、奖励位生效，未消耗灌注；该局开始已在城镇，不能把第一步城镇观察当作野外回城证据。

已知未闭合：军营返外侧回廊时，角色到15140,4799，边界门15141,4802仍关闭；较早版本把提交当完成，最终源码改为等待门模式并明确超时失败。当前objects::execute存在object.area与actor.area不同时拒绝的检查；D2MOO PlrMsg的UNIT_OBJECT分支按实际单位、距离、碰撞进入ObjMode，跨级房间链及局部坐标的公共适配仍需核对，未在调试层绕过。另一边界为本次条件下蓝门创建未获实际门户确认；回城器只尝试一次，保留未确认原因并回退正常旅行，不能据此认证开门／耗卷轴成功。最终版本final-door已实际确认同区门18的模式变化，并对仍关闭的边界门进入door-wait，12秒后以“Door interaction submitted but the assigned door stayed closed: 25”停止；没有撞墙重发。正常保存退出已观察到C2S69与S2C05／06。完整“营地出发→铁匠→取物→回城→交付”尚未通过同一最终版本全程验收。

最终版本仍需覆盖：从全新营地自主完成整条任务及更多种子；锁门／已开门／迟到回包，完整跨区及目标撤出重指派；满背包／归属迟到、已有门户／缺卷轴步行、更多交付资格与失败组合；无技能／法力、死亡与后撤恢复；传送失败回退、圣骑士Vigor／Charge／战斗光环完整组合；暂停时已受理操作／人工接管、断线与结果未知，以及基准复制后的完整重复任务。圣骑士较早独立副本只观察过Charge请求和战斗光环选择，不认定整条任务通过。面板／宿主法力上限差异见基准档段落。
