# Windows 调试命令

调试管道仅在显式指定 `--debug-pipe` 时启用，默认暂停模拟。它允许改变当前游戏，请使用单独存档，不要对重要现场开启。

## 启动

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
- 默认无监听；启用后 ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令处理、会话变更和呈现均在游戏主线程，没有后台线程直接修改库存。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由本次用户授权新增。

## 当前证据与限制

在原洞窟地图种子 210、掉落种子 10 下，通过正常移动接近后击杀四个可见普通怪物，得到箭袋 `aqv`（数量 196、等级 2）、金币 5、法力药水及一次 NoDrop。箭袋正常入包保留数量与等级；金币正常拾取后地面实例消失、钱包变为 5、不占背包。重复击杀同 ID 被拒绝；管道保存／恢复后金币 5、箭袋 196、击杀和结算数 4、随机状态保持。现场 `artifacts/gold-pipe-v8.d2xsave`，截图 `artifacts/gold-wallet-v8.png`，均不提交。

当前存档格式 v8、规则 v26；旧 v7 明确拒绝。六件上限、钱包满额部分拾取、跨用户 ACL 拒绝及 Linux 构建尚未实际验证。金币仓库存取／死亡掉金、装备金币加成、投掷武器数量和完整品质实例尚未实现。