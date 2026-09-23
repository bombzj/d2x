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

## 命令

| command | Arguments | 行为 |
| --- | --- | --- |
| status | 无 | 玩家坐标、生命、钱包、区域、击杀、已结算数、掉落随机状态、调试暂停状态 |
| monsters | `visible`，默认 true | 当前区域已创建怪物 ID、真实身份、等级类别、生命、坐标、屏幕内／激活状态；不含尚未创建计划 |
| ground | 无 | 当前区域地面物品 ID、版本、代码、数量、物品等级、坐标 |
| inventory | 无 | 所有角色容器内物品及钱包；包含背包、腰带、装备和私人箱，不改变箱子访问权 |
| objects | 无 | 当前场景对象名称、原内容键、位置、可绘制标志，以及入口／边界预设信息 |
| exits | 无 | 当前出口名称、slot、访问坐标与启用状态 |
| view | `x`、`y` | 只移动相机并查询附近墙格键值与隐藏标志；下一次模拟步恢复跟随，不修改玩家位置 |
| equip | `id`，可选 `slot` | 正式预览及提交 EquipItem；如 rarm/larm，省略 slot 则卸下入包；不支持腰带专用事务 |
| travel | `level` | 调试自由传到已实现地图，优先落在原传送点旁；不检查或写入激活记录 |
| waypoint | `id`、`level` | 正常传送点命令，id 是当前区域源点；校验距离／通路、两端激活及玩家状态，拒绝时返回错误 |
| interact | `id` | 正常走近对象交互；传送点首次激活、再次开菜单，远处需继续 step |
| use | `id` | 正常物品预览和使用；支持背包回城卷轴，城镇使用拒绝 |
| portal | `revision` | 正常走近当前蓝门；需使用 status 中当前版本，营地返程关闭双端点 |
| kill | `id` | 仅击杀存活且当前屏幕范围内、已激活的指定怪物，玩家须存活；使用正常死亡事件及掉落结算 |
| pickup | `id`，可选 `revision` | 提交正常拾取请求，执行一个固定步；queued 不表示已经拾取，需继续推进并查询 |
| move | `x`、`y` | 校验当前地图可行走坐标，提交正常 MoveTo 并执行一个固定步，不是传送 |
| step | `ticks`，默认 1，范围 1–250 | 同步推进固定步，每步 1/25 秒，包含 AI、伤害、死亡及拾取；不会暂停怪物单独移动玩家 |
| pause / resume | 无 | 暂停／恢复实时模拟；调试暂停独立于游戏菜单暂停 |
| save / load | 无 | 使用启动时 `--save`、否则 `--load`、否则 `saves/quick.d2xsave`；不能通过请求任意指定路径 |
| screenshot | 无 | 保存最近渲染画面到 `artifacts/debug-pipe.png`；命令返回前一已完成帧，立即移动后可在下一请求截取 |
| quit | 无 | 正常退出；若启动指定 `--save`，退出时仍会保存 |

kill 不进行攻击命中／伤害计算，因此用于验证死亡和掉落链，不能作为普通战斗伤害验证。屏幕范围按当前 SceneView 投影与背包遮挡判断，不保证像素级遮挡或墙后可见性。没有批量清屏、改随机种子、改钱包、直接造物品或任意代码执行命令。

## 协议和安全

- `\\.\pipe\<name>`，Windows byte-mode named pipe，UTF-8 单行 JSON。请求如 `{"command":"kill","id":1076}`，响应如 `{"ok":true,"command":"kill","killed":1076}`；失败包含 `error`。
- 每次连接一条请求及一条响应，响应换行后客户端关闭。客户端断线或 15 秒超时后回收连接。请求上限 16 KiB，响应上限 4 MiB，单实例连接。
- EXE 不指定管道时无监听；Play.cmd 默认显式传入管道参数，可用 `-NoDebugPipe` 关闭。ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令处理、会话变更和呈现均在游戏主线程，没有后台线程直接修改库存。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由本次用户授权新增。

## 当前证据与限制

当前为格式 v10／规则 v30，旧档不迁移。status 增加 portal、waypoints 和 travelMenu；objects 中 Waypoint 返回 activated 及原 fps。新游戏包括营地全部未激活；travel 自由传送不激活，waypoint 不能绕过解锁。F2 是独立开发目录，不是游戏传送点菜单。原三态动画、首次交互、锁定目的地拒绝及解锁保存恢复已实际验证，现场 artifacts/waypoint-state-v10.d2xsave。

在原洞窟地图种子 210、掉落种子 10 下，通过正常移动接近后击杀四个可见普通怪物，得到箭袋 `aqv`（数量 196、等级 2）、金币 5、法力药水及一次 NoDrop。箭袋正常入包保留数量与等级；金币正常拾取后地面实例消失、钱包变为 5、不占背包。重复击杀同 ID 被拒绝；管道保存／恢复后金币 5、箭袋 196、击杀和结算数 4、随机状态保持。现场 `artifacts/gold-pipe-v8.d2xsave`，截图 `artifacts/gold-wallet-v8.png`，均不提交。

当前存档格式 v8、规则 v26；旧 v7 明确拒绝。六件上限、钱包满额部分拾取、跨用户 ACL 拒绝及 Linux 构建尚未实际验证。金币仓库存取／死亡掉金、装备金币加成、投掷武器数量和完整品质实例尚未实现。

上述金币现场为历史 v26 验证。当前修复已升级规则 v27，旧规则现场拒绝；status 追加 look／routePoints，monsters 追加可用的原 sourceVelocity。新的视觉验证现场为 artifacts/visual-v27.d2xsave。view 会改变当前屏幕范围，因此 kill 的可见约束按调试相机计算，但仍要求单位已激活。