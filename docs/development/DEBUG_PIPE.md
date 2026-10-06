# Windows 调试命令

直接运行 EXE 时，调试管道仅在指定 `--debug-pipe` 时启用，始终启动为运行状态；Play.cmd 默认开启 `d2x-debug`。当前产品只接受联机命令和测试表现 pause/resume，不再提供本地授予／存档／单步玩法入口。本批已 Windows Release 构建并更新运行包；暂停、ESC菜单与库存 UI 输入有有限原服证据，快捷记忆登录尚未复验。

## 联网命令

后续按单位移动、回城门创建、NPC交谈及27种物品／城镇服务命令已Release构建，城镇物品有限原服冒烟通过；战斗／技能／成长command及服务器状态副本已Release并有限原服验证、保存重入通过；现已打包至`dist/current`，含共用UI及本批动作／基础效果。实际范围及未验项见联网模块。

当前源码 在主菜单／登录／服务器选角／大厅／入局等待页／五幕远端场景接收同一个调试管道；无需进入单机场景。网络始终持续收包／心跳；`pause/resume` 仅冻结／恢复客户端表现，`step` 不支持。包内有限冒烟已覆盖双账号营地显示／移动、退局重进，准确范围和 UI 限制见 [联网模块](../modules/NETWORK.md)。

| 命令 | 参数与结果 |
| --- | --- |
| `ui-input` | 复用既有FrameInput诊断格式：x／y逻辑坐标、button=left／right、leftHeld／leftReleased／rightHeld、key、shift／control／focused等；frames可一次排队1–32帧，每绘制帧消费一项。只允许已显示、未测试暂停的局内UI；退局／游戏或区域代次切换／暂停清队列，普通控制器提交原服意图，不直接改权威副本 |
| `pause` / `resume` | 只在显式调试管道启用；pause要求在线ProtocolReady，冻结画面和界面输入。回执presentationPaused／networkRunning=true／serverPaused=false；不暂停原服，恢复采用最新副本，旧动作不重放 |
| `online-status` | 顶层presentationPaused；online.protocol按SID／MCP／game返回包ID、received／sent／unconsumed、逻辑字节数和lastReceived；只读 `online`：stage、error、revision、connectionGeneration、gameGeneration、Realm／角色／游戏列表、load、延迟（首个pong前null）、gameQueuePosition、gameListComplete、world／scene；联网模式的 `status` 是其别名 |
| `online-social` / `online-chat` | 同一完整只读快照的world.social：名册身份及字段可用性、队伍／关系原值、公开位置、聊天原语言nameBytes／textBytes；尚无发送及组队／聊天UI，不能据此认证M4 |
| `online-world` | 只读同一快照：world.units／rooms／equipment／attributes、本人全局 subtile 坐标与当前生命／法力／体力；scene 含原 DS1、原点、候选／地标、碰撞／显示／移动可用性、本人是否绘制与缺外观数量；当前源码units增加nativeMode、direction、actionSkill／actionSkillLevel，区分实际模式、原路径面对方向及当前技能动作 |
| `online-resurrect` | 无参数；要求原服报告死亡。等待DEAD后发送原0x41，重复请求去重；world.respawnRequest记录WaitingForDeath／Sent／Confirmed／TimedOut及sent，确认前不恢复本地资源。Hardcore执行退局 |
| `online-recover-corpse` | unitId为world.corpses及scene.mapTargets中本人的真实可见尸体GUID，自动type0；先按共同路径靠近，再原0x13请求取回。以服务端库存／装备与尸体回包确认，accepted不表示回收完成 |
| `online-items` / `online-ground` | 只读同一完整快照；online.inventory.items含地面和各所有者物品，依mode／owner筛选，不请求服务器刷新；尺寸、数量、耐久、词缀、孔内所有者、decoded／reason及revision见下文 |
| `online-item-action` | action和真实itemId，可选itemRevision／targetRevision；配对动作还需targetId。27种原物品／城镇请求及格子／部位参数见[联网物品操作](#联网物品操作)，accepted仅入队 |
| `online-combat` / `online-skills` | 只读同一快照；combat.skills列MPQ技能名、基础／装备加成／有效等级、innate、左右键／城镇资格；combat.states列原服状态和原单位属性；combat.events为最多256条有序战斗事件，sequence递增，消费者自行检查缺口；0x73首路径点为missileDestination（旧missileOrigin名称已更正），不是飞弹出生点 |
| `online-select-skill` | skillId（0–65535）、hand（left／right，默认right）；当前MPQ及服务端有效等级校验，原0x3C选择，等combat.request.state=Confirmed再施放。不支持带物品GUID的充能技能 |
| `online-cast` | hand；坐标x／y或真实unitId／unitType（默认1）二选一。stationary默认false；单位目标false允许原服靠近，true原地请求。repeat默认false，true发原Hold包一次，调用方负责继续提交／停止，不创建客户端循环。各轴≤50、活动地图及MPQ城镇限制，当前单位目标限PvE敌对怪物／符合技能的尸体；自施技能用本人坐标 |
| `online-attack` | 同cast目标及stationary／repeat，固定左手；先选择MPQ Attack技能并等确认。伤害、追击与命中由原服处理 |
| `online-stop-skill` | 发原0x12停止地狱火状态；停止重复提交Hold请求由调用方负责。这不是全部技能的通用撤销包 |
| `online-learn-skill` | skillId；检查职业、已学前置、等级、属性、MPQ最大等级和原服可用点。原0x3B，等待基础技能等级增加；原服无通用失败包，超时结果未知，不自动重试 |
| `online-spend-attribute` | statId：0力量／1精力／2敏捷／3体力；count：1–100，默认1，需有足够原服属性点。原0x3A打包count-1于高字节，等待对应绝对属性增加 |
| `online-move` | x、y为服务端全局subtile整数（0–65535），run默认true；ProtocolReady、角色存活、目标通过完整玩家尺寸活动碰撞且各轴距本人≤50时接受。最新源码复用共同寻路并分段发0x01／0x03，短段≤18、至少间隔100ms，依原服位置推进；accepted为首段发送成功，不表示已到达 |
| `online-move-to-unit` | unitId、unitType（默认2，支持1／2／5）、run（默认true）；限定当前scene.mapTargets真实单位及各轴50-subtile范围。最新源码远处先分段移动，进入18-subtile范围后发原0x02／0x04跟随单位，不发交谈 |
| `online-use-exit` | unitId为online-world返回的真实type5单位ID，要求原生地图可用／角色存活；发送0x13交互，不接受用LvlWarp类型编号替代单位ID |
| `online-interact` | unitId、unitType（默认2，支持1／2／5）、run（NPC靠近默认true）；目标必须在scene.mapTargets中。地图对象发原0x13；NPC远处先按GUID移动，服务端确认到原交谈距离后才0x13；accepted不代表已操作、交谈打开或换区 |
| `online-town-portal` | 无参数；要求存活／原生地图可移动／非城镇及原服已报告可用卷轴或书技能、右技能。选原右技能并施放，后续world.townPortalPending及实际type2门户回包给出等待／分配状态；不发放卷轴 |
| `online-npc-interact` | unitId、run（默认true）；online-interact的type1入口。只接MPQ允许交谈的实际NPC，远处先靠近；等待scene.npcConversation |
| `online-npc-message` | stringId（0–65535）、可选npcRevision；必须为当前服务端对白列表且未确认，版本不符拒绝。发0x31，acknowledged只表示入队，不是任务成功回执 |
| `online-npc-close` | 无参数；取消尚在靠近的交谈意图，或发0x30关闭当前NPC。取消靠近意图不会传送角色或撤销已提交的服务端寻路 |
| `online-npc-travel` | 无参数；要求当前scene.npcConversation.travelLabel来自瓦瑞夫／马席夫真实身份和任务资格；发0x38 action=0并结束临时交谈，随后查询实际幕／位置 |
| `online-waypoint-travel` / `online-waypoint-close` | travel的level为原区域ID1–136，必须有当前服务端0x63菜单且目的地已解锁；close无参数，可取消当前待确认请求或关闭已确认菜单。发送原0x49，level=0为关闭，不解锁任务或传送点 |
| `online-automap` | 可选visible／large布尔值，visible省略时开关、large省略时保留；只改变显示。scene.automap返回大小／显示、stamps／towns数量及当前连续层revealedCells |
| `online-login` | 必填 account、password；原版文件／认证模式／端口读取 `--online-config` 私有配置，只在 Idle／Failed／Cancelled 接受；自动选择配置中的 Realm |
| `online-register` | account、password（各2–15）；配置和允许阶段同登录，注册成功自动登录；服务端拒绝码在 error 中 |
| `online-create-character` | name（2–15、字母起首，其余字母／连字符／下划线）、classId（0–6，默认0）、hardcore（默认false）；CharacterSelection 接受，固定资料片／非 Ladder，服务器生成初始数据后刷新列表 |
| `online-delete-character` | name 和完全相同的 confirmName；CharacterSelection 接受，必须来自当前列表；不可撤销，成功刷新列表 |
| `online-return-realms` | CharacterSelection 关闭 MCP 并重新取 Realm 列表；保持 RealmSelection 等待显式选择 |
| `online-cancel-list` | ListingGames 取消等待并返回 Lobby，不关闭 MCP，列表完成标记为 false |
| `online-realms` / `online-characters` / `online-games` | 只读当前服务器快照，不发起刷新；未知角色字段为 null |
| `online-select-realm` | name；只在 RealmSelection 接受；首次登录自动选择配置项；切换 Realm 或配置项不存在时等待显式选择 |
| `online-select-character` | name；只在 CharacterSelection 接受，限定可玩的非 Ladder LoD 角色 |
| `online-list-games` | 可选 filter（≤15）；只在 Lobby 发起真实列表请求，filter 为本地名称包含筛选，完成后返回 Lobby；无终止包时超时并清除未完成列表 |
| `online-create-game` | name（≤15）、可选 password（≤15）／description（≤31）／maximumPlayers（1–8，默认4）／levelDifference（0–99，默认4）／difficulty（0普通／1噩梦／2地狱，默认0；按服务器角色进度解锁）；只在 Lobby 接受，成功自动取票入局 |
| `online-join-game` | name、可选 password；只在 Lobby 接受，调用原 MCP 取票接口，UI 的 Join 调用同一接口，可按名字直接加入 |
| `online-leave-game` | 创建／加入／连接游戏／握手阶段取消并重新取服务器角色列表；LoadingGame／ProtocolReady 请求原0x69保存退局；LeavingGame重复请求不重发，退局超时标记保存结果未知 |
| `online-return-characters` | Lobby 返回服务器选角，重新取票 |
| `online-cancel` / `online-logout` | 关闭连接；前者进入 Cancelled，后者清空会话回主菜单 |
| `screenshot` / `quit` | 联网截图使用 path 参数；quit 立即返回 accepted，应用在 LoadingGame／ProtocolReady 先正常退局，等响应／关闭或期限后注销退出；不是保存成功回执 |

源码联机地图诊断另含 `scene.nativeMapReady/nativeMapReason` 及 `world.mapEventSequence/mapEventFirst/mapEventCount`。有序队列在每次 LOADACT 清空，最多保留4096条房间／玩家位置事件；消费者发现缺口后停止原生重建，需重新入局。Trees 已知缺损末组按最新要求作零尺寸兼容，保留14组抽签，诊断为 groups=14／declared=14／zeroFilled=1；原预设地标绑定成功不代表野外重建通过。当前包包含这些字段。

最新源码`scene.effectLimitations`为本幕遇到的未支持技能／弹体客户端程序、发射数量公式及0x73标志说明数组，换幕清空；缺效果不妨碍原服结算。空数组不代表所有技能效果已认证。死亡诊断含`world.dead/deathPhase/deathRevision/respawnRequest/corpses`；corpse记录unitId／owner／owned／position，空间隐藏时position可空，归属等原0x8E解除。type0只开放本人尸体的交互／单位靠近，不能用于任意玩家。这些字段及共用弹体／死亡改动已入包；respawnRequest另含restoredResources位组（生命／法力／耐力）与repositioned，用于确认原服资源恢复和0x15组合。实际有限验证及最新未复验范围见联网模块。

修改命令立即返回 `accepted` 和当时的 `online` 快照，不等待网络完成。后续查询 `online-status`：例如登录完成后为 CharacterSelection，选角完成后为 Lobby；ProtocolReady 只代表入局协议初始化，`worldDisplayAvailable` 由活动地图重建及资源加载决定；还须查看 scene.movementAvailable／playerDisplayed，未匹配时 scene.reason 说明原因。阶段不允许时返回 ok=false；错误回复／超时可从后续状态读取。可附带 connectionGeneration／gameGeneration／areaGeneration／interactionGeneration，代次不符则拒绝迟到命令。当前源码回执另有 uiQueue.inputFrames／itemCommands／waitingItemRequest；world.interactionGeneration、库存与战斗 request.context、control.context 可定位本地意图来源。这些代次不是原协议事务号，旧队列取消不表示撤销原服已执行操作。响应不回显请求、密码、CD key、角色票据或原始世界包。

快照含`control.reason/approaching`、`world.movementRequest/npcRequested/townPortalPending/playerSkills/itemSkillQuantities/rightSkill`、单位`destinationUnit/positionRevision/positionDiscontinuity/actionRevision/pathType/pathSteps/pathDistance/velocityPercent`及`scene.town/townPortalSkills/npcConversation`。单位位置仍是权威坐标，显示层连续路径不写回快照；NPC行走pathDistance不是生命比例。新增源码`control.navigation`为null或包含goal（最终全局坐标）、segment（当前坐标短段）与target（单位目标）的只读对象，结束推进后为null；最终GUID靠近请求仍见world.movementRequest。当前包包含分段修正及诊断；最新鼠标交互修正按用户要求未运行复验。交谈投影含source／revision／speaker／travelLabel和messages的stringId／menu／text／acknowledged，文字读当前MPQ；并非离线NPC服务投影。按GUID查询mapTargets后提交命令，无需手造NPC或门户ID。

本机测试账号为 `bomb / 1qaz2wsx`、`bomb2 / 1qaz2wsx`，用户授权记入文档，仅用于测试。按以下顺序手工调用：

```powershell
.\build\bin\d2x.exe --mpq assets/mpq2 --debug-pipe d2x-online --online-config online.local.json
# 另一个终端；每一步通过 online-status 等待相应阶段，再提交下一步。
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-login -Arguments @{
    account = 'bomb'
    password = '1qaz2wsx'
}
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-status
$characters = (.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-characters).online.characters
# name 使用服务器返回的原值；选择非 Ladder 资料片角色。
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-select-character -Arguments @{name=$characters[0].name}
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-status
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-create-game -Arguments @{name='d2x-room';maximumPlayers=4}
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-status
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-leave-game
```

新增入口示例（每次操作后用 online-status 等待完成，不连发）：

```powershell
# 已登录到 CharacterSelection 时，新建独立测试角色。
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-create-character -Arguments @{name='NetSorceress';classId=1;hardcore=$false}
# 选角后到 Lobby，另一个账号建房；空列表无终止包时可取消后按名加入。
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-list-games
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-cancel-list
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-join-game -Arguments @{name='d2x-room'}
# 仅在 CharacterSelection 且确实要永久删除独立测试角色时执行，勿删除 aaa／bbb。
.\scripts\Send-D2XCommand.ps1 -PipeName d2x-online -Command online-delete-character -Arguments @{name='NetSorceress';confirmName='NetSorceress'}
```

入局后查看 `online.scene.available/movementAvailable/playerDisplayed/nativeMapReady` 和 `online.world.playerPosition`。命令坐标为服务端全局subtile；局部坐标加当前 `scene.origin`，原点可随活动房间变化。accepted后再查询位置。五幕共用原生地图入口和原出口交互；实际连服冒烟范围见联网模块。

`scene.area`表示当前玩家房间所属区域；`layoutOrigin`为生成布局原点，`layoutMatched`表示原生房间锚点已通过校验；快照 `origin/width/height`表示当前可显示／导航的活动范围。`nativeMapReason/mapErrors`说明失败，`cachedAreas`列已接入区域。世界返回 `mapEventSequence/mapEventFirst/mapEventCount`，房间 `assignmentRevision`保留首次0x07次序。五幕事件历史失去连续性必须重新入局。

既有资源工具诊断（开发输出，不是游戏导航接口）：

```text
d2x_assets <MPQ目录> native-layout <幕:1-5> <地图种子> [难度:0-2]
d2x_assets <MPQ目录> native-outdoor <区域> <地图种子> [难度]
d2x_assets <MPQ目录> native-room <区域> <地图种子> <tileX> <tileY> [难度]
d2x_assets <MPQ目录> native-map <区域> <地图种子> <难度> [原版导出.json|complete]
```

native-map输入原版JSON时仅读取调用事件，输出房间／近邻、选定DT1文件和记录、单位／Pops及完整碰撞；complete使用同一核心完整准备连续组件，输出当前区域的碰撞、边界、原出口和单位。native-room仅生成孤立房间，不认证激活顺序或共享边缘；布局工具本身不认证完整地形。第一幕此前122组、第二至第五幕本批364组实际原版对照见[实施计划](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。

不要附带 --class／--load／--hidden／--level 等会直入单机的选项。进入既有离线角色选择期间，该旧选择器尚未轮询管道；进入单机场景后管道由原单机 command 接管，online-* 不在那里启动另一个会话。

`hireling` 返回原类型、来源难度、技能基础／有效等级、原模式、当前动作／技能及天然光环；`types=$true` 另返回当前 MPQ 全部资料片类型及分段行，包括技能权重和成长。可传 `npc` 打开正式雇佣服务，继续传 `slot` 雇佣，仍要求 NPC 可访问并按原费用扣金。

### 联网物品操作

当前源码已通过Windows Release、打包与有限原服冒烟；实际范围见[联网模块](../modules/NETWORK.md#城镇物品批次冒烟2026-10-06)。先用online-items查询实际GUID、revision与decoded；mode为0存储、1装备、2腰带、3地面、4Cursor、5掉落过渡、6孔内。page是原InvPage+1：1背包、4方块、5箱子。本人所有权为ownerType=0且owner等于load.playerUnitId；孔内子项ownerType=4、owner为宿主物品GUID；货架ownerType=1且owner为当前NPC。online-ground也返回完整快照，请按mode=3筛选。

所有修改用`online-item-action`；itemId来自本局服务器，不沿用单机ID。itemRevision／targetRevision可选，省略或0采用提交时版本；建议传查询版本避免操作旧物品。connectionGeneration／gameGeneration／areaGeneration沿用联网通用代次校验。

| action | 参数与范围 |
| --- | --- |
| pickup | itemId；toCursor默认false，true拾到Cursor；地面物品、空Cursor及原50单位距离，服务端自行接近 |
| take | itemId；从本人背包、腰带、当前装备或已确认打开的箱子／方块拿到空Cursor |
| place | Cursor itemId、x／y从0计；page为native编号0背包／3方块／4箱子，默认0；后两者需服务器确认打开，按当前MPQ尺寸与空格校验 |
| drop | Cursor itemId；最终地面坐标由原服决定 |
| equip | Cursor itemId、body=1–10；原head／neck／tors／rarm／larm／rrin／lrin／belt／feet／glov；自动按当前槽和两手武器选原交换包 |
| unequip | 当前装备itemId；空Cursor，body取服务器该物品部位 |
| swap | Cursor itemId、targetId、x／y；当前可访问的原格子交换 |
| use | 背包或腰带itemId；mercenary默认false，true仅腰带；使用方块暂拒绝，回城卷轴／书用online-town-portal |
| belt-place | Cursor itemId、beltSlot从0计；不得超当前MPQ腰带容量 |
| belt-swap | Cursor itemId、腰带targetId；槽位取目标服务器位置 |
| stack | 源itemId、targetId；原兼容堆叠请求，不支持自选拆分数量 |
| book | 卷轴itemId、书targetId；必须为当前MPQ Books原配对 |
| socket | Cursor填充物itemId、宿主targetId；鉴定、归属和已知空孔校验 |
| identify | 背包鉴定卷轴／书itemId、未鉴定targetId；目标限背包／装备，Cursor须空 |
| switch-weapons | 无itemId；空Cursor，0x97确认后更新weaponSet |
| cube-open | 本人背包方块itemId；按当前MPQ pSpell=7提交使用，0x77确认后开放方块格 |
| storage-close | 无itemId；关闭当前／待确认箱子或方块，可取消Pending；发送后标SentNoAck |
| transmute | 无itemId；已确认方块、空Cursor且有材料；配方与产物由原服决定 |
| gold-deposit / gold-withdraw | amount为正整数；已确认箱子、空Cursor；复验已知钱包／箱子余额，最终上限和金额由原服决定 |
| gold-drop | amount为正整数；空Cursor，服务器决定金币落点 |
| trade-open | 无itemId；当前NPC交谈且空Cursor，生成普通货架 |
| buy | 当前货架itemId；普通单件购买，原服计算价格／安置；不开放多买或赌博 |
| sell | 本人背包itemId；当前货架／交谈及空Cursor，非任务物品，原服定价 |
| repair | 本人可访问物品或装备itemId；当前原铁匠货架／交谈，最终资格由原服决定 |
| repair-all | 无itemId；当前原铁匠货架／交谈及空Cursor |
| identify-all | 无itemId；当前五幕凯恩交谈及空Cursor；任务资格／费用／效果由原服决定 |

`online.inventory`含revision、gameGeneration、columns／rows、stashColumns／stashRows、cubeColumns／cubeRows、beltSlots、cursor、weaponSet、items和request。storage列kind／requested／source／requestedSource／revision；shopRequested／shopSource绑定货架；tradeResult列原result／flags／itemId／gold／revision。物品含基础name／artKey、品质／词缀、位置／所有者、数量／耐久、防御／金币、孔数及统计列表；0x3E baseStats保留服务器单位。未知／截断数据decoded=false并给出reason，不允许操作。

accepted仅表示请求已排队。request.sequence区分连续操作；Pending等待相关回包，Updated需再查位置／数量／余额；TimedOut结果未知，Interrupted为死亡／换幕／退局，Rejected为NPC明确失败（原码在tradeResult／online.error），SentNoAck为存储关闭已发送。买卖／维修／批量鉴定等待0x2A；初步物品变化不提前结束请求。待请求结束后再提交下一项，不自动重发；storage-close例外允许取消。拾取／合成应同时核对所有权、原材料、产物及world.attributes，不能把地面消失或Updated直接当成功。箱子先从scene.mapTargets查询interaction=stash的真实单位，再用online-interact；商店先用online-npc-interact并等npcConversation。完整背包／货架UI、玩家交易、赌博／多买、佣兵装备仍待接。

```powershell
# 查看原服物品；返回全部物品，按 mode／owner 筛选
.\scripts\Send-D2XCommand.ps1 online-items
# 下面的 ID／revision 均替换为上述查询值，每一步后重新查询实际结果
.\scripts\Send-D2XCommand.ps1 online-item-action -Arguments @{action='take';itemId=123;itemRevision=7}
.\scripts\Send-D2XCommand.ps1 online-items
.\scripts\Send-D2XCommand.ps1 online-item-action -Arguments @{action='place';itemId=123;x=0;y=0}
.\scripts\Send-D2XCommand.ps1 online-item-action -Arguments @{action='switch-weapons'}
```

## 启动与调用

```powershell
.\Play.cmd -PipeName d2x-debug
# 登录一次并记忆账号后，可自动完成正常认证、选角和一次建房／加入：
.\Play.cmd -PipeName d2x-debug -OnlineCharacter <角色名> -OnlineCreateGame <房间名>
.\Play.cmd -PipeName d2x-debug -OnlineCharacter <角色名> -OnlineJoinGame <房间名>
.\scripts\Send-D2XCommand.ps1 -Command online-status
.\scripts\Send-D2XCommand.ps1 -Command pause
.\scripts\Send-D2XCommand.ps1 -Command screenshot -Arguments @{ path='artifacts/online-capture.png' }
.\scripts\Send-D2XCommand.ps1 -Command resume
.\scripts\Send-D2XCommand.ps1 -Command online-leave-game
.\scripts\Send-D2XCommand.ps1 -Command quit
```

自定义管道用 `-PipeName`；`ok=false`、连接失败或超时会抛异常。默认10000毫秒，`-TimeoutMs`支持100–60000。accepted只表示原请求被接受／入队，须读取原服状态确认。暂停时online-status继续看到当前副本，截图保留冻结画面；角色仍可能在原服移动、受伤或死亡。

本地 `step/save/load/travel/item-spawn/grant-*` 等命令实现保留在历史源码中，但不进入当前产品目标，也不能作为联机验收。旧本地启动参数明确拒绝；用户D2S、MPQ、旧包和mvp保留。已有资源工具的只读开发能力见地图／资源模块。

## 协议和安全

- `\\.\pipe\<name>`，Windows byte-mode named pipe，UTF-8 单行 JSON。请求如 `{"command":"online-status"}`，响应包含 `ok` 与 `online`；失败包含 `error`。
- 每次连接一条请求及一条响应，响应换行后客户端关闭。客户端断线或 15 秒超时后回收连接。请求上限 16 KiB，响应上限 4 MiB，单实例连接。
- EXE 不指定管道时无监听；Play.cmd 默认显式传入管道参数，可用 `-NoDebugPipe` 关闭。ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令入口和呈现在客户端线程；RealmSession 的后台 worker 持锁消费原服回包并更新权威副本，界面只读取客户端快照。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由用户授权新增。

## 当前证据与限制

本批已 Windows Release 构建、打包及有限原服冒烟，观察测试暂停期间冻结画面与持续收包、ESC菜单、背包拖放、箱子连续转移、保存重入及加载超时恢复。online.inventory.reason 返回适配器拒绝原因；waypointRequested／waypointSource 区分请求与确认，lateWaypointReplies 计迟到回复。快捷记忆登录、迟到回复分支和完整鼠标／多人仍未认证；历史 command／截图冒烟不认证完整新调用链。完整玩法与运行包差异见[联网模块](../modules/NETWORK.md)和[项目基线](../../BASELINE.md)。跨用户 ACL 拒绝与 Linux 实际运行尚未完整验证；本机管道不能代替 D2GS 协议。
