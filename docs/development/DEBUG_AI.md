# 客户端调试任务AI

调试AI将高层目标队列转为正常客户端操作，属于app/debug，默认关闭，仅在图形客户端运行。当前支持零售1.13c的五幕27项任务、旅行、找人／物、战斗、取物和补给；不将这些配方套用到PD2，不执行headless任务AI，也不构造另一套玩法或存档规则。

## 阅读入口

| 要做什么 | 唯一负责页面 |
| --- | --- |
| 启动、隔离角色、续跑及退出 | [构建运行](BUILD_AND_RUN.md#诊断启动与任务-ai) |
| 下发目标、修改策略、了解当前缺口 | 本页 |
| 理解五幕编排与部分进度恢复 | [任务配方](DEBUG_AI_QUESTS.md) |
| 修改调度、输入占用、战斗或补给 | [执行契约](DEBUG_AI_RUNTIME.md) |
| 修改知识、探索、寻路及旅行 | [导航结构](../architecture/CLIENT_NAVIGATION_REDESIGN.md) |
| 读取状态、日志、碰撞及后台耗时 | [AI诊断](DEBUG_AI_DIAGNOSTICS.md)；传输和通用命令归[调试管道](DEBUG_PIPE.md) |
| 选择验证方法或优化候选 | [测试指南](TESTING.md)、[优化候选](DEBUG_AI_ANALYSIS.md) |

AI只读客户端观察，通过与手动输入共用的功能接口行动；不能读取Host隐藏状态、改副本、发包或模拟键鼠。权威任务规则归[任务系统](../gameplay/quests/SYSTEM.md)，工具成功不认证全部原版行为。下述状态描述当前源码，未验收组合集中在文末，不维护构建及运行流水。

## 输入与组合

### 游戏内聊天入口

正常入局后按Enter输入，无需调试管道。以下start是互斥示例，活动计划不能直接替换：

```text
/debug-ai-start
/debug-ai-start act1-5
/debug-ai-start act2-4
/debug-ai-start act1 quest3
/debug-ai-start quest search-for-cain
/debug-ai-start campaign
/debug-ai-status
/debug-ai-pause
/debug-ai-resume
/debug-ai-cancel
```

`act1`至`act5`及升序范围筛选配置中的quest目标，保留配置顺序；`actN questN`必须为单幕，编号取任务面板从1开始的顺序，不是MPQ内部编号。例如第一幕3为凯恩、5为马勒斯，第三幕1为金鸟、4为古书，第四幕2为熔炉、3为Diablo。所选任务不在文件中则拒绝。

其他scenario名称读取`debug-ai-<名称>.json`，名称限ASCII字母、数字、下划线和连字符；任务名不是scenario，单任务用`quest <名称>`或显式goals。命令与选择器不区分ASCII大小写。聊天可附JSON对象，不带command；粘贴换行／制表符转空格，仍受原生编码后255字节限制，更长队列用管道。

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-start -Arguments @{
    configFile = 'config/debug-ai.json'
    scenario = 'act1'
    questNumber = 5
    combat = @{skill='Frozen Orb'}
    mobility = @{mode='teleport'}
}
```

复杂目标也可组合：

```json
{
  "goals": [
    {"action":"hunt","monster":"smith"},
    {"action":"acquire","item":"hdm"},
    {"action":"return-town"},
    {"action":"deliver","quest":"tools-of-the-trade"}
  ],
  "goalTimeoutSeconds": 600
}
```

这是结构化接口，不解析自由文本。区域取当前Levels.Name（可省幕前缀），单位取MonStats.Id、物件取Objects.Name、精英取SuperUniques名称／hcIdx并核对Class及真实身份，物品取原代码，技能取Skills.skill；接受现有数值身份，未知／歧义拒绝。未登记位置来源的目标应给area，不承诺自动遍历五幕。

| action | 参数 | 完成依据 |
| --- | --- | --- |
| travel | 必填area | 目的区域原生场景就绪 |
| find | monster／npc／object／superunique四选一，可给area；NPC可interact=true | 真实指派并接近；要求交谈时等实际对话 |
| hunt | monster或superunique，可给area | 精确身份死亡模式；尸体可满足已死，不证明个人击杀 |
| acquire | item，可给area及source物件 | 物品实际归本人，按正常任务查询的根库存／Cursor范围 |
| return-town | 无 | 本幕真实城镇场景 |
| deliver | quest，当前仅tools-of-the-trade | 材料消费及个人奖励位 |
| restock | 使用supplies | 留在本幕城镇，实际储备达标；要求enabled和buy |
| quest | 已登记quest名，不接受area | 配方规定的私人奖励／物品／世界结果；幕末另核对实际跨幕或终局 |

最多32项目标，逐项执行。代码回退预算：quest每项1800秒，其他600秒；`goalTimeoutSeconds`及各goal.timeoutSeconds允许10–3600秒，默认文件为3600秒。start／append均整批校验，不部分接收。

| 命令 | 行为 |
| --- | --- |
| debug-ai-start | 读取配置并启动；接管独立debug-walk，已有AI计划先cancel或append |
| debug-ai-append | 仅给running／paused计划追加目标，支持相同选择器；继承现行策略，拒绝显式策略覆盖 |
| debug-ai-status | 只读完整状态，受理不等于完成；字段见[诊断](DEBUG_AI_DIAGNOSTICS.md#状态与事件) |
| debug-ai-pause | 停止AI输入及技能动作，保留队列与未决结果观察，不暂停Host |
| debug-ai-resume | 同游戏、区域及区域代次恢复；暂停中换区须取消重建计划 |
| debug-ai-cancel | 终止计划并正常Stop，不撤销已发生的消费、死亡或交付 |
| debug-ai-reload-config | 原子更新允许的策略数值，见下节 |
| debug-ai-terrain-export | 只读导出实际观察记忆，见[地形导出](DEBUG_AI_DIAGNOSTICS.md#地形导出) |

默认保留灌注、镶孔和个性化奖励。只有start或单个quest的`useServiceRewards:true`才消费服务；不可热重载，材料和确认条件见[任务恢复](DEBUG_AI_QUESTS.md#条件图与恢复)。

`activateAllWaypoints:true`默认关闭、仅启动时设置。按MPQ Levels标记有点区域，离开当前区域前须确认本区点已解锁；完整计划把未途经支线点安排在各幕末任务前，已确认点跳过。短计划仍要求全部点，不授予通行资格。

### 输入接管

主游戏画面的Esc解除AI控制；面板／聊天／对话／菜单内Esc只处理当前界面。其他人工移动、技能、饮药和online-*命令正常执行但不自动取消AI，下一轮可能被AI策略覆盖；需要持续手动控制时先pause或cancel。独立debug-walk保留自己的人工接管规则。

观察面板、聊天、最小化及表现暂停阻止新AI输入，计划保留，恢复后继续；已提交结果仍观察。任务结束、失败、死亡、离局或游戏代次改变收尾，计划不写D2S。聊天错误本地反馈、不广播，摘要带[Debug]且最多64条；完整状态用管道。

## 文件配置与运行中重载

[config/debug-ai.json](../../config/debug-ai.json)是唯一随源码默认配置。无configFile时读取`debug-ai.json`；绝对路径直接用，相对路径先查EXE旁，不存在才查工作目录。找到但解析失败即报错，不换读同名文件。限64KiB UTF-8 JSON对象，不允许command或嵌套configFile。

goals显式替换文件队列，幕选择器只筛quest；无goals的文件不能按幕启动，非quest只在未按幕筛选的完整计划执行。策略依次取文件公共项、文件当前职业组、命令公共项、命令职业组；命令递归覆盖文件。职业未知拒绝byClass，缺当前组则用公共项。

各表“回退”指字段未配置时的代码值，不是实际启动值。当前默认文件例如红／蓝各8、移动距离30、战斗range18、女巫Frozen Orb独立range12；实际值以status为准。

```powershell
.\scripts\Send-D2XCommand.ps1 -Command debug-ai-reload-config
```

重载沿最近成功文件，也可显式指定configFile或允许的数值覆盖；文件不自动轮询。解析失败不部分应用。成功绝对路径记在configuration.file，下一正常决策生效，既有请求及其期限不重建。

| 可重载 | 必须重新start |
| --- | --- |
| combat数值及rangedKite、mobility.distance、supplies阈值／数量、events策略 | goals、预算、职业技能／门槛／独立range、scope等模式、移动模式、补给开关、服务奖励、全点策略、searchKnowledge、navigationExperience |

重载文件中不可重载项被忽略；命令显式请求修改这些项则拒绝。append继承当前策略，不接受combat／mobility／supplies／events／navigationExperience覆盖。

## 战斗策略

`combat.byClass`支持amazon、sorceress、necromancer、paladin、barbarian、druid、assassin，各组可配skill、fallbackSkills、groupSkill、aura。技能接受名称、ID或对象：

```json
{"skill":"Frozen Orb","minLevel":5,"range":12}
```

minLevel默认1、范围1–255，比较普通客户端有效等级；先天Attack按至少1级。range可选2–30子格，覆盖全局远程距离，近战仍3格，静电场用原技能半径。未知、被动、攻击列表中的光环及冲突门槛拒绝；Attack仅显式列出才作配置备用。七职业配置不代表召唤、陷阱、蓄气或全通关已验证。

| combat参数 | 代码回退 | 含义 |
| --- | --- | --- |
| enabled | true | 自动战斗，hunt／quest要求开启 |
| skill／fallbackSkills | 按已学技能／空 | 主攻及最多8项备用；出城资格选择和无进展恢复 |
| groupSkill | 无 | 可选群攻，使用实际可用技能与受限目标 |
| aura | 自动 | 已学Fanaticism／Concentration／Might或显式原光环 |
| scope | defensive | defensive处理实际近身压力；clear处理engageRadius；objective不主动沿途接敌，必要目标独立 |
| staticField／avoidHazards | true／true | 冷却空档静电场／已观察危险避让，不修改技能或伤害规则 |
| engageRadius／range | 18／16 | 接敌／默认远程距离，技能range可覆盖后者 |
| noProgressSeconds | 6 | 3–30秒，实际攻击后的效果窗口 |
| stallTimeoutSeconds | 90 | 15–300秒且不少于两个窗口，局部无进展预算 |
| retreatLifePercent／lifeDropPercent | 45／10 | 范围5–90／1–80，生命阈值与最近4秒掉血比例 |
| crowdRadius／crowdCount | 6／4 | 群体压力判断，不是唯一后撤条件 |
| waitDefenseRadius | 10 | 1–30子格，停留远程防御仍受射程与射线限制 |
| retreatDistance | 8 | 已知地面上的撤退距离 |

风筝可配rangedKite、kiteIntervalMs、kiteDistance、kiteMinDistance、hazardLookaheadMs、hazardPadding，默认文件维护具体值。选敌、清障、途经攻击、效果观察和危险预测只维护在[执行契约](DEBUG_AI_RUNTIME.md#战斗与路段交接)，不在配置页重复算法。

## 职业移动策略

| mobility参数 | 代码回退 | 含义 |
| --- | --- | --- |
| mode | auto | auto按已学能力选女巫Teleport、圣骑士Charge／Vigor；walk禁用传送／冲锋，也可显式teleport或charge |
| distance | 24 | 8–50子格的单次输入距离，默认文件30；可数值重载 |
| vigor | true | 允许已学移动光环；walk需另设false才同时禁用它 |

teleport表示赶路优先传送，不是禁止步行。短接近、城镇、技能不可用或无合法落点仍可朝同目标步行；必要击杀、清路和避险保留。传送／冲锋都走正常技能入口，实际位移确认才推进。地图资格、坐标、落点修正和续接见[导航结构](../architecture/CLIENT_NAVIGATION_REDESIGN.md#路线与缓存)。

## 药剂与自动补给

| supplies参数 | 代码回退 | 含义 |
| --- | --- | --- |
| enabled／buy | true／true | 自动饮药及补给总开关／采购开关；buy=false仍可用已有药 |
| lifePercent／manaPercent | 60／35 | 严格低于阈值才考虑饮药，1–99 |
| criticalLifePercent | 25 | 不超过lifePercent，危急时优先已有回复药并允许普通红药追加 |
| intervalMs | 1500 | 600–10000ms，已确认饮药间策略间隔，不是玩法冷却 |
| healthCount／manaCount | 4／4 | 各0–16，0不采购该类；默认文件各8 |
| quiverCount | 3 | 0–6，包含已装备与背包，0禁购弹药 |
| returnWhenEmpty | true | 资源低且药用尽或实际战斗压力时可插入补给 |
| fillPortalBook | true | 无书或页数不超过一半时补货；其他短缺已进店或显式restock按实际容量补满 |

只使用本人真实物品和资金，不自动从关闭容器取药、不替佣兵喝药。补给保留父目标，城内完成后重新判断任务目的地，普通目标沿统一旅行返回；显式restock留城。治疗、Shift采购、弹药替换、未知资源和结果确认见[补给执行](DEBUG_AI_RUNTIME.md#药剂与补给执行)。

## 事件参数

下列events支持数值重载。已开始的事件冻结等待预算／初等值，不因重载续期；菜单显示时间的例外见表。

| 字段 | 回退／范围 | 含义 |
| --- | --- | --- |
| waitSeconds | 90／15–300秒 | 通用事件上限，配方resultWaitSeconds可覆盖 |
| initialWaitMs／doorInitialWaitMs／scrollInitialWaitMs | 1000／250／1000ms | 通行／普通门／开蓝门初等，之后按共用通行复试 |
| encounterRadius | 32／12–64 | 现场范围，原生子格 |
| standoffMin／standoffMax | 8／4–20；24／8–40 | 观察环带，min < max且max+3 ≤ encounterRadius |
| waypointCloseDelayMs | 300／0–30000ms | 实际菜单最短显示；重载按原打开时刻立即用新值，暂停不输入，主动旅行及结束清理不受限 |
| npcDialogueCloseDelayMs | 500／0–30000ms | 实际NPC对白最短显示，新自动台词重计；期间不确认／关窗／离开NPC |
| questLogDurationMs | 2000／0–30000ms | 完成后正常Q预览，0禁用；改0取消待开并关闭AI预览，其他修改只影响下次；不阻塞移动战斗，玩家接管后不自动关 |
| walkDecisionMs／walkReactionMs | 160／40，各1–5000ms | 普通行走定时决策／段尾及停步复核，不改变战斗周期 |
| walkRequestIntervalMs | 200／0–5000ms | 提交后替换路段的最小间隔，不是到达后等待 |
| walkRetryMs | 1000／0–30000ms | 相同位置与目标重复移动请求间隔 |

菜单延时0取消额外等待；请求／重试间隔0只取消AI门槛，不取消客户端节流或无进展期限。状态变化与后台结果可提前唤醒，重载保留提交时刻和当前路段；独立debug-walk使用默认或最近AI设置的行走策略。

## 目标搜索知识

`searchKnowledge`按条件图节点名覆盖任务知识，普通find／hunt／acquire／deliver可直接带search。start／append准备目标，重载不改搜索知识；名称／ID准备时解析，运行不查隐藏布局。

| 字段 | 含义与约束 |
| --- | --- |
| anchor | object／npc／monster／superunique选择器；正常找到实际锚点再解释范围 |
| offset | 相对子格[x,y]，各-1024…1024 |
| radius | 1…1024子格；普通目标省略为本区域，任务门户有事件范围 |
| direction | 非零[-1…1,-1…1]，优先半平面但无路仍核查其余 |
| candidateRooms | 1…16个互异房间偏移，各轴-32…32；须anchor，按实际锚点房间原点／宽高换算，不与半径、偏移、方向、延迟出生混用 |
| returnViaWaypoint | 仅candidateRooms可用，换候选时比较正常回城／已解锁点与直达成本 |
| openedObjects | 最多16个机关，全须实际OPENED，以最后新触发起算 |
| waitMs | 0…120000ms初等，早到目标立即接管，不代表生成完成 |
| timeoutSeconds | 1…3600秒总预算，含初等，重规划不续期 |

封印首领内置知识以实际Operate54／55／56为锚，已核相对方向(-12,-52)／(-39,+33)／(+32,+16)，初等5秒、半径128、预算180秒；Diablo以实际Init55为锚，16秒／32格／90秒。这些是可覆盖策略，不保证目标在范围内，不冒充原出生时长。依据D2MOO A4Q2，原规则见[第四幕任务](../gameplay/quests/ACT3_5.md)。庇护所候选及王座范围见[任务配方](DEBUG_AI_QUESTS.md)，生命周期见[目标核查](../architecture/CLIENT_NAVIGATION_REDESIGN.md#目标核查)。

## 地图经验配置

`navigationExperience`在start加载，enabled=false关闭，未配区域中性；append继承，热重载不替换。经验只影响方向与地标，不能提供碰撞、活单位、任务或解锁事实。实际观察优先，未知城镇变体不用经验；configuration.navigationExperienceAreas显示区域数。

| areas条目 | 含义 |
| --- | --- |
| area | 原区域ID或名称，同区域仅一项 |
| bounds:[x,y,width,height] | 已核固定世界子格范围，仅降低越界探索优先级，不裁路线；实际同区域地形超出即停用 |
| travelDirections:[{to,direction:[x,y]}] | 各分量-1／0／1、非全零，世界坐标方向；只用于Connection，独立有界评分，不生成虚拟目标 |
| landmarks:[{npc或object或connection,positions,townVariant?}] | 普通位置为世界子格；带变体时为全显城镇原点偏移。NPC必须带变体，多个位置保留当前有效接近，再比距离 |

默认配置有15区域、五幕城镇62服务站点与9传送点变体记录，另含宫殿50–54、第五幕110／111／112／117方向及131入口。坐标只维护在配置，不在本文复制；来源为当前MPQ Levels／LvlMaze／LvlPrest及城镇／宫殿DS1，按D2MOO DRLGMAZE_InitBasicMazeLayout和DRLGOUTSIEGE_PlaceBarricadeEntrancesAndExits／PlaceCaves核对。51–54的100×100为预留范围，四块实际组成32×32原生瓦片，不将回程入口变体写死。

城镇点身份经ObjPreset及OperateFn23核对；Objects.AutoMap=0不代表没有地形图元，当前独立传送点cel307可来自Automap.txt或城镇底图。配置只在实际坐标／自然图元缺失时后备。父AI通过DebugWalkIntent.searchHint交给子导航，换线索释放旧承诺，最终等真实指派及菜单／激活；独立debug-walk可成对传searchHintX/Y。经验模块不扫描本局隐藏预设，knownTownFrame只公开已全显城镇原点、大小和变体。

冰冷之原入口点偏好依据DRLGOUTDOORS_SpawnAct12Waypoint的入口附近8瓦片块及随机后备，不是固定位置。实际步行跨入时最多观察入口及两侧3处／20秒、到点等250ms；已指派优先，已开跳过，远处门户进入不返入口。第五幕沿边方向不套第四幕诅咒之城内部楼梯；后者按DrlgOutdoors的MESA_WARP内部放置继续搜索。

## 已知运行问题

只保留未解决现象和修复未闭合的原条件，已验证修复不留历史。“已调整”仅描述源码；不同路线成功不等于原时序复现。归因到公共客户端／宿主后转负责专题，不能把工具策略当原版缺陷。

| 问题 | 当前处理与剩余核对 |
| --- | --- |
| 城镇首次读取传送点绕行 | 已接地形图元及变体searchHint；待实机确认无指派／无独立图元时能定向接近，不推算节省耗时 |
| 满距离传送中继拒绝 | 已统一整数输入域；庇护所脚格(25508,5220)到(25508,5190)的30格首跳，显示脚点约(25507.795,5220.377)会被旧小数距离误拒，原组合待重现 |
| 延迟首领／入口搜索耗尽 | 已按真实触发建立核查轮次并仅重访退订范围；待核对开印后的重新订阅、真实指派与截止 |
| 转化回执身份误拒 | 已绑定方块及实际受理命令；限流拒绝后首次成功转化的回执组合待复测，禁止重发转化 |
| 任务物件接近被战斗位移抢占 | 已保护0x13至首次效果；原紧邻传送时序待复现，不重复操作 |
| 单位搜索过早结束／到点空等 | 已区分07订阅、实际进入核查及本地停稳；蛆虫巢穴62区(17551,5251)的退订／楼梯指派，以及黑暗森林本地(15638,4840)停稳、Host采样(15637,4839)的等待组合待复验，不补假Host终点 |
| NPC近点及服务衔接停滞 | 鲁高因Jerhyn双端预测曾不同，Host占位阻断而客户端继续；待核对NPC路径提前停止、位置校验与接近恢复。日蚀后服务停留及Drognan未收27，须关联双端脚点、NPC与会话消息，归因未全定 |
| 营地传送点开窗未确认 | 有04／13但未收63；失败来源已暂缓，原进城路线仍需核对，旧96不证明最终Host脚点 |
| 跨平台与远端中继往返 | 已接可执行前段、修正落点及后缀复验；庇护所／剥皮地窖原落点和步行／传送组合待重现，未驻留不是永久无路 |
| 旅行与条件切换重复失败 | 同局保留失败成本；第三幕城镇到94的78传送点选择、76／77／78路线及切换任务后的原失败组合待复验 |

## 待验收调试导航

本节唯一维护工具验收范围，不等于已确认缺陷，也不替代原版一致性清单。方法与授权见[测试指南](TESTING.md)，运行产物不进入长期文档。

### 组合验收范围

| 编号 | 范围 |
| --- | --- |
| R01 | 第一幕bks／bkd、已进Tristram、救出待奖、持有／已交hdm等部分进度与重入 |
| R02 | 后幕补办前幕任务；合法63与跨幕、完成跳过、服务资格保留分别核对 |
| R03 | COMPLETEDNOW／状态12无个人资格及Cain不可再救的skipped；混合队列不伪造completed或奖励 |
| R04 | 背包／方块／私人箱／初始Cursor取回；无损交换及授权单张卷轴清理、满包／未知尺寸／归属变化、迟到与暂停；整理扩展待实机验收 |
| R05 | 第二幕材料／成杖／插杖后线索、373／377／430前置与Join重入；NPC未指派、多人已开门、目标对白与可选menu2不互相替代 |
| R06 | 第三幕多容器恢复；第四幕已放石／任务锤／部分封印及有界补锤；第五幕冰药／奖励／入口、新局古代人及王座残怪范围核查 |
| R07 | NPC转角前视、原生近距交接／停滞恢复、自动对白后服务和跨幕保护；不同双端脚点下不能以受理代替结果 |
| R08 | 斜跳、区域归属与旧63交错、无卷轴回城、点未指派、563重入；统一旅行估价、捷径结束及补给后重选目的地 |
| R09 | 碰撞／图元独立发布、接缝占地、分支与局部暂缓、延迟指派／退订、窄岸／长墙／桥口；庇护所候选与传送台、物件碰撞过渡、快照reset及修订复验 |
| R10 | 面板／最小化／暂停／重载与结果交错；药剂、Cursor／方块／插杖、事件、掉落打断／离区／他人拾取；执行预算与现实期限分开 |
| R11 | Frozen Orb独立12格与备用射程；风筝耗时、等待防御交还、传送途经及入战、步行／多段事件站位；净生命／回升／未知反馈、延迟／持续技能、首领脱离与无安全点 |
| R12 | 各幕商人、未知资源、资金／容量、无货架有界重开、Shift成交／迟到、治疗后再受伤及弹药不足 |
| R13 | 修正落点后精确续接、18格原生绕行、群攻清路／闪避／补弹后复原、中立阻挡；无传送开门、长门十字占地、关门／退订、出城首传及跨重规划失败上限 |
| R14 | 自然图元种子／Layer／区域消歧、负投影／量化、错误入口暂缓；五幕地标、鲜血荒地可见土路／桥面中断、进入冰冷之原后取消道路偏好、瓦特双墙线索，不借额外reveal或隐藏预设 |
| R15 | join启动器与同房间续跑；同一当前构建从全新隔离角色不中断完成五幕、不同职业及原版Host／D2X Host组合；分段结果不认证整链 |

### 实现边界与独立缺口

- 未实现自动练级、属性／技能分配、通用装备估值／换装、死亡后自主复活取尸；需预先准备能力足够的角色。
- 未实现通用小地图墙／河／道路拓扑、封闭空白概率、共享图标完整消歧或仅凭历史图元穿越未加载区域；可见土路和瓦特壁画是有限线索。
- 原武器恢复仅绑定同游戏GUID；不能跨进程猜旧装备。收纳仅有有界搜索及显式清理白名单，不保证任意跨容器布局可解。
- 普通怪物现场绝对HP未知，静电场仅用原比例下限；完整召唤、陷阱数量、蓄气连招及全部危险预兆未覆盖。
- observed-passages尚无离线重放分派；采样与文件限额可能导致诊断不完整，日志缺项不能证明事件未发生。

## 专题索引

以下保留旧链接入口；新内容直接更新负责页，不在此追加副本。

<a id="全链路问题的共用恢复"></a>
<a id="执行分层"></a>
<a id="自适应决策节奏"></a>
<a id="事件观察与现场保持"></a>
<a id="公共操作与部分进度恢复"></a>

动作、时钟与恢复：[执行契约](DEBUG_AI_RUNTIME.md)；任务恢复：[条件图](DEBUG_AI_QUESTS.md#条件图与恢复)；解锁记忆：[旅行与传送点](../architecture/CLIENT_NAVIGATION_REDESIGN.md#旅行与传送点)。

<a id="场景记忆的归属"></a>
<a id="导航与动作执行"></a>
<a id="共享寻路预算"></a>

知识、路径与预算：[导航结构](../architecture/CLIENT_NAVIGATION_REDESIGN.md)。

<a id="事后查看行为日志"></a>
<a id="慢搜索与无路现场导出"></a>

日志与导出：[AI诊断](DEBUG_AI_DIAGNOSTICS.md)。

<a id="第一幕任务配方"></a>
<a id="第二幕任务配方"></a>
<a id="第三幕任务配方"></a>
<a id="第四幕任务配方"></a>
<a id="第五幕任务配方"></a>
<a id="其他任务与五幕首领的覆盖边界"></a>
<a id="马勒斯任务配方"></a>
<a id="交易的工具配方"></a>
<a id="一条聊天命令安排五幕"></a>

五幕流程与首领：[任务配方](DEBUG_AI_QUESTS.md)；马勒斯默认保留灌注，面板编号5。

<a id="可复用女巫基准档"></a>

角色准备与续跑：[启动指南](BUILD_AND_RUN.md#诊断启动与任务-ai)。

<a id="实现入口与待验收范围"></a>

实现入口：[执行契约](DEBUG_AI_RUNTIME.md#权限与状态)；剩余验证：[组合验收](#组合验收范围)。历史构建、通关或复测流水不再作为当前能力证明。
