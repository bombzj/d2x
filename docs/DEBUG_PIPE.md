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

物品操作复用游戏的事务与访问校验。以下调用可分别使用，不要求按顺序执行：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command pickup -Arguments @{ id = 1200; ticks = 250 }
.\scripts\Send-D2XCommand.ps1 -Command item-move -Arguments @{ id = 1200; to = 'backpack'; x = 2; y = 1 }
.\scripts\Send-D2XCommand.ps1 -Command item-move -Arguments @{ id = 1200; to = 'stash' }
.\scripts\Send-D2XCommand.ps1 -Command equip -Arguments @{ id = 1200; slot = 'rarm' }
.\scripts\Send-D2XCommand.ps1 -Command use -Arguments @{ id = 1201 }
.\scripts\Send-D2XCommand.ps1 -Command item -Arguments @{ id = 1200 } | ConvertTo-Json -Depth 8
```

`pickup` 仍需正常寻路和容量，`ticks` 到上限时返回 `queued=true` 表示仍在靠近。`item-move` 的 `to` 可为 `backpack`、`belt`、`stash`；同时指定从 0 开始的 `x`、`y` 则放入目标格，不指定则自动放置。跨容器自动放入背包／储物箱沿用堆叠合并，源 ID 可能消失，响应会标明 `removedByMerge`；同容器移动需指定格子。从装备栏移出会走正式卸装，储物箱仍须正常开启。装备栏入口用 `equip` 的原部位代码，省略 `slot` 表示卸下入背包；`belt` 槽也受腰带专用事务约束。`use` 返回使用成功的物品 ID。

`item` 只读查询单件实例的品质原行、词缀行、属性掷值、位置及从 MPQ 适配的外观 token；`status` 同时给出本局出现过的暗金原行数量（含不限量行）和当前人物合成缺资源提示，便于对照穿脱与恢复前后的状态。

调试职业可在角色存活时切换。`Ctrl+Alt+C` 按 MPQ `CharStats` 的职业顺序循环；管道省略 `class` 也循环，指定原表职业名则直达：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command switch-character
.\scripts\Send-D2XCommand.ps1 -Command switch-character -Arguments @{ class = 'Sorceress' }
```

切换后等级、经验、属性和技能分配归零，生命／法力／耐力回满；背包、装备与世界保留，超出一级携带上限的金币裁剪。`status.player.class` 和响应 `class` 可核对职业；职业成长、经验阈值和技能树始终从挂载的 MPQ 读取。

技能调试入口使用正式升级、分配和存档状态。先通过 `skills` 查看当前职业的原技能 ID、前置及 F1–F8 绑定，再用 `grant-experience` 获得升级技能点；`learn-skill` 遵守等级、前置和最大等级。`reset-skills` 与 `Ctrl+Alt+T` 归还已分配点，`skill-tree` 可指定 1–3 页打开界面并配合 `screenshot` 查看。`skill-picker` 打开左右技能菜单，`bind-skill-hotkey` 通过正式会话命令绑定或清除快捷键，便于复查存档。

```powershell
.\scripts\Send-D2XCommand.ps1 -Command switch-character -Arguments @{ class = 'Sorceress' }
.\scripts\Send-D2XCommand.ps1 -Command skills | ConvertTo-Json -Depth 6
.\scripts\Send-D2XCommand.ps1 -Command grant-experience -Arguments @{ amount = 10000 }
.\scripts\Send-D2XCommand.ps1 -Command learn-skill -Arguments @{ id = 36 }
.\scripts\Send-D2XCommand.ps1 -Command skill-tree -Arguments @{ page = 3 }
.\scripts\Send-D2XCommand.ps1 -Command reset-skills
```

营地商人可先从 `objects` 取会话对象 ID，再按正常交互进入菜单；`shop` 给出可买 slot，`buy` 使用同一购买事务。`talk` 用于单独查看原 MPQ 对话，购买不要求先 Talk。

```powershell
$vendorId = ((.\scripts\Send-D2XCommand.ps1 -Command objects).objects | Where-Object name -eq 'Charsi' | Select-Object -First 1).id
.\scripts\Send-D2XCommand.ps1 -Command interact -Arguments @{ id = $vendorId; ticks = 250 }
$offers = (.\scripts\Send-D2XCommand.ps1 -Command shop -Arguments @{ id = $vendorId }).offers
.\scripts\Send-D2XCommand.ps1 -Command buy -Arguments @{ id = $vendorId; slot = $offers[0].slot }
```

## 命令

| command | Arguments | 行为 |
| --- | --- | --- |
| status | 无 | 玩家坐标、生命、钱包、区域、击杀、已结算数、掉落随机状态、调试暂停状态 |
| monsters | `visible`，默认 true | 当前区域已创建怪物 ID、真实身份、等级类别、当前／最大生命、原 AI 名、停顿／追击、当前攻击／命中剩余时间、MPQ A1 动作时长／命中时刻与帧数、坐标、屏幕内／激活状态；不含尚未创建计划 |
| ground | 无 | 当前区域地面物品 ID、版本、代码、数量、品质、特殊行号、物品等级、坐标 |
| inventory | 无 | 所有角色容器内物品及钱包；包含背包、腰带、装备和私人箱，不改变箱子访问权 |
| item | `id` | 查询单件地面或容器物品的原行、属性掷值、位置与装备外观参数，不改变状态 |
| objects | 无 | 当前场景对象名称、原内容键、位置、可绘制标志，以及入口／边界预设信息 |
| exits | 无 | 当前出口名称、slot、访问坐标与启用状态 |
| view | `x`、`y` | 只移动相机并查询附近墙格键值与隐藏标志；下一次模拟步恢复跟随，不修改玩家位置 |
| equip | `id`，可选 `slot` | 正式预览及提交装备事务；如 rarm/larm/belt，省略 slot 则卸下入包；不推进世界时间 |
| item-move | `id`、`to`，可选成对 `x`、`y` | 背包、腰带、已开启储物箱间移动；也可从装备栏卸下到指定容器，正式预览后提交，不推进世界时间 |
| travel | `level` | 调试自由传到已实现地图，优先落在原传送点旁；不检查或写入激活记录 |
| waypoint | `id`、`level` | 正常传送点命令，id 是当前区域源点；校验距离／通路、两端激活及玩家状态，拒绝时返回错误 |
| interact | `id`，可选 `ticks` 1–250 | 正常走近对象交互；返回是否已开启、仍在寻路；NPC 首先打开交互菜单 |
| talk | 无 | 在已打开的 NPC 菜单中选择 Talk，返回原 MPQ 对话文本与排版行数 |
| gossip | 无 | 已打开的 NPC 菜单或对话切换到下一段原 MPQ 通用闲聊，返回文本和排版行数；不改变玩法状态 |
| identify | 凯恩对象 `id` | 需先正常交谈且在范围内；按未完成任务档位每件 100 金币鉴定背包和装备中的物品，返回数量和扣款 |
| shop | 商人对象 `id` | 查询原 MPQ 货架报价、常驻／已售状态；该 NPC 菜单已打开时进入货架界面 |
| buy | 商人对象 `id`、货架 `slot` | 需先正常交谈且在范围内；按报价扣金币并正式创建背包物品，随机货品售出后不可重购 |
| grant-gold | `amount` | 增加钱包金币，仍遵守当前角色等级对应的携带上限，便于检验需付费的 NPC 服务 |
| grant-experience | `amount` | 增加经验并依照运行时 MPQ `Experience.txt` 的当前职业阈值升级；达到 `MaxLvl` 时封顶；每升一级增加一个未用技能点 |
| allocate-attribute | `attribute`：`strength`／`dexterity`／`vitality`／`energy` | 正式分配一个未用属性点；角色面板和装备需求同步刷新 |
| reset-attributes | 无 | 调试重置四维已分配点；等级、经验和装备槽不变，需求不足的装备停用 |
| skills | 无 | 返回当前职业 MPQ 技能节点、页／行／列、等级门槛、前置、等级和剩余点数 |
| bind-skill-hotkey | `key` 1–8、`id` 原技能 ID；`-1` 普攻、`-2` 清除；可选 `right` 布尔值 | 用正式会话规则绑定 F1–F8，拒绝不可用、被动或不允许左键的技能 |
| learn-skill | 技能 `id` | 按正式分配命令学习或升级技能；拒绝等级、前置或点数不满足的请求 |
| reset-skills | 无 | 清空已分配技能并归还升级所得技能点；不会重置等级或经验 |
| switch-character | 可选 `class`：MPQ `CharStats.class` 原名 | 存活时指定职业或循环下一职业；重置成长状态，保留物品和世界 |
| character-panel | 可选 `open` 布尔值，默认 true | 打开或关闭角色面板，便于结合 `screenshot` 对照职业外观和数值 |
| skill-tree | 可选 `open` 布尔值和 `page` 1–3 | 打开或关闭当前职业技能树并切到指定页，便于结合 `screenshot` 查看布局 |
| skill-picker | 可选 `open`、`right` 布尔值，默认 true | 打开或关闭左／右技能菜单，便于结合 `screenshot` 查看图标与快捷键标签 |
| use | `id` | 正常物品预览和使用，返回 `used`；支持背包回城卷轴，城镇使用拒绝 |
| portal | `revision` | 正常走近当前蓝门；需使用 status 中当前版本，营地返程关闭双端点 |
| kill | `id` | 仅击杀存活且当前屏幕范围内、已激活的指定怪物，玩家须存活；使用正常死亡事件及掉落结算 |
| drop | `id` | 快速击杀当前区域已创建且存活的指定怪物，不要求可见或激活；不推进世界时间，经相同死亡和 MPQ 掉落结算，返回本次地面物品 |
| pickup | `id`，可选 `revision`、`ticks` | 提交正常拾取请求，默认执行一个固定步；ticks 为 1–250，返回 `pickedUp`、`queued` 和实际推进步数 |
| move | `x`、`y` | 校验当前地图可行走坐标，提交正常 MoveTo 并执行一个固定步，不是传送 |
| step | `ticks`，默认 1，范围 1–250 | 同步推进固定步，每步 1/25 秒，包含 AI、伤害、死亡及拾取；不会暂停怪物单独移动玩家 |
| pause / resume | 无 | 暂停／恢复实时模拟；调试暂停独立于游戏菜单暂停 |
| save / load | 无 | 使用启动时 `--save`、否则 `--load`、否则 `saves/quick.d2xsave`；不能通过请求任意指定路径 |
| screenshot | 无 | 保存最近渲染画面到 `artifacts/debug-pipe.png`；命令返回前一已完成帧，立即移动后可在下一请求截取 |
| quit | 无 | 正常退出；若启动指定 `--save`，退出时仍会保存 |

kill 和 drop 都不进行攻击命中／伤害计算，因此用于验证死亡和掉落链，不能作为普通战斗伤害验证。drop 确实杀死怪物并消耗当前掉落随机状态，同一实体不能重复结算；生成物仍落在怪物原位置，远处拾取须正常移动。kill 的屏幕范围按当前 SceneView 投影与背包遮挡判断，不保证像素级遮挡或墙后可见性。没有批量清屏、改随机种子、绕过上限的任意钱包写入、直接造物品或任意代码执行命令。当前品质生成以 TC 叶子和怪物死亡为入口；直接指定物品代码／品质入包会绕过原概率、词缀、失败降级及同局暗金限制，暂不提供。可用 `drop` 生成真实实例，再经正常拾取和库存事务检查。

## 协议和安全

- `\\.\pipe\<name>`，Windows byte-mode named pipe，UTF-8 单行 JSON。请求如 `{"command":"kill","id":1076}`，响应如 `{"ok":true,"command":"kill","killed":1076}`；失败包含 `error`。
- 每次连接一条请求及一条响应，响应换行后客户端关闭。客户端断线或 15 秒超时后回收连接。请求上限 16 KiB，响应上限 4 MiB，单实例连接。
- EXE 不指定管道时无监听；Play.cmd 默认显式传入管道参数，可用 `-NoDebugPipe` 关闭。ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令处理、会话变更和呈现均在游戏主线程，没有后台线程直接修改库存。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由用户授权新增。

## 当前证据与限制

当前源码为格式 v25／规则 v52，旧档不迁移。技能树与女巫技能菜单已构建截图，快捷键已完成存读档冒烟；基础远程攻击和女巫战斗效果待逐项实机验收。v10／规则 v30 的传送现场 `artifacts/waypoint-state-v10.d2xsave` 仅是历史证据，不能按当前格式读取。status 增加 portal、waypoints 和 travelMenu；objects 中 Waypoint 返回 activated 及原 fps。新游戏包括营地全部未激活；travel 自由传送不激活，waypoint 不能绕过解锁。Ctrl+F2 是独立开发目录，不是游戏传送点菜单。原三态动画、首次交互、锁定目的地拒绝及解锁保存恢复曾在 v10 实际验证，本轮未重新运行。

新增 `drop`、`item-move`、`item` 及物品命令回执已完成源码和协议文档。Windows Release 构建后，实际调用 `item` 查询初始手斧、`equip` 卸下和重新装备，以及对当前区域指定怪物 `drop`；后者生成 6 件地面物品、无暂缓原因，已结算数从 0 到 1。拾取、药水使用和头盔胸甲图层仍待定向验收。

在原洞窟地图种子 210、掉落种子 10 下，通过正常移动接近后击杀四个可见普通怪物，得到箭袋 `aqv`（数量 196、等级 2）、金币 5、法力药水及一次 NoDrop。箭袋正常入包保留数量与等级；金币正常拾取后地面实例消失、钱包变为 5、不占背包。重复击杀同 ID 被拒绝；管道保存／恢复后金币 5、箭袋 196、击杀和结算数 4、随机状态保持。现场 `artifacts/gold-pipe-v8.d2xsave`，截图 `artifacts/gold-wallet-v8.png`，均不提交。

上述金币现场使用历史存档 v8／规则 v26，不能按当前 v25／规则 v52 恢复。六件上限、钱包满额部分拾取、跨用户 ACL 拒绝及 Linux 构建尚未实际验证。金币仓库存取／死亡掉金和装备金币加成仍未实现；投掷基础消耗已接源码但未运行验收。当前品质展示实例见 [物品补全](ITEM_COMPLETION.md)。

另有历史 v27 视觉现场 `artifacts/visual-v27.d2xsave`；status 包含 look／routePoints，monsters 包含可用的原 sourceVelocity。view 会改变当前屏幕范围，因此 kill 的可见约束按调试相机计算，但仍要求单位已激活；drop 不受该约束。
