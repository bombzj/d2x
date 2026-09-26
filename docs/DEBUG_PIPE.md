# Windows 调试命令

直接运行 EXE 时，调试管道仅在指定 `--debug-pipe` 时启用，默认暂停模拟；加 `--debug-run` 则直接运行。Play.cmd 通过启动脚本默认开启 `d2x-debug` 并直接运行。管道允许改变当前游戏，请谨慎操作重要现场。

## 启动

双击 Play.cmd 即可开启默认管道，无需额外参数。启动器可选项：

```powershell
.\Play.cmd
.\Play.cmd -DebugPaused
.\Play.cmd -PipeName d2x-second
.\Play.cmd -NoDebugPipe
```

默认启动不暂停；`-DebugPaused` 启动后等待调试命令，不能与 `-NoDebugPipe` 合用。第二个实例需使用不同的 `-PipeName`，客户端也指定同名参数。同名冲突会明确失败，不自动连接或控制原实例。原 `-Mpq`、`-Level`、`-Region` 参数保持可用。

直接运行 EXE 并指定独立存档：

```powershell
.\build\bin\d2x.exe --mpq assets/mpq2 --level 8 --debug-pipe d2x-debug --save artifacts/debug.d2xsave
```

可加 `--hidden`；隐藏调试实例限 60 FPS。管道名称只能包含 1–80 个 ASCII 字母、数字、下划线和短横线。同名管道已存在时启动失败，不接管其他实例。Linux 常规构建保留，指定该选项会明确报 Windows 专用。

## 调用

另一个 PowerShell 终端运行，推荐 PowerShell 7：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command status
$monsters = (.\scripts\Send-D2XCommand.ps1 -Command monsters).monsters
$monsters | Format-Table
$target = $monsters | Where-Object { $_.hp -gt 0 -and $_.rank -eq 'Normal' } | Select-Object -First 1
if ($target) {
    .\scripts\Send-D2XCommand.ps1 -Command kill -Arguments @{ id = $target.id }
}
$items = (.\scripts\Send-D2XCommand.ps1 -Command ground).items
$items | Format-Table
if ($items.Count -gt 0) {
    .\scripts\Send-D2XCommand.ps1 -Command pickup -Arguments @{ id = $items[0].id; revision = $items[0].revision }
    .\scripts\Send-D2XCommand.ps1 -Command step -Arguments @{ ticks = 100 }
}
.\scripts\Send-D2XCommand.ps1 -Command inventory | ConvertTo-Json -Depth 6
.\scripts\Send-D2XCommand.ps1 -Command save
```

自定义管道名通过 `-PipeName` 指定。返回值为 PowerShell 对象；服务端 `ok=false`、连接失败或超时会抛异常。默认超时 10000 毫秒，`-TimeoutMs` 支持 100–60000。暂停中移动／拾取需要 `step` 推进，或 `resume` 恢复实时模拟。没有可见怪物时，可用 `monsters -Arguments @{visible=$false}` 查询当前区域已创建单位坐标，再用 `move` 和 `step` 正常寻路靠近。

只想快速检查真实掉落链时，直接对当前区域已创建的怪物实体 ID 使用 `drop`，无需移动或调整相机。已知实体 ID 可用 `-Command drop -Arguments @{ id = 1076 }`；未知时先查询：

```powershell
$target = (.\scripts\Send-D2XCommand.ps1 -Command monsters -Arguments @{ visible = $false }).monsters |
    Where-Object { $_.hp -gt 0 } | Select-Object -First 1
if ($target) {
    .\scripts\Send-D2XCommand.ps1 -Command drop -Arguments @{ id = $target.id }
}
```

响应的 `drops` 列出本次地面物品 ID、代码、数量、品质、原特殊行号和坐标；`deferred` 给出无法结算的原因。`drops=[]` 且 `deferred=null` 也可能是原 TC 的 NoDrop。这里的 ID 是 `monsters` 返回的会话实体 ID，不是 MonStats 怪物类型代码；尚未创建的刷怪计划没有可用实体 ID。

定向怪物现场可跳过自然刷怪：`monster-spawn` 的 `monster` 是当前 MPQ `MonStats.Id`，`x/y` 是当前区域的可走地图坐标，必须与角色及现有活怪留出空位。仅接受原表的敌对身份，城镇与玩家死亡时拒绝；未实现的敌对外观返回 `substitute=true`。生成仍走正式实例 ID、原生命掷骰和当前难度；`monsters` 返回 `debugSpawn=true`，只在本局会话中保留这个来源；角色存档不会保存调试生成的怪物。随后用 `monster-damage` 扣指定血量，若致死则走正式经验、掉落和死亡结算；`monster-kill` 是现有 `drop` 的直观别名，`kill` 仍要求目标在屏幕内。

```powershell
$spawn = .\scripts\Send-D2XCommand.ps1 -Command monster-spawn -Arguments @{monster='zombie1'; x=22.5; y=25.5}
.\scripts\Send-D2XCommand.ps1 -Command monster-damage -Arguments @{id=$spawn.id; amount=3}
.\scripts\Send-D2XCommand.ps1 -Command monster-kill -Arguments @{id=$spawn.id}
```

示例坐标对应邪恶洞窟 `--map-seed 210` 的入口；其他地图需挑选可走且未占用的格子。管道请求本身是 JSON，其他语言可发送相同 `command` 和参数，无需另造游戏规则入口。`monster-damage` 可作用于当前区域已创建但不在镜头内的敌人，返回剩余血量；致死时与 `monster-kill` 一样返回经验及新掉落。

`monsters` 同时返回当前 `attackMode`、`attackRemaining`、`impactRemaining` 及已实现外观的 `attackDuration/attackImpact/attackFrames`；Brute、普通骷髅、僵尸和沉沦魔另有 `attack2Duration/attack2Impact/attack2Frames`。这些时序从挂载 MPQ 的 `AnimData.d2` 读取，可在暂停状态逐帧 `step`，确认 A2 模式与命中事件；角色载入将重置怪物。

`aiEscaping` 表示 Fallen 正沿见尸逃离路线移动；它与原身份、路线都只在本局维持。可在同一可走房间生成两只 `fallen1`，击杀其中一只再 `step` 一帧观察另一只。原死亡动作时长由运行时 MPQ 的 `AnimData.d2` 决定。`monsters` 同时返回原生成 `group`、`aiCommanded` 和 `skill2Remaining`，可观察同组命令和 S2 喊叫，后二者不会进入角色存档。

`aiCircling` 表示 Brute、Bighead 或 Skeleton Mage 正沿可走路径绕目标行走。可定点生成 `brute2`，逐次 `step` 后查询 `monsters`，在近战攻击两次 `aip3` 掷骰中首次失败、第二次成功时观察该状态和位移；绕行路线只在本局维持。

`aiRunning` 表示 Corrupt Rogue 当前跑步动作；`aiAdvanceRemaining` 表示 Corrupt Rogue、Skeleton Bow 或 Skeleton Mage 当前路径决策的剩余距离。可定点生成 `corruptrogue1`，逐帧推进并用 `monsters` 观察；该状态只在本局维持。

`monsters` 的 `hostileProjectiles` 统计该怪物仍在飞行的敌方弹体；`aiRetaliate` 表示 Quill Rat 受击后待 A2 回击。可定点生成 `quillrat1` 后推进 6 帧观察原 `spike1` 发射，在本局继续推进观察命中；这两项状态不会进入 v83 角色存档。`aiEscaping` 对 Quill Rat 也表示其按 MPQ `aip4` 距离后撤。

普通怪物的 `combat` 返回共用三难度 MPQ 解析后的等级、生命区间、可用的 A1／A2 `[最小伤害, 最大伤害, 命中]`、防御、暴击、再生和六项抗性。原表缺少某攻击模式时省略该模式；缺命中或防御列时相应位置为 `null`。`combat.elements` 返回有完整数值的 `El1–3` 模式、元素种类、概率、伤害区间及持续帧数；`status.player.chill` 是冰冷剩余秒数，`poisonRemaining` 与 `poisonPerSecond` 是毒素剩余秒数和每秒伤害。这些字段与游戏使用同一解析结果，不是调试接口另算的一套数值。

`status.combat` 返回角色当前结算用的抗性、固定减伤、格挡和每把武器的物理及元素伤害范围；`global*Damage` 是不限定武器的部分，`weapons` 中合并了当前手中武器专属词缀。速度、吸取、寻宝等尚未生效的字段只表示已解析的装备修正，不代表对应动画帧或掉落结算已实现。限时效果数量见 `activeEffects`。

物品操作复用游戏的事务与访问校验。以下调用可分别使用，不要求按顺序执行：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command pickup -Arguments @{ id = 1200; ticks = 250 }
.\scripts\Send-D2XCommand.ps1 -Command item-move -Arguments @{ id = 1200; to = 'backpack'; x = 2; y = 1 }
.\scripts\Send-D2XCommand.ps1 -Command item-move -Arguments @{ id = 1200; to = 'stash' }
.\scripts\Send-D2XCommand.ps1 -Command equip -Arguments @{ id = 1200; slot = 'rarm' }
.\scripts\Send-D2XCommand.ps1 -Command use -Arguments @{ id = 1201 }
.\scripts\Send-D2XCommand.ps1 -Command item -Arguments @{ id = 1200 } | ConvertTo-Json -Depth 8
```

`pickup` 仍需正常寻路和容量，`ticks` 到上限时返回 `queued=true` 表示仍在靠近。`item-move` 的 `to` 可为 `backpack`、`belt`、`stash`、`cube`；同时指定从 0 开始的 `x`、`y` 则放入目标格，不指定则自动放置。跨容器自动放入背包／储物箱／方块沿用堆叠合并，源 ID 可能消失，响应会标明 `removedByMerge`；同容器移动需指定格子；指定同类堆叠所在格会执行合并。从装备栏移出会走正式卸装，储物箱仍须正常开启，方块必须在背包中。装备栏入口用 `equip` 的原部位代码，省略 `slot` 表示卸下入背包；`belt` 槽也受腰带专用事务约束。`use` 返回使用成功的物品 ID。

`item` 只读查询单件实例的品质原行、词缀行、属性掷值、书的页数、位置及从 MPQ 适配的外观 token；`status` 给出钱包和私人箱金币，以及本局出现过的暗金原行数量。

可用 `item-spawn` 在人物脚边生成已鉴定的原 MPQ 装备，快速检查地面文字和拾入背包后的悬停说明。`code` 为当前 MPQ 的武器或护甲代码，`quality` 只接受 `magic`（蓝）、`rare`（黄）、`set`（绿）、`unique`（暗金），`level` 为 1–99 的物品等级。须选择该品质在原表中可生成的底材与等级；缺词缀、特殊行或原图时会明确拒绝。响应返回新实例 `id`，之后可用 `ground`、`pickup`、`item` 查看。

```powershell
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments @{ code = 'cap'; quality = 'magic'; level = 30 }
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments @{ code = 'cap'; quality = 'rare'; level = 30 }
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments @{ code = 'cap'; quality = 'set'; level = 30 }
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments @{ code = 'cap'; quality = 'unique'; level = 30 }
```

该命令复用正式词缀／特殊行掷值与库存创建，但有意绕过 TC、品质概率及同局暗金唯一限制；不能拿它证明自然掉落正确，也不会计入正式掉落结算。调试物品可进入正常角色存档，重要现场请使用独立 `--save` 路径。2026-09-26 Release 已实际调用四种品质生成、拾取及 D2S 保存／重载；无效代码拒绝且不增加物品。PowerShell 客户端白名单已包含 `item-spawn`。

`ui-input` 可附带 `screenshot=true`，在该输入处理后的同一帧保存 `artifacts/d2x-capture.png`，用于准确捕获悬停提示。`showLoot=true` 模拟该帧显示地面标签。`status.ui` 的 `purchaseConfirmation`、`saleConfirmation`（物品 ID，0 表示无）和 `shopRepair` 只读反映当前交易界面，截图前应先确认 `shop=true`。单独 `screenshot` 命令仍保存上一张已绘制帧到 `artifacts/debug-pipe.png`。

调试不同职业时，`Play.cmd -Class Sorceress -DebugPaused` 可直接创建全新的女巫进入城镇，无需角色界面和存档；`-Level 8` 可指定邪恶洞窟。EXE 使用 `--class Sorceress --level 8 --debug-pipe d2x-debug`。职业名按 MPQ 原名传入：`Amazon`、`Sorceress`、`Necromancer`、`Paladin`、`Barbarian`、`Druid`、`Assassin`。新角色走正式一级属性与初始装备构造，不是原地改写旧角色。

有场景存档时使用 `Play.cmd -Load <角色.d2s> -Save <输出.d2s> -DebugPaused`，EXE 对应 `--load`／`--save`；`-Class` 与 `-Load` 互斥。脚本的存档路径按调用时工作目录解析。加载按原规则从城镇开始，可再用 `travel` 到目标区域。不要把输入场景档作为输出路径，除非确实要覆盖它；不指定 `-Save` 的命令行直进实例不会在退出时自动保存，手动 `save` 则使用下文默认路径规则。`status.player.class` 可核对职业。运行中的原地换职业命令和快捷键已移除。

技能调试入口使用正式升级、分配和存档状态。先通过 `skills` 查看当前职业的原技能 ID、前置及 F1–F8 绑定，再用 `grant-experience` 获得升级技能点；`learn-skill` 遵守等级、前置和最大等级。`reset-skills` 与 `Ctrl+Alt+T` 归还已分配点，`skill-tree` 可指定 1–3 页打开界面并配合 `screenshot` 查看。`skill-picker` 打开左右技能菜单，`bind-skill-hotkey` 通过正式会话命令绑定或清除快捷键，便于复查存档。

下例要求已进入女巫角色：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command skills | ConvertTo-Json -Depth 6
.\scripts\Send-D2XCommand.ps1 -Command grant-experience -Arguments @{ amount = 10000 }
.\scripts\Send-D2XCommand.ps1 -Command learn-skill -Arguments @{ id = 36 }
.\scripts\Send-D2XCommand.ps1 -Command skill-tree -Arguments @{ page = 3 }
.\scripts\Send-D2XCommand.ps1 -Command reset-skills
```

营地商人可先从 `objects` 取会话对象 ID，再按正常交互进入菜单；`shop` 给出可买 slot，`buy` 使用同一购买事务。`talk` 列出任务话题并可选择原 MPQ 对话，购买不要求先 Talk。

```powershell
$vendorId = ((.\scripts\Send-D2XCommand.ps1 -Command objects).objects | Where-Object name -eq 'Charsi' | Select-Object -First 1).id
.\scripts\Send-D2XCommand.ps1 -Command interact -Arguments @{ id = $vendorId; ticks = 250 }
$offers = (.\scripts\Send-D2XCommand.ps1 -Command shop -Arguments @{ id = $vendorId }).offers
.\scripts\Send-D2XCommand.ps1 -Command buy -Arguments @{ id = $vendorId; slot = $offers[0].slot }
```

## 命令

`cast-skill` 接受原技能 `id` 和区域内目标 `x/y`，通过正式 `UseClassSkill` 提交，不绕过等级、法力或施法时序；`accepted` 表示开始了施法或接受了已有引导的目标更新。`status.player.castRemaining` 返回动作剩余时间，`status.missiles` 只读返回当前弹体 ID、原导弹 ID、位置、速度、伤害、剩余寿命和路径点数。可用 `step` 逐帧观察，不直接修改技能结果。

地狱之火 `cast-skill` 会持续引导，重复调用只更新目标；`stop-channel` 提交正式停止命令，不推进模拟。`status.player.channelSkill` 为原技能 ID，未引导为 -1，`channelAge` 为本次引导秒数。真实右键释放由控制器提交同一停止命令；命名管道持续施法不模拟鼠标按住，不等同于人工输入验收。

`status.effects` 返回临时效果的原技能 ID、组、剩余时间、防御加成、冻结回击时长和叠层 ID；`monsters` 返回 `freeze/chill` 剩余时间，可观察防御冰甲的真实受击触发。

`skills.skills[].allowedInTown` 为当前 MPQ 的城镇施法许可。城镇调用禁用的 `cast-skill` 返回 `accepted=false`、`message="This skill cannot be used in town"`，不扣蓝、不进入动作、不生成弹体。冰封装甲允许在城镇使用；技能选择、绑定与是否已学会不受城镇限制影响。

### 快速授予佣兵

```powershell
.\scripts\Send-D2XCommand.ps1 -Command grant-hireling
.\scripts\Send-D2XCommand.ps1 -Command hireling | ConvertTo-Json -Depth 6
.\scripts\Send-D2XCommand.ps1 -Command hireling-panel -Arguments @{open=$true}
```

`grant_hireling` 也可使用。授予不需要任务、金币或 NPC 距离，按当前难度、角色等级及资料片 MPQ 生成一名罗格，默认打开属性面板；`open=$false` 可只领取。重复执行不替换已有佣兵或装备，`created` 表示本次是否新建；死亡角色拒绝领取。响应的 `hireling` 包含名字、等级、生命、经验、伤害、四抗和装备实例，未雇佣时查询为 `null`。`hireling-equip -Arguments @{id=物品ID;slot='rarm'}` 从背包装备；原部位代码可为 `head`、`tors`、`rarm`，省略 `slot` 卸下入背包，均复用正式装备限制。`ui-input` 的 `key='o'`／`'hireling'` 模拟 O 键。

接口源码随 v89 加入；该版本曾完成构建，但这些命令尚未单独运行验收。更早的 EXE 不含这些命令。完整边界见 [资料片佣兵](HIRELINGS.md)。

### 命令表

| command | Arguments | 行为 |
| --- | --- | --- |
| grant-hireling / grant_hireling | 可选 `open`，默认 true | 立即授予资料片罗格并打开 O 面板；已有则保留，不扣金币或改任务 |
| hireling | 无 | 查询佣兵身份、派生属性及装备；不修改状态 |
| hireling-panel | 可选 `open`，默认 true | 开关已有佣兵的 O 面板 |
| hireling-equip | `id`，可选 `slot` | 正式佣兵装备事务；省略 slot 卸下入背包 |
| status | 无 | 玩家坐标、生命、蛛网减速剩余时间／百分比、钱包、区域、击杀、已结算数、掉落随机状态、调试暂停状态，以及 ui 中的商店／NPC 菜单／字幕偏移／I、II 组／左右面板伤害 |
| quest-status | 无 | 当前难度六项第一幕任务的阶段和标记，只读；A1Q3 灌注、A1Q4 石阵与 A1Q6 结局都使用正式会话状态 |
| quest-panel | 可选 `open`、`selected`（-1 为总览，0–5 为六项） | 调试打开原 MPQ 任务面板，便于与 `screenshot` 查看布局 |
| monsters | `visible`，默认 true | 当前区域已创建怪物 ID、真实身份、召唤来源、等级类别、当前／最大生命、蛛网光环、原 AI 名、停顿／追击、当前攻击／命中剩余时间、MPQ A1 动作时长／命中时刻与帧数、坐标、屏幕内／激活状态；不含尚未创建计划 |
| ground | 无 | 当前区域地面物品 ID、版本、代码、数量、品质、特殊行号、物品等级、坐标 |
| inventory | 无 | 所有角色容器内物品及钱包；包含背包、腰带、装备和私人箱，不改变箱子访问权 |
| item | `id` | 查询单件地面或容器物品的原行、属性掷值、位置与装备外观参数，不改变状态 |
| item-spawn | `code` 原 MPQ 武器／护甲代码、`quality` 为 magic／rare／set／unique、`level` 1–99 | 调试生成已鉴定装备在脚边；返回实例 ID，缺少原表行或资源时拒绝 |
| objects | 无 | 当前场景对象名称、原内容键、位置、可绘制标志，以及入口／边界预设信息 |
| exits | 无 | 当前出口名称、slot、访问坐标与启用状态 |
| view | `x`、`y` | 只移动相机并查询附近墙格键值与隐藏标志；下一次模拟步恢复跟随，不修改玩家位置 |
| equip | `id`，可选 `slot` | 正式预览及提交装备事务；如 rarm/larm/belt，省略 slot 则卸下入包；不推进世界时间 |
| item-move | `id`、`to`，可选成对 `x`、`y` | 背包、腰带、已开启储物箱、已携带方块间移动；也可从装备栏卸下到指定容器，指定已占用的同类堆叠格会合并，正式预览后提交，不推进世界时间 |
| travel | `level` | 调试自由传到已实现地图，优先落在原传送点旁；不检查或写入激活记录 |
| waypoint | `id`、`level` | 正常传送点命令，id 是当前区域源点；校验距离／通路、两端激活及玩家状态，拒绝时返回错误 |
| interact | `id`，可选 `ticks` 1–250 | 正常走近对象交互；返回是否已开启、仍在寻路；NPC 首先打开交互菜单 |
| objects | 可选 `interactiveOnly` 布尔值 | 列出当前区域对象；可筛选可交互／已操作对象，返回 Objects 原类别、操作编号、祭坛 Code 和井水余量 |
| grant-shrine | `code`：运行时 `Shrines.txt` 的 Code | 调试领取指定祭坛效果，无需找实物；限时效果见 `status.shrines`，一次性效果显示领取提示 |
| talk | 可选 `quest`：0–5 | 打开当前 NPC 的 Talk 话题菜单，返回全部可用任务；传 quest 播放该条原文，回顾不推进任务 |
| gossip | 无 | 已打开的 NPC 菜单或对话切换到下一段原 MPQ 通用闲聊，返回文本和排版行数；不改变玩法状态 |
| identify | 凯恩对象 `id` | 需先正常交谈且在范围内；玩家亲自救出凯恩则免费，罗格代救则每件 100 金币，返回数量和扣款 |
| shop | 商人对象 `id` | 查询原 MPQ 货架报价、`storePage`、常驻／已售状态；该 NPC 菜单已打开时进入货架界面 |
| buy | 商人对象 `id`、货架 `slot` | 需先正常交谈并保持同区商店会话；按报价扣金币并正式创建背包物品，随机货品售出后不可重购 |
| grant-gold | `amount` | 增加钱包金币，仍遵守当前角色等级对应的携带上限，便于检验需付费的 NPC 服务 |
| gold-transfer | `action` 为 `deposit`／`withdraw`／`drop`，`amount` | 正式金币存取／丢弃；存取需先正常打开私人箱，丢弃受 MPQ 单堆上限约束 |
| cube-drop | 无 | 调试快捷投放一件原 MPQ 方块于人物脚边；已有方块时拒绝。对应按键 `Ctrl+Alt+B` |
| cube-open | 无 | 角色已把方块拾入背包时打开原面板，返回随身容器 ID |
| book-load | `scroll`、`book` 物品 ID | 用正式装书事务把对应卷轴放入书，返回页数 |
| identify-item | `source` 为鉴定卷轴／书、`target` 为未鉴定物品 ID | 消耗卷轴／一页并鉴定目标，返回鉴定结果 |
| grant-experience | `amount` | 增加经验并依照运行时 MPQ `Experience.txt` 的当前职业阈值升级；达到 `MaxLvl` 时封顶；每升一级增加一个未用技能点 |
| unlock-waypoints | 无 | 激活当前 MPQ 所建区域中实际存在的传送点，返回激活的区域 ID；对应快捷键 `Ctrl+Alt+W` |
| allocate-attribute | `attribute`：`strength`／`dexterity`／`vitality`／`energy` | 正式分配一个未用属性点；角色面板和装备需求同步刷新 |
| reset-attributes | 无 | 调试重置四维已分配点；等级、经验和装备槽不变，需求不足的装备停用 |
| skills | 无 | 返回当前职业 MPQ 技能节点、页／行／列、等级门槛、前置、等级和剩余点数 |
| bind-skill-hotkey | `key` 1–8、`id` 原技能 ID；`-1` 普攻、`-2` 清除；可选 `right` 布尔值 | 用正式会话规则绑定 F1–F8，拒绝不可用、被动或不允许左键的技能 |
| learn-skill | 技能 `id` | 按正式分配命令学习或升级技能；拒绝等级、前置或点数不满足的请求 |
| reset-skills | 无 | 清空已分配技能并归还升级所得技能点；不会重置等级或经验 |
| character-panel | 可选 `open` 布尔值，默认 true | 打开或关闭角色面板，便于结合 `screenshot` 对照职业外观和数值 |
| skill-tree | 可选 `open` 布尔值和 `page` 1–3 | 打开或关闭当前职业技能树并切到指定页，便于结合 `screenshot` 查看布局 |
| skill-picker | 可选 `open`、`right` 布尔值，默认 true | 打开或关闭左／右技能菜单，便于结合 `screenshot` 查看图标与快捷键标签 |
| use | `id` | 正常物品预览和使用，返回 `used`；支持背包回城卷轴和有页数的回城书，城镇使用拒绝 |
| portal | `revision` | 正常走近当前蓝门；需使用 status 中当前版本，营地返程关闭双端点 |
| kill | `id` | 仅击杀存活且当前屏幕范围内、已激活的指定怪物，玩家须存活；使用正常死亡事件及掉落结算 |
| drop | `id` | 快速击杀当前区域已创建且存活的指定怪物，不要求可见或激活；不推进世界时间，经相同死亡和 MPQ 掉落结算，返回本次地面物品 |
| pickup | `id`，可选 `revision`、`ticks` | 提交正常拾取请求，默认执行一个固定步；ticks 为 1–250，返回 `pickedUp`、`queued` 和实际推进步数 |
| move | `x`、`y` | 校验当前地图可行走坐标，提交正常 MoveTo 并执行一个固定步，不是传送 |
| step | `ticks`，默认 1，范围 1–250 | 同步推进固定步，每步 1/25 秒，包含 AI、伤害、死亡及拾取；不会暂停怪物单独移动玩家 |
| pause / resume | 无 | 暂停／恢复实时模拟；调试暂停独立于游戏菜单暂停 |
| save / load | 无 | 使用启动时 `--save`、否则 `--load`、否则 `saves/quick.d2s`；保存角色，载入后在城镇开启新的一局；不能通过请求任意指定路径 |
| ui-input | 可选 `x`、`y`、`button`（left／right）、`key`（escape／enter／inventory／character／quests／weapon-swap／automap／r／run／restart） | 排入下一帧的正常 SceneController 输入，坐标基于 1066×680；不直接调用购买或加点事务，适合核对实际界面路径；返回 queued 后在后续请求查看状态 |
| screenshot | 无 | 保存最近渲染画面到 `artifacts/debug-pipe.png`；命令返回前一已完成帧，立即移动后可在下一请求截取 |
| quit | 无 | 正常退出；若启动指定 `--save`，退出时仍会保存 |

kill 和 drop 都不进行攻击命中／伤害计算，因此用于验证死亡和掉落链，不能作为普通战斗伤害验证。drop 确实杀死怪物并消耗当前掉落随机状态，同一实体不能重复结算；生成物仍落在怪物原位置，远处拾取须正常移动。kill 的屏幕范围按当前 SceneView 投影与背包遮挡判断，不保证像素级遮挡或墙后可见性。没有批量清屏、改随机种子、绕过上限的任意钱包写入或任意代码执行命令。`item-spawn` 是受限的品质显示入口，使用原 MPQ 掷值但不代表正式 TC 和品质概率；真实掉落须用 `drop`，再经正常拾取和库存事务检查。

## 协议和安全

- `\\.\pipe\<name>`，Windows byte-mode named pipe，UTF-8 单行 JSON。请求如 `{"command":"kill","id":1076}`，响应如 `{"ok":true,"command":"kill","killed":1076}`；失败包含 `error`。
- 每次连接一条请求及一条响应，响应换行后客户端关闭。客户端断线或 15 秒超时后回收连接。请求上限 16 KiB，响应上限 4 MiB，单实例连接。
- EXE 不指定管道时无监听；Play.cmd 默认显式传入管道参数，可用 `-NoDebugPipe` 关闭。ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令处理、会话变更和呈现均在游戏主线程，没有后台线程直接修改库存。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由用户授权新增。

## 当前证据与限制

当前角色存档只保留持久进度，载入会返回城镇并重置怪物；格式与规则版本见[存档说明](SAVES.md)，旧档不迁移。技能树与女巫技能菜单已构建截图，快捷键已完成存读档冒烟；基础远程攻击和女巫战斗效果待逐项实机验收。v10／规则 v30 的传送现场 `artifacts/waypoint-state-v10.d2xsave` 仅是历史证据，不能按当前格式读取。status 增加 portal、waypoints 和 travelMenu；objects 中 Waypoint 返回 activated 及原 fps。新游戏包括营地全部未激活；travel 自由传送不激活，waypoint 不能绕过解锁。Ctrl+F2 是独立开发目录，不是游戏传送点菜单。原三态动画、首次交互、锁定目的地拒绝及解锁保存恢复曾在 v10 实际验证，本轮未重新运行。

新增 `drop`、`item-move`、`item` 及物品命令回执已完成源码和协议文档。Windows Release 构建后，实际调用 `item` 查询初始手斧、`equip` 卸下和重新装备，以及对当前区域指定怪物 `drop`；后者生成 6 件地面物品、无暂缓原因，已结算数从 0 到 1。拾取、药水使用和头盔胸甲图层仍待定向验收。

在原洞窟地图种子 210、掉落种子 10 下，通过正常移动接近后击杀四个可见普通怪物，得到箭袋 `aqv`（数量 196、等级 2）、金币 5、法力药水及一次 NoDrop。箭袋正常入包保留数量与等级；金币正常拾取后地面实例消失、钱包变为 5、不占背包。重复击杀同 ID 被拒绝；管道保存／恢复后金币 5、箭袋 196、击杀和结算数 4、随机状态保持。现场 `artifacts/gold-pipe-v8.d2xsave`，截图 `artifacts/gold-wallet-v8.png`，均不提交。

上述金币现场使用历史存档 v8／规则 v26，不能按当前格式恢复。六件上限、钱包满额部分拾取、跨用户 ACL 拒绝及 Linux 构建尚未实际验证。金币仓库存取／死亡掉金和装备金币加成仍未实现；投掷基础消耗已接源码但未运行验收。当前品质展示实例见 [物品补全](ITEM_COMPLETION.md)。

另有历史 v27 视觉现场 `artifacts/visual-v27.d2xsave`；status 包含 look／routePoints，monsters 包含可用的原 sourceVelocity。view 会改变当前屏幕范围，因此 kill 的可见约束按调试相机计算，但仍要求单位已激活；drop 不受该约束。
