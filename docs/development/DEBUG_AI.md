# 客户端调试任务AI

本功能把一次高层目标或目标队列转为正常客户端操作。它属于app/debug，默认不启动；只在图形客户端调度，headless入口不执行该AI，不构造另一套战斗、掉落、任务或存档规则。现有单目标行走仍见[调试管道](DEBUG_PIPE.md#客户端自主行走)。Windows Release已构建并执行有限实机检查，实际覆盖与未验收范围见文末，不沿用早期debug-walk桥头往返及39传送点结果。

第一幕六任务配方、200毫秒决策、药剂使用／购买和自动补给已通过Windows Release构建并进入实机回归；完成条件与实际覆盖见文末，源码支持不等于全部组合已验收。

## 输入与组合

不需要逐步给坐标、GUID、施法目标、门或NPC消息编号。入口通过现有Send-D2XCommand.ps1发送，任务在游戏主循环中持续执行；命令返回只表示计划已接收。当前位置、目标、进度和失败原因通过status观察。当前支持原版1.13c客户端配置，不将零售任务配方套用到PD2。

一次安排第一幕全部六个任务，优先使用角色已有的传送和固定战斗技能：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-start -Arguments @{
    scenario = 'act1'
    mobility = @{ mode = 'teleport' }
    combat = @{ skill = 'Frozen Orb' }
}
```

这会按任务日志顺序执行六个高层目标，最后经瓦瑞夫的正常Go East服务到达鲁高因。使用下文独立女巫配方准备全新任务、购买资金和灌注用普通帽子；已有存档跳过已领取奖励的任务，保留尚未完成的实际资格。仅做某项时使用`goals=@(@{action='quest'; quest='search-for-cain'})`；六个名称与完整完成条件见[第一幕任务配方](#第一幕任务配方)。

原有仅交还马勒斯、保留灌注奖励的入口继续保留：

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

区域使用当前MPQ的Levels.Name（可省略“Act 1 - ”一类前缀），单位使用MonStats.Id、物件使用Objects.Name，`superunique`使用SuperUniques.Superunique／hcIdx并核对其Class和实际分配包身份，物品使用原代码，技能使用Skills.skill；匹配不区分ASCII大小写，歧义与未知资源明确拒绝。也接受已存在的数值身份。没有位置提示的目标可给`area='Barracks'`等粗粒度区域；不要求调用方指定路径。这里是结构化目标接口，不解析自由文本自然语言。

| action | 目标及参数 | 实际完成条件 |
| --- | --- | --- |
| travel | 必填area | 目标区域的原生场景已就绪 |
| find | monster／npc／object／superunique四选一；可给area；NPC可interact=true | 真实单位已指派并接近；要求交谈时观察到该NPC对话 |
| hunt | monster或superunique；可给area | 指定真实身份出现死亡模式；已存在的尸体可满足“目标已死”，不声称个人击杀归因 |
| acquire | item；可给area及source物件名 | 对应物品实际归本人所有，包含原任务查询允许的根库存／cursor范围 |
| return-town | 无必填参数 | 本幕真实城镇场景已到达 |
| deliver | quest语义配方；当前只有tools-of-the-trade | 对应物品已被消费，且真实角色任务奖励位已更新 |
| restock | 无必填参数；使用supplies配置 | 正常回到本幕城镇，实际可用红／蓝药数量达到储备目标；显式目标完成后留在城镇 |
| quest | 已登记的第一幕quest名；自动解析区域，不接受area | 完整执行该任务配方并观察原角色奖励位；安达利尔另要求实际抵达第二幕及原跨幕完成位 |

整个计划最多32项目标，quest默认每项1800秒，其余每项600秒，可用goalTimeoutSeconds或各goal.timeoutSeconds设置10–3600秒。没有完成当前目标不会执行后续目标。一次请求先完整解析／校验，再接收计划；append同样先校验整批，不部分插入。

| 命令 | 行为 |
| --- | --- |
| debug-ai-start | 接收scenario或goals；接管独立debug-walk；已有活动计划须先cancel或使用append |
| debug-ai-append | 给现有running／paused计划追加goals；继承原战斗／移动／药剂策略，不接受新的combat／mobility／supplies配置 |
| debug-ai-status | 只读状态、当前goalIndex、完整队列、phase／reason、questProgress内部步骤／待确认操作／灌注阶段、决策及请求计数、实际目标和最近128条事件 |
| debug-ai-pause | 停止本AI输入及技能动作，保留队列／已发请求的观察状态；不暂停宿主 |
| debug-ai-resume | 同一游戏、区域及区域代次内恢复；暂停时间不计入本步期限；暂停中换区须取消并建立新计划 |
| debug-ai-cancel | 终止计划并沿正常客户端Stop停动作；不撤销已经发生的拾取、死亡或交付 |

人工移动／施法点击、技能与装备切换、饮药、ESC、显式独立debug-walk、修改客户端状态的online-*请求、退出或换游戏接管控制。Tab、小地图平移／显示及只读观察不取消计划；人物、包裹、任务等观察面板保留计划并暂停AI输入，关闭后继续。管道ui-input同样按实际意图判断，不因观察帧取消。最小化、表现暂停及游戏菜单阻止AI输入。计划只在内存中保存，不写D2S，不在新游戏恢复旧GUID、目标或请求。死亡停止任务，不添加自动复活。

## 执行分层

`DebugAi`负责队列、目标完成条件、旅行／探索阶段、战斗优先级与有界观察记录；`DebugAutoWalk`负责单次接近。两者共用200毫秒决策间隔，行走过程中也重新判断；均在app主循环运行，和正常输入共用RemoteControl、RemoteCombat、RemoteInventory、RealmSession原操作入口，不直接编码另外一套游戏消息。

元数据在第一次start／append时按当前挂载惰性读取，未启动AI不额外加载这些表。房间、碰撞、单位、库存、技能和任务状态均来自正常客户端副本／重建；不读取GameHost、server-snapshot、服务端隐藏位置或本地存档，不调用管理旅行、生成、伤害、授予技能、回血或解锁。

旅行按本幕已解析的Vis连接及当前实际指派的出口／门户规划区域路线（包含崔斯特瑞姆红门回程），允许使用角色实际已解锁且能缩短路线的传送点；尚未收到0x63历史时先正常打开当前可用传送点。步行连接先到真实开口，再提交普通跨界移动；warp用当前实际指派单位交互。区域实际改变后重算普通步行连接，不继续使用上一层出口；已提交的warp／传送点保留结果等待。公共接近流程会处理附近可接近的已指派关闭门，候选路径末点须通过同一原物件交互几何检查，包含探索、战斗接近、地面物品、NPC、架子、门户和传送点；开门接近保留当前目标直到子导航完成或明确失败，避免移动中在两扇门之间反复改选；门按GUID及指派代次记录待确认操作，等待实际模式变化后才记为已操作；同一指派持续关闭12秒则明确失败，不把accepted当作开门成功，也不盲目重复切换。门操作不能误记成物品来源或传送成功。NPC开窗前同时等待本地路径停稳和正常原包位置进入交互范围；仅开窗超时可正常重开至多两次，已发任务主题／物品请求不重放。服务端继续校验钥匙、任务及碰撞。已在身边的门户／物品／NPC交互先完成，不被远处开门抢占。子导航沿前端已按本地DynamicPath选择的场景绑定，不再无位置重选到服务器稀疏采样所在的另一侧。区域与生成代次更新后重新绑定，不将旧child导航的取消误当成旅行成功。仅登记第一幕瓦瑞夫的正常Go East服务；其他跨幕交通尚无完整编排，不补造未解锁通道。

已知地图是客户端累计探索的完整区域，不受屏幕、小地图窗口、缩放或当前渲染范围裁切。已知地形与本任务已搜索覆盖分开记录；RemoteTown复用原AutomapExploration层及NativeMap，按游戏／区域／地形修订和新增探索更新只读房间／连接索引，并按const引用复用。AI不另建地图生成器，不每次重构地图。每次最多对8个候选房间详细寻路，失败候选随碰撞修订、出发点推进及5秒期限重新评估；已访问房间和观察过的接近点持续保留。

已开始的探索接近保留到子路径完成；已知目标的局部接近也固定本次前沿，完成或明确失败后记录已探查点，只有更远的新前沿或可达真实目标才再次接近，避免未知地形边缘的接近点不断抢占探索。NPC、物件及出口的位置线索只从实际客户端单位分配记住，撤出当前兴趣范围后仍可作为最后已知位置接近；重新收到实际单位才交互或判断完成。已指派但中间地形尚未物化的warp先接近已知可达前沿，再精确交互。新游戏和同幕地图种子改变清相应线索。不读取NPC预设、未见NPC的位置或宿主隐藏状态。

探索只对当前已物化碰撞提交精确移动，未物化房间以可达前沿接近；探索点在进入观察范围后结束，野外观察距离默认16子格，室内依据Levels.IsInside缩短到3子格，不把接近前沿误记为进入未加载房间。门、拾取和实际关卡连接保留必要的精确接近。普通移动带来后续房间消息；正常跨区替换子导航时重新绑定，不把取消当旅行成功。不调用完整地图生成、房间激活或revealAll接口。无可达新房间时等待正常更新并保留原因，最后受目标期限约束。

移动／战斗每0.2秒判断，不要求角色先停下。任务被战斗打断后重新开始必要的接近，不重复已经提交的物品／剧情操作；已提交传送点或warp交互时先等真实区域结果；已提交蓝门创建时也保留施法输入，避免战斗切换技能打断开门。蓝门创建超时且未实际使用门户时只记录未确认结果，不重发开门或假定卷轴已消费，回退正常步行／已解锁传送点；其他超时仍明确失败。死亡、拾取、回城及交付分别使用实际死亡模式、归属、城镇场景和任务位作完成依据；accepted、Updated或acknowledged都不单独表示成功。目标消失不能当作死亡。

## 药剂与自动补给

`supplies`是整条计划的输入策略，默认启用，不需要逐瓶指定GUID或购买步骤。例如：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-start -Arguments @{
    scenario='tools-of-the-trade'
    supplies=@{lifePercent=60; manaPercent=35; criticalLifePercent=25; healthCount=4; manaCount=4}
}
```

| 参数 | 默认 | 行为 |
| --- | --- | --- |
| enabled | true | 自动喝药及补给总开关 |
| lifePercent／manaPercent | 60／35 | 当前生命／法力严格低于该百分比时考虑饮药，范围1–99 |
| criticalLifePercent | 25，最多为lifePercent | 危急生命线，优先本人已有回复药水；允许正常红药追加 |
| intervalMs | 1500 | 两次已确认饮药之间的调试策略间隔，范围600–10000；不是新增玩法冷却 |
| buy | true | 在城镇自动补齐储备；false时仍可喝已有药剂 |
| healthCount／manaCount | 4／4 | 红／蓝药各自的储备瓶数，范围0–16，0表示不购买该类 |
| returnWhenEmpty | true | 野外资源已低且对应药及回复药均用尽时，安全插入回城补给并返回原区域 |

当前生命／法力读取正常原包，最大值借用同一人物HUD／人物面板的已知投影，未知最大值不猜阈值。`debug-ai-status.supplies.vitals`分别显示原包当前值及`displayMaxLife`／`displayMaxMana`，不将显示上限作为宿主权威真值；既有法力上限投影差异见基准档段落。

只选本人腰带或背包里的已解码药剂，直接通过RemoteInventory.Use使用；不从关闭的箱子／方块取物，不替佣兵喝药。分类读取当前Misc、ItemTypes、pSpell、state与States，先区分rpot，再区分继承的hpot／mpot，避免把回复药当普通红／蓝药。已观察到普通恢复状态时暂不叠喝该类，危急生命例外；没有普通药时才以回复药后备。相同类别按当前表的恢复量常量选强瓶，这只是选择提示，不在客户端计算、应用恢复或职业随机倍率。

使用提交后冻结本次物品／请求序号，等待原件实际消费；普通库存Updated不单独代表成功。`lastPotion.consumed`和`recoveryObserved`分别记录消耗及其后12秒内观察到的恢复状态／对应资源增长，不能把瓶子消失直接写成已恢复；观察期结束仍未见结果会保留未确认标记，不把后来无关的资源变化当作该瓶证据。结果未知、被替换或超时不盲目重用；等待期间不继续发子导航输入。正常Pending、Cursor、死亡、门户及物品资格仍由现有客户端入口裁决。

补给按本幕寻找Akara／Lysander／Ormus／Jamella／Malah，身份从当前MonStats编译记录解析，并核对Npc及Misc的原商店供应字段；只通过真实NPC交谈、TradeOpen、实际货架及Buy执行。报价复用RemoteUiClients.itemQuote；价格未知不传零价。按实际资金选择能负担的强瓶，逐瓶等待新0x2A成交结果与本人药剂数量增长，再购买下一瓶；不使用无界Shift补满、不出售装备筹钱、不生成药剂或授金币。缺金币、满背包、拒绝、缺货及未知结果均明确失败，保留现场原因。

自动补给仅在没有待确认的开门、拾取、物品来源、任务交付、选点／跨区或门户操作时插入；保留原目标索引，补给期间单独有600秒上限，完成后补回原目标的剩余执行时间。回程优先使用实际存在、归本人所有且目的地为原区域的蓝门，没有则普通旅行；若当前目标本来就是回城或去本幕城镇，则补给后留在城镇继续该目标，避免无意义往返。回城／补给途中没有可用药剂、且缺蓝或生命危急时优先继续正常脱离路线，避免站在原地反复施放不可用的固定技能；原怪物碰撞及伤害照常生效。已经提交但未确认的原操作不因缺药重新执行。显式`@{action='restock'}`可单独排入队列，回城购买后留在城镇，方便组合新的目标；它要求enabled与buy都开启。暂停保留已提交药剂／购买的观察状态，换游戏取消旧意图。

原依据为D2MOO `SKILLS/SkillItem.cpp::SKILLITEM_pSpell03_Potion`／`SKILLITEM_pSpell05_RejuvPotion`、`ITEMS/ItemMode.cpp`及`UNIT/SUnitProxy.cpp`原NPC／幕与交易准备；正常0x20／0x26使用、0x38开店、0x32购买和0x2A结果复用现有协议入口。原玩法细节见[腰带与消耗品](../gameplay/items/BELT_AND_CONSUMABLES.md)、[NPC交易](../gameplay/npc/TRADE.md)、[商店经济](../gameplay/items/ECONOMY.md)。已有蓝药实际消费／恢复及正常货架购买／回城补给往返记录，阈值与失败组合仍待验收；默认开启购买要求角色实际有金币，基准角色的准备不等于AI可以无偿购买。

## 第一幕任务配方

`scenario='act1'`等价于按下表顺序排入六项`action='quest'`。除保留兼容语义的`scenario='tools-of-the-trade'`外，其他表内quest名也可直接作scenario。每项内部自行处理旅行、门、战斗、真实取物和对话，不要求外部逐个下发动作。

| quest | 自主流程与完成依据 |
| --- | --- |
| den-of-evil | 阿卡拉介绍可选，进入邪恶洞窟搜寻全部实际敌怪；沿已重建房间精细巡查并在探索耗尽后复查，不以屏幕清空判完成。观察真实清场进度后返城领取76，确认原奖励位；本次领取前已知技能点时另等实际加1 |
| sisters-burial-grounds | 卡夏介绍可选，搜寻墓地并击杀真实bloodraven，返城领取92；确认原奖励位及本次领取的正常罗格记录，已有活／死佣兵不强制替换 |
| search-for-cain | 黑暗森林实际Inifuss树取得bks→阿卡拉112翻译bkd→背包正常使用卷轴，等待本次0x50/id4→按该局顺序操作五石→实际红门进入崔斯特瑞姆→打开Gibbet并等实际救出状态／Cain出现→返城阿卡拉118；确认奖励位与本次新增戒指归属，必要时正常拾取、放回背包 |
| forgotten-tower | 石块旷野古书可选，存在时正常操作并确认实际提供的127；进入塔底搜寻并击杀SuperUnique“The Countess”，不把同类普通怪物算作女伯爵；返城读取实际完成位，所提供的143仅走正常对话 |
| tools-of-the-trade | 搜寻并击杀SuperUnique“The Smith”，操作真实Malus架子取hdm→返城恰西163→用预留的背包普通无孔cap走Take／Cursor／QuestService；确认0x58服务结果、实际稀有帽子与消耗后的奖励位，再正常放回背包 |
| sisters-to-the-slaughter | 凯恩介绍可选，进入地下墓穴第四层搜寻并击杀真实andariel→返城瓦瑞夫183→实际提供的Go East服务→鲁高因原生场景；同时确认原任务奖励与独立的第一幕跨幕记录 |

依据为本地D2MOO `QUESTS/ACT1/A1Q1.cpp`至`A1Q6.cpp`的ScrollMessage、自然死亡及ObjMode回调，灌注复用原`UNIT/SUnitNpc.cpp`的Charsi服务。该服务调用`ACT1Q3_SetRewardGranted`而不立即`UpdatePlayerFlags`：AI先确认0x58成功与真实稀有产物，放好物品后才正常重开NPC读取完整位，不再次灌注。树／石柱／吊笼／马勒斯操作分别核对当前Objects.OperateFn 12／9／10／21，古书为6；区域、怪物、SuperUnique、物品和技能从当前MPQ解析。正常协议使用现有0x3E读卷轴、0x50/id4线索、0x31实际主题确认、0x58服务结果及0x38交通入口；不从地图种子推石柱顺序，不把0x5D进度值当作完整人物任务位。任务玩法与版本证据仍归[第一幕任务](../gameplay/quests/ACT1.md)。

已确认的可选介绍在旅行前直接结束；A1Q1／A1Q2介绍正常结束交谈后才等待NPCDEACTIVATE触发的状态回包。待领取位允许略过已完成的野外目标；已领取任务直接跳过。击杀步骤可由实际尸体结束，随后必须通过真实NPC／最终任务位核实个人资格，不从尸体推导个人归功。原版部分完整任务位延后至交谈更新：仅在实际奖励效果或对应完成状态已到达后，正常关闭并重开NPC查询，不重发已提交的主题。瓦瑞夫183按原版即时授奖，AI等待实际奖励位，再办理正常Go East；槽7独立确认旅行，不能用pending状态代替领奖。

任务物品在私人箱等不可访问位置时不会假装拿出，卷轴须在背包。背包放不下奖励、缺预留普通帽子、原资格不可用、凯恩已由罗格救出的旧档等均明确说明原因。普通帽子仅从背包normal／无孔cap选择，不碰已装备或稀有／暗金物品；本次会消费灌注奖励，保留奖励时仍用旧入口。阿卡拉的重置属性技能服务不属于任务完成条件，AI不消费。原版允许直接推进的介绍／古书在未提供时可跳过。

## 其他任务与五幕首领的覆盖边界

第一幕六任务已登记上述源码配方，尚未整链运行验收。`find`／`hunt`／`acquire`仍可组合其他目标：给实际MPQ身份及必要的粗粒度区域后，复用已知地图、探索、门、移动和战斗；不会从任意任务名称推导未登记的完整剧情。位置提示限已登记的第一幕目标及实际客户端线索；仅给其他目标名字而不提供区域，并不意味着会自动遍历五幕。

| 目标 | 当前AI范围与缺口 |
| --- | --- |
| A1 Andariel | 已登记完整sisters-to-the-slaughter源码配方，含正常Go East；尚未逐项运行验收 |
| A2 Duriel | 没有合杖、真实墓室识别／插杖开墙的完整前置编排；开放巢穴现场可用通用hunt |
| A3 Mephisto | 没有克林姆部件／合成／议会／宝珠解锁的完整编排；开放憎恨囚牢现场可用通用hunt |
| A4 Diablo | 没有封印、封印精英及等待Diablo自然出生的专门目标流程；已出生现场可用通用hunt |
| A5 Baal | 没有五波、王座退场及入殿的完整配方；Worldstone Chamber中的真实Baal可用通用hunt，不把王座NPC／分身当作最终目标 |

这些现场组合尚未逐幕验收；通用死亡模式条件也不等于首领任务位完成或个人击杀归因。其他幕任务配方／交通编排、自动处理全免疫／多技能战术仍未覆盖，不能宣称已实现“从新档自动杀完A1–A5”。

## 战斗策略

未指定combat.skill时，按当前实际掌握的主动技能依次选择Frozen Orb、Fire Ball、Zeal、Attack；女巫因此优先冰封球，也可显式固定其他技能。普通攻击配置沿右手选择与施放入口执行；战斗光环存在时，右手选择光环、左手使用攻击技能，攻击技能须支持左手。结束时保留最后选中的技能，不自动加点、装备或替换技能规则。指定技能不可用时保留正常客户端拒绝原因，最终受本步期限约束；不赋予法力、忽略免疫或伪造命中。目标被墙隔开时先接近／开门，不持续对墙施放。

探索／赶路默认只处理贴身威胁，不清除所有看见的怪物；接近指定hunt目标后，才考虑为击杀处理其附近敌人。优先级为近处拥挤后撤、与当前目标有关的战斗、当前高层目标。当前策略参数均是调试输入策略，不是原版玩法数值：

| combat参数 | 默认 | 用途 |
| --- | --- | --- |
| enabled | true | 是否在任务期间自动战斗；hunt／quest要求开启 |
| skill | 按已学技能选择 | Frozen Orb优先；MPQ技能名／ID仍须当前角色可用 |
| scope | defensive | defensive处理近身威胁，clear处理engageRadius，objective赶路不主动接敌；任务必杀目标附近接敌及洞窟清场独立保留 |
| aura | 自动 | 优先已掌握的Fanaticism／Concentration／Might；也可显式指定已学会的原光环 |
| engageRadius | 18 | 处理当前已知敌对单位的范围 |
| range | 16 | 远程接近距离；Attack及MPQ h2h技能使用近战接近距离3 |
| crowdRadius／crowdCount | 6／4 | 范围内达到指定敌人数才后撤 |
| retreatDistance | 8 | 在已知可达地形选择更安全的位置 |

非任务必杀的沿路目标连续6秒未观察到生命百分比进展时，暂避该次指派10秒并继续原目标；不将其记为死亡，也不跳过洞窟清场或hunt必杀目标。

后撤从16个方向的已知可达点选择距当前敌群更安全的位置，最多保持2秒，至少4秒间隔后才再次发起后撤，保留攻击窗口；无安全路线时继续按正常技能资格处理。敌对资格复用RemoteCombat，不攻击中立NPC、自己或盟友的宠物。固定技能最多每600毫秒尝试一次独立施放，正常技能／动画／资源与请求限流仍生效。

## 职业移动策略

默认`mobility.mode='auto'`按当前已学技能选择：女巫优先Teleport；有Charge的圣骑士使用左手Charge，并在右手选择已学Vigor；只有Vigor时保持普通行走。配置`mode='walk'`关闭传送／冲锋，`vigor=false`可同时关闭移动光环。也可选`teleport`或`charge`，技能未知、城镇禁用、落点不合适或正常请求不允许时仍回退步行，不授予技能和法力。`distance`默认24、范围8–30，为单次调试输入距离。

传送方向沿当前已知步行路径的后续进度选择，不能只按到终点的直线距离判断；落点须能继续连接选定路径，避免反复传进弯墙凹角再步行退出。同一步对同一目标／落点最多尝试两次。原生寻位返回整数格，检查、碰撞余量与实际发送均使用同一个整数落点，不对其减半格后再次向下取整。前进及侧向候选比较2／4／6子格碰撞余量，优先留出内部观察距离，不反复贴地图边界。允许跨过墙壁，但不生成隐藏地图；读取当前Levels.Teleport并保留原服务端规则裁决。提交后等待本人原0x15位置不连续通知，再按真实路径位置继续搜索，不把普通行走进度算作传送完成。连续传送不插入人为等待期让旧行走补齐墙前坐标。冲锋只选普通碰撞及原整数射线都畅通的方向，等待实际移动进度。同一子目标连续8秒没有至少2子格接近进展时，停止传送／冲锋15秒，让普通路径继续绕障；未提交交互的子路径同时重新绑定进展时钟，不能让频繁换落点无限占住输入。该策略不改变寻路、碰撞或技能规则。两种施放期间暂停子行走输入；选择技能等待确认，连续请求受正常Pending门控与节流；无位置进度则等待3秒并暂退普通行走。短距离的门／NPC／拾取仍正常接近。

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

第一幕整链另有[debug-act1.json](fixtures/debug-act1.json)，独立姓名`ActOneSorceress`，同一批原装备和全新任务；490属性点分配为70／50／70／300，98技能点用于冰弹／冰封球／冰冷支配／传送各20、暖气12及合法前置／静态力场，初始左手冰风暴、右手传送；实际面板539生命／1682法力。钱包与私人箱各准备100000金币，背包另带一顶普通无孔cap供恰西灌注。金币为离线调试准备，遵守等级钱包与原私人箱上限；运行中的AI仍只能实际付费购买。这份配方及资源工具的可选`gold`／`bankGold`字段已通过Windows Release构建、原v96编码／重读及独立入局；实际基准在忽略目录artifacts/debug-ai-act1-20261029/baseline-cold，旧DebugSorceress基准及摘要不变。

用已更新的`d2x_assets`生成到一个**尚不存在**的独立忽略目录，再保留只读基准并复制工作目录：

```powershell
.\build\bin\d2x_assets.exe assets/mpq2 save-recipes docs/development/fixtures/debug-act1.json artifacts/debug-ai-characters-20261029/baseline-act1
```

重复任务时先正常退出并关闭使用工作目录的进程，再把基准复制到一个新的独立角色目录，清除该副本的只读属性，以该目录启动新游戏。新游戏同时重建本局怪物、架子／掉落及任务运行态；不只清角色任务位后继续使用已完成任务的旧游戏。这样保留固定装备、技能及地图种子，同时回到全新任务资格。需要其他幕的前置状态时另建合法基准，不向运行中的客户端增加改任务入口；当前只准备全新正常难度基准。

## 交易的工具配方

当前MPQ中，MonStats的smith、charsi分别对应402、154，Barracks为28，Objects.Malus为108／OperateFn21，马勒斯代码为hdm；实现通过名称／表记录动态解析，不把这些数值写成调试目标坐标或生成参数。

依据本地D2MOO `QUESTS/ACT1/A1Q3.cpp::OBJECTS_OperateFunction21_HoradrimMalus`：操作真实架子按当前人物资格创建hdm；击杀铁匠不是其掉落来源。配方按用户目标额外先满足铁匠已死，再操作架子／拾取；已拥有hdm可跳过重复取得。原版至少8级、本局及个人任务资格仍由实际任务链裁决，AI不修改资格。

同文件的ScrollMessage 163在实际持有hdm时删除物品并设置PRIMARYGOALDONE／REWARDPENDING。交付器仅确认恰西实际提供的自动文本及该配方主题，不向未提供的消息编号发伪造确认；原任务槽来自公共questDefinition。原查询和消费范围见[第一幕任务](../gameplay/quests/ACT1.md#原规则证据)。旧`scenario='tools-of-the-trade'`／`deliver`完成表示马勒斯已交回、灌注资格可用，不消耗灌注；新增`action='quest'; quest='tools-of-the-trade'`与`scenario='act1'`继续完成预留普通帽子的正常灌注。

## 实现入口与待验收范围

[ai.cpp](../../src/app/debug/ai.cpp)负责解析／队列／绑定，[ai_navigation.cpp](../../src/app/debug/ai_navigation.cpp)负责旅行／探索／门，[ai_actions.cpp](../../src/app/debug/ai_actions.cpp)负责战斗／物品／回城／交付，[ai_mobility.cpp](../../src/app/debug/ai_mobility.cpp)负责职业移动／技能确认，[ai_supplies.cpp](../../src/app/debug/ai_supplies.cpp)负责药剂／真实购买／补给往返，[ai_quests.cpp](../../src/app/debug/ai_quests.cpp)登记第一幕配方／步骤及清场，[ai_quest_actions.cpp](../../src/app/debug/ai_quest_actions.cpp)处理正常任务交互／奖励／灌注／交通。[RemoteTown](../../src/client/remote_town.hpp)只读暴露已经重建的房间和连接；[frontend](../../src/app/frontend.cpp)持有唯一输入所有权及调度，并只读提供原UI资源上限与购买报价。

2026-10-30，Windows／MinGW GCC13.2、Release，`cmake --build build --config Release --target all --parallel 3`完成全部目标编译／链接及修正后复编，成功日志无编译／链接警告或错误。实机EXE SHA-256为`EE32A5108305A3C282265E75B47C898EC6C757D7F3BFD32D1A435198DE7D7353`。未执行Linux／Debug构建或PD2运行，没有新增测试脚本、用例或专用程序。

最终整链证据为忽略目录`artifacts/debug-ai-act1-20261029/run-24/`：原版assets/mpq2、正常难度、seed557488535、独立ActOneSorceress全新任务副本，一条scenario=act1计划，固定Frozen Orb、Teleport及默认defensive策略。过程中没有管理旅行、生成、伤害、授金或修改任务位；AI连续完成六任务并正常Go East到鲁高因原生场景40，state=completed、goalIndex=6。基准`baseline-cold`的D2S SHA-256为`6D3611BD1C5510D0D50270549588B4F5EB6DA07BADF33BAD0BACE33CABF68B98`，各轮EXE摘要、原包和快照独立保留，早期失败不计作通过。

- 洞窟自然计数清零、0x89效果通知、正常Akara76领取实际增加1技能点；血乌自然击杀、Kashya92领取罗格Aliza；凯恩取卷、112译卷、使用原卷取得本局0x50/id4顺序、五石／红门／牢笼、返城118领取新魔法戒指。本轮戒指ID355、quality4，区别于原有两枚暗金戒指。
- 女伯爵自然击杀并正常蓝门返城；军营实际Smith死亡、操作Malus架子、取得hdm、正常返城并确认163交付，预留普通帽子经一次正常Take／QuestService变为新稀有cap ID961、quality6并在背包。随后自主经地牢／暗门／地下墓穴击杀安达利尔、返城183领奖，再走原Go East。最终原槽1至6为0x2001，独立跨幕槽7为1；hdm已消费，未用接受／排队状态代替结果。
- 正常保存后以同一EXE新进程run-25重入，仍在鲁高因，原槽1至7的奖励／跨幕位均保留，技能点1、魔法戒指、稀有帽子和Aliza等级8记录保留。关闭补给后重新提交已完成的六任务计划，六项均跳过，castRequests／interactions／navigationGoals为0，无重复领奖或灌注。原资源工具正常重读最终D2S；另有独立run-17／18确认“183已领奖、尚未旅行”的原v96状态为PassageReady(4)、槽7为0，重入后可正常Go East，不能把这组较早记录合并成最终整链。
- 本轮正常NPC货架购买8瓶药，未触发饮药。独立run-06实际消费4瓶蓝药、观察恢复，并完成缺药补给返回原目标；run-14实际消费hp1并观察生命恢复。消耗和恢复分开记录，其他阈值、拒绝与时序组合不由这些记录代替。

回归修正包括普通移动空路径接受同目标新输入重算；停止状态同目标输入至多每秒重试、方向仍每200毫秒判断及15秒无进展上限；已知目标的局部前沿不抢占探索、访问不借用其他子路径的完成；稳定目标8秒无接近进展时传送暂退15秒普通行走；实际区域变化后重算普通出口，并保留已受理warp／传送点的结果等待。开门候选使用原交互几何；NPC开窗等待停稳及正常位置回包，开窗超时最多正常重开两次，任务主题／物品请求不重放。物件保留原本地到达后0x13及宿主接近流程，不套用NPC等待条件。

原版规则修正归负责模块：OperateFn18暗门的一次开启、立即释放碰撞与静默ENDANIM；门户共用原物件矩形／交互射线，已链接OperateFn15门户在OPERATING即可使用，不加OPENED资格；Warriv183立即授予奖励，幕旅行及原v96槽7独立记录。依据与边界见[物件](../gameplay/world/OBJECTS.md#碰撞与靠近)、[第一幕任务](../gameplay/quests/ACT1.md)、[存档](../modules/SAVES.md)。run-23原包保留OPERATING首次点击被旧门槛拒绝、OPENED后同位置正常交互成功的诊断，不能当作未介入的整链通过。

仍待验收：更多种子／难度及其他scope的整链；不同石柱顺序、已有亮柱／Cain延后出生、同类普通怪负向组合；满包、私人箱任务物品、缺灌注材料、更多已完成／pending资格；延迟完整奖励位时的NPC查询、拒绝／未知结果、暂停／人工接管／断线及不重复消费。单体Ice Blast及地图边界附近目标曾持续无伤害，未定位为单一原因，不能认证所有冷弹体命中；此前鲜血荒地反向普通步行入城超时仍待回归。宿主直接远距门户0x13的自动接近尚未补齐；圣骑士Vigor／Charge／战斗光环、无技能／法力／死亡、更多补给组合和全免疫战术仍保留边界。缺原WAV资源的声音及面板／宿主法力上限差异未关闭，不能将本次正常难度固定技能的完成扩展为全部规则／音画与原版一致。
