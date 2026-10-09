# Windows调试管道

直接运行EXE只在显式--debug-pipe时启用，Play.cmd默认d2x-debug。原服和Single Player均使用同一RealmSession，因此共用online-*、UI输入、截图、状态、quit及表现pause/resume命令。命令名称保留online不表示只允许TCP。另有显式嵌入宿主管理能力，执行端位于服务端，管道不直接读写D2S或客户端副本；原服连接拒绝这些管理命令。自研暂未实现的操作不会产生成功结算。构建／包状态见[基线](../../BASELINE.md)。

Single Player的online-status仍只读原协议OnlineView；server-status单独返回宿主诊断身份和tick，不能作为客户端世界同步旁路。ui-input沿现有32帧队列和SceneController，F11／Ctrl+F11与pipe save／load共用AdminRequest管理入口。单机ESC／失焦暂停权威；应用将显式调试暂停一并用于单机宿主，原服pause/resume仍只影响表现。入局／退局清旧UI帧。
## 启动与调用

```powershell
.\Play.cmd -PipeName d2x-debug
# UI登录记忆后正常认证、选角及一次随机普通建房：
.\Play.cmd -PipeName d2x-debug -OnlinePlay <角色名>
.\scripts\Send-D2XCommand.ps1 -Command online-status
.\scripts\Send-D2XCommand.ps1 -Command pause
.\scripts\Send-D2XCommand.ps1 -Command screenshot -Arguments @{path='artifacts/capture.png'}
.\scripts\Send-D2XCommand.ps1 -Command resume
.\scripts\Send-D2XCommand.ps1 -Command online-leave-game
```

自定义管道用-PipeName，超时-TimeoutMs范围100–60000、默认10000。脚本遇ok=false／连接失败／超时抛异常。登录／建房用UI、快捷参数或下面原命令，每一步查询阶段再继续；不能连发非幂等请求。测试服账号／端口只维护在[本机部署](../architecture/MULTIPLAYER.md#本机部署与验证入口)。

## 嵌入宿主管理命令

当前Windows包已通过现有脚本有限运行目录、save／load／step／grant-experience／grant-gold，以及伤害、物品／怪物生成和怪物击杀管理路径；具体证据及未运行边界见基线。JSON只在app/debug解析一次；宿主收到类型化操作与GameHandle／PlayerId绑定。管理调用在当前宿主调度线程执行，网络worker仍只经字节队列访问服务端。失败返回ok=false和明确status，不以HTTP式私有ACK修改原MCP／D2GS。

| 命令 | 参数和当前结果 |
| --- | --- |
| `server-snapshot` | 同一调度tick的权威人物、资源／成长／库存revision、物品位置、当前区域怪物及AI、技能动作、待释放数量、弹体、伤害队列、换区和已准备区域／出口。limit默认128、范围1–256；Count是过滤前总数，列表达到limit时不能视为完整。可给全局x／y，只读计算当前区域路径（最多1024点），不移动角色。monsters.rules列AI家族／searchDistance／aip、再生／Threat／速度／碰撞／格挡及动作／技能；componentCounts为MPQ十六槽选择数，componentVariants为当前区域该类型池的组合数（0表示原单体路径）；运行列components为实际十六槽选择，另含盾牌、巢生计数、蛛网和中断 |
| `server-events` | 返回已提交领域事实及命令入队／执行记录；since／commandSince是各自观察序号，默认0，limit同上。事件环1024条、命令环512条，first／last／gap表示覆盖缺口。记录不被网络发送／ACK清除，不代表客户端已收到；原回包另查online-*。事件value按type解释：life／mana为256固定点，attack为技能ID，hit为原比例生命（人物100、怪物128；怪物0C编码另减一），character为经验且secondary为等级，travel为源／目标区域 |
| `server-pause`／`server-resume`／`server-auto-pause` | 对选中玩家所在整个实例设置调试时钟覆盖：pause保留当前行动并停止推进；原协议命令仍可入队，在step执行。resume连续推进；这两者显式覆盖失焦／ESC自动暂停，不冻结客户端表现。auto-pause解除覆盖，恢复应用自动暂停策略；若恢复到暂停会沿原规则取消行动。所有参与者共用时钟，仅本机宿主管理可调用 |
| `refill-resources` | 经人物事务恢复当前玩家生命／法力／体力至派生最大值，并发送原人物增量；只允许存活、已入局角色，不承担复活 |
| `server-status` | 只读phase、tick、paused、hostSlot／hostGeneration／hostPlayer、实际command序号／结果、最近分派和失败、characterIssues；command是最新诊断，不能作为可靠事务回执 |
| `server-protocol` | 返回全部C2S／S2C／MCP和4F／38子命令目录；实现状态、领域、收发计数、queued／stub／rejected／malformed。入场编码标admission-only，不代表完整玩法 |
| `server-commands` | 当前管理命令、参数类别及implemented标志 |
| `server-systems` | 28个内核目录项的name／phase／scope／lastStep；执行范围以各项scope和内核子系统文档为准。lastStep=null表示未调度或没有固定步入口，不表示实现；详见[内核子系统](../modules/SERVER_SYSTEMS.md) |
| `save` | 无参数；从服务端导出当前角色，校验租约版本并原子保存／备份。applied表示保存已完成，失败保留实例和租约 |
| `load` | 无参数；准备暂停候选实例，成功后经原69／MCP／D2GS离局重入；applied仅表示准备成功且原离局已排队。最后是否入局仍查online-status，候选复用且保留当前难度 |
| `cancel-load` | 原离局尚未接受时释放候选；已经离局后须关闭宿主或完成重新入局 |
| `step` | frames默认1，范围1–250；只允许已入局且权威paused的实例，以1/25秒固定步推进，返回更新后的tick。普通pause会清路径和待执行移动，step不恢复被清除的路径；server-pause保留行动，也不推进原服 |
| `grant-experience` | amount为正的有符号整数；宿主progression授予、封顶／升级／余点事务，原包更新同一客户端；暂停时可用，原服拒绝 |
| `grant-gold` | amount有符号整数；通过人物事务授予金币，复验钱包上限 |
| `monster-spawn` | code为当前MPQ monstats身份，x／y为当前区域全局subtile。复用自然人口的怪物准入／数值／动作准备，再交population／monsters；返回entityId。rank默认normal，可选champion／unique／boss／superunique；superunique须提供当前SuperUniques精确身份superUnique，且code必须匹配原Class。仆从只由自然领队人口生成，不提供无领队调试替身。不接受level覆盖，等级随区域／难度，不支持身份、城镇、碰撞和容量明确拒绝 |
| `item-spawn` | code、level可选及世界x/y；quality默认normal，可选magic／rare／unique／set／superior／inferior按当前MPQ生成，sockets可选0..6并校验原上限；durability可显式指定0..实际最大耐久。仅item-spawn接受这些项，缺生成规则明确拒绝；走统一地面安装入口 |
| `player-damage` | amount为非负整数生命点，转换为原固定点后走人物伤害事务及正常死亡结算 |
| `grant-shrine`、`grant-hireling` | code，level可选；类型化stub |
| `monster-damage`、`monster-kill` | id为当前区域存活怪物；damage另需amount正整数生命点，kill扣除其剩余生命。走monsters正常伤害事实，后续固定步执行死亡、经验、掉落及任务统计，不直接改任务或客户端 |
| `travel` | level；类型化stub |
| `unlock-waypoints`、`reset-attributes`、`reset-skills` | 无参数；类型化stub |

查询及修改命令可附hostSlot／hostGeneration／hostPlayer，取server-status；三个字段须同时指定；不匹配返回invalid-target。未提供时由应用绑定当前宿主角色。这些不是online.gameGeneration，不能互换。save／load不接受path覆盖，固定使用服务器持有租约的角色文件；其他角色加载使用局前入口。stub只验证参数形状，尚未承诺对应资源或玩法资格。

```powershell
.\scripts\Send-D2XCommand.ps1 -Command server-protocol
.\scripts\Send-D2XCommand.ps1 -Command server-systems
.\scripts\Send-D2XCommand.ps1 -Command save
.\scripts\Send-D2XCommand.ps1 -Command pause
# 查询server-status确认paused=true后再单步：
.\scripts\Send-D2XCommand.ps1 -Command step -Arguments @{frames=1}
.\scripts\Send-D2XCommand.ps1 -Command resume
```

旧普通玩法调试入口继续使用下面的online-move／online-item-action／online-cast等原协议命令，不额外恢复一套能直接改客户端状态的move／pickup／equip。旧命令的宽松拼写别名没有恢复。协议与扩展约定见[服务端协议](../modules/SERVER_PROTOCOL.md)。

## 联网命令

| 命令 | 参数与结果 |
| --- | --- |
| `ui-input` | 复用既有FrameInput诊断格式：x／y逻辑坐标、button=left／right、leftHeld／leftReleased／rightHeld、key、shift／control／focused等；frames可一次排队1–32帧，每绘制帧消费一项。聊天用key=enter／escape／message-log（或m）、entryText可打印ASCII、backspace、delete／left／right／home／end编辑；日志支持up／down／page-up／page-down及wheel。允许局前页面及已显示、未测试暂停的局内UI；游戏或区域代次切换／暂停清队列，普通控制器提交原服意图，不直接改权威副本 |
| `pause` / `resume` | 只在显式调试管道启用；pause要求在线ProtocolReady，冻结画面和界面输入。回执presentationPaused／networkRunning=true；不暂停原服。Single Player在下一宿主调度帧应用权威暂停，以server-status.paused为准；恢复采用最新副本，旧动作不重放 |
| `online-status` | 顶层presentationPaused；online.protocol按SID／MCP／game返回包ID、received／sent／unconsumed、逻辑字节数和lastReceived；只读 `online`：stage、error、revision、connectionGeneration、gameGeneration、Realm／角色／游戏列表、load、延迟（首个pong前null）、gameQueuePosition、gameListComplete、world／scene；联网模式的 `status` 是其别名 |
| `online-social` / `online-chat` | 同一完整只读快照的world.social：名册身份及字段可用性、队伍／关系原值、公开位置、聊天原语言nameBytes／textBytes；只读、不刷新。共享界面就绪后额外chatUi返回ready／inputOpen／logOpen／draft／scroll／rows／unavailableMessages／reason，只有表现和草稿，不是发送回执。聊天新UI尚未运行认证，组队未接，不能据此认证M4 |
| `online-send-chat` | message为1–255字节可打印ASCII且不全为空格；ProtocolReady及当前游戏身份有效时发送局内普通广播0x15。accepted只代表入队，双方消息取实际0x26及chatSequence，不插本地回显／自动重试；不支持私聊、表情、中文编码或BNCS频道命令。已入当前运行包，双账号原服普通广播及M日志有限观察见联网交付记录 |
| `online-trade-respond` | accept布尔值、revision为当前world.playerTrade.revision；true接受邀请（button3），false拒绝／取消（button2）。accepted仅表示发送；真实身份等0x78，完成等0x77/13。 |
| `online-trade-offer` | action为agree／revoke／gold，revision绑定当前交易，gold另需amount 0–INT32_MAX；原button4／7／8。world.playerTrade返回ownGold／peerGold、ownAgreed（请求已发）／peerAgreed、agreementLocked及response；最终交换只由原服确认。 |
| `online-world` | 只读同一快照：world.units／rooms／equipment／attributes、本人全局 subtile 坐标与当前生命／法力／体力；scene 含原 DS1、原点、候选／地标、碰撞／显示／移动可用性、本人是否绘制与缺外观数量；当前源码units增加nativeMode、direction、actionSkill／actionSkillLevel，区分实际模式、原路径面对方向及当前技能动作 |
| `online-resurrect` | 无参数；要求原服报告死亡。等待DEAD后发送原0x41，重复请求去重；world.respawnRequest记录WaitingForDeath／Sent／Confirmed／TimedOut及sent，确认前不恢复本地资源。Hardcore执行退局 |
| `online-recover-corpse` | unitId为world.corpses及scene.mapTargets中本人的真实可见尸体GUID，自动type0；先按共同路径靠近，再原0x13请求取回。以服务端库存／装备与尸体回包确认，accepted不表示回收完成 |
| `online-items` / `online-ground` | 只读同一完整快照；online.inventory.items含地面和各所有者物品，依mode／owner筛选，不请求服务器刷新；尺寸、数量、耐久、词缀、孔内所有者、decoded／reason及revision见下文 |
| `online-item-action` | action和真实itemId，可选itemRevision／targetRevision；配对动作还需targetId。27种原物品／城镇请求及格子／部位参数见[联网物品操作](#联网物品操作)，accepted仅入队 |
| `online-item-quote` | action=buy／sell／repair／repair-all；前三者需本局itemId，可选itemRevision。返回known及price，缺数据为null；复用UI报价，不发送询价包或推进待发库存操作。赌博购买报价取当前真实货架模式 |
| `online-combat` / `online-skills` | 只读同一快照；combat.skills列MPQ技能名、基础／装备加成／有效等级、innate、左右键／城镇资格；combat.states列原服状态和原单位属性；world.itemTargetingSource为原3F来源（null表示无准备，不表示鉴定成功），itemSkillQuantities为原物品技能数量；combat.events为最多256条有序战斗事件，sequence递增，消费者自行检查缺口；0x73首路径点为missileDestination（旧missileOrigin名称已更正），不是飞弹出生点 |
| `online-select-skill` | skillId（0–65535）、hand（left／right，默认right）；可选ownerId为已装备物品原GUID，省略为普通技能源FFFFFFFF；当前MPQ、原等级或已解码充能校验，原0x3C选择，等combat.request.state=Confirmed再施放 |
| `online-bind-hotkey` | slot（0–15）、skillId、hand及可选ownerId；校验原普通／充能来源，原0x51保存绑定，无即时ACK，request为SentNoAck；重入world.skillHotkeys读取0x7B。UI使用F1–F8 |
| `online-cast` | hand；坐标x／y或真实unitId／unitType（默认1）二选一。stationary默认false；单位目标false允许原服靠近，true原地请求。repeat默认false，true发原Hold包一次，调用方负责继续提交／停止，不创建客户端循环。各轴≤50、活动地图及MPQ城镇限制，单位目标按MPQ资格区分PvE敌怪／尸体、Enchant友方、Unsummon本人7A／PetType许可的召唤物及Telekinesis物件／物品；自施技能用本人坐标 |
| `online-attack` | 同cast目标及stationary／repeat，固定左手；先选择MPQ Attack技能并等确认。伤害、追击与命中由原服处理 |
| `online-stop-skill` | 发原0x12停止地狱火状态；停止重复提交Hold请求由调用方负责。这不是全部技能的通用撤销包 |
| `online-learn-skill` | skillId；检查职业、已学前置、等级、属性、MPQ最大等级和原服可用点。原0x3B，等待基础技能等级增加；原服无通用失败包，超时结果未知，不自动重试 |
| `online-spend-attribute` | statId：0力量／1精力／2敏捷／3体力；count：1–100，默认1，需有足够原服属性点。原0x3A打包count-1于高字节，等待对应绝对属性增加 |
| `online-move` | x、y为服务端全局subtile整数（0–65535），run默认true；ProtocolReady、角色存活、目标属于已加载原地图且各轴距本人采样≤50时接受。直接发原0x01／0x03目标，至少间隔100ms，不切18格短段等待回包；可达点／障碍由原服处理。accepted表示命令已发送，不表示已到达；鼠标使用同帧显示坐标作投影范围提示，原服仍按真实位置校验50格范围 |
| `online-move-to-unit` | unitId、unitType（默认2，支持1／2／5）、run（默认true）；限定当前scene.mapTargets真实单位及各轴50-subtile范围，直接发原0x02／0x04跟随真实GUID，不发交谈 |
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
| `online-create-character` | name（1–15个ASCII字母／数字／连字符／下划线；最终资格由服务端判断，自研暂不接受下划线）、classId（0–6，默认0）、hardcore（默认false）；CharacterSelection 接受，固定资料片／非 Ladder，服务器生成初始数据后刷新列表 |
| `online-delete-character` | name 和完全相同的 confirmName；CharacterSelection 接受，必须来自当前列表；不可撤销，成功刷新列表 |
| `online-return-realms` | CharacterSelection 关闭 MCP 并重新取 Realm 列表；保持 RealmSelection 等待显式选择 |
| `online-cancel-list` | ListingGames 取消等待并返回 Lobby，不关闭 MCP，列表完成标记为 false |
| `online-realms` / `online-characters` / `online-games` | 只读当前服务器快照，不发起刷新；未知角色字段为 null |
| `online-select-realm` | name；只在 RealmSelection 接受；首次登录自动选择配置项；切换 Realm 或配置项不存在时等待显式选择 |
| `online-select-character` | name；只在 CharacterSelection 接受，限定可玩的非 Ladder LoD 角色 |
| `online-list-games` | 可选 filter（≤15）；Lobby／ListingGames 发起或替换真实列表请求，filter 为本地名称包含筛选，完成后返回 Lobby；无终止包时超时保留已收到列表，并标记不完整 |
| `online-create-game` | name（≤15）、可选 password（≤15）／description（≤31）／maximumPlayers（1–8，默认4）／levelDifference（0–99，默认4）／difficulty（0普通／1噩梦／2地狱，默认0；按服务器角色进度解锁）；只在 Lobby 接受，成功自动取票入局 |
| `online-game-info` | name；Lobby／ListingGames 发原 MCP 0x06，online.gameInfo 返回 Pending／Ready／TimedOut、真实难度标志、人数上限、等级限制、说明与角色。独立请求编号／超时，迟到回复不替换新选择；无回复不推断房间已删除 |
| `online-join-game` | name、可选 password；Lobby／ListingGames 接受，复验输入后退役未完成列表并调用原 MCP 取票接口；UI 同一入口，可按名字直接加入 |
| `online-leave-game` | 创建／加入／连接游戏／握手阶段取消并重新取服务器角色列表；LoadingGame／ProtocolReady 请求原0x69保存退局；LeavingGame重复请求不重发，退局超时标记保存结果未知 |
| `online-return-characters` | Lobby 返回服务器选角，重新取票 |
| `online-cancel` / `online-logout` | 关闭连接；前者进入 Cancelled，后者清空会话回主菜单 |
| `screenshot` / `quit` | 联网截图使用 path 参数；quit 立即返回 accepted，应用在 LoadingGame／ProtocolReady 先正常退局，等响应／关闭或期限后注销退出；不是保存成功回执 |

## 诊断与异步含义

accepted仅入队或发送，不等待原服。online-status的ProtocolReady只表示协议初始化；另查scene.nativeMapReady／movementAvailable／playerDisplayed、原服坐标、请求状态与reason，不能以界面或命令成功代替实际结果。

连接／游戏／区域／交互代次用于本地迟到意图防护，不是原事务号／ACK；物品GUID／revision从当前快照取得。所有权、NPC、Cursor和目标上下文失效时终止未发部分，不撤销原服结果。超时未知，不自动重试。

world.mapEventSequence／First／Count与scene.nativeMapReason定位地图缺口；房间序列最多4096条，失去连续性须重入。playerDisplayPosition为绘制浮点坐标，world.playerPosition为原服整数样本；verifiedDestination／pathVerificationRevision不表示最新鼠标请求已确认。15秒无原位置进展才超时，0x15明确校正及受击／死亡仍服从原服。

world.dead／deathPhase／respawnRequest／corpses描述死亡、回城请求及尸体归属；resources／repositioned组合用于回城确认。scene.effectLimitations只列遇到的未支持客户端程序，空数组不表示全技能认证。network包计数／unconsumed按SID／MCP／game诊断，不回显密码／key／票据或原认证包。

scene.players 按实际已指派玩家返回 id／name／classId、local、visible、moving、dead、显示 position 及不可见 reason。visible 只表示本帧原图进入视口，名册存在不表示可见；位置不写回副本或授权操作。world.units.attributes 保留原 1.13c 0x20 的玩家公开属性，不能当作本人的私有属性／库存。

## 联网物品操作

先用online-items查询实际GUID、revision与decoded；mode为0存储、1装备、2腰带、3地面、4Cursor、5掉落过渡、6孔内。page是原InvPage+1：1背包、4方块、5箱子。本人所有权为ownerType=0且owner等于load.playerUnitId；孔内子项ownerType=4、owner为宿主物品GUID；货架ownerType=1且owner为当前NPC。online-ground也返回完整快照，请按mode=3筛选。

所有修改用`online-item-action`；itemId来自本局服务器，不沿用单机ID。itemRevision／targetRevision可选，省略或0采用提交时版本；建议传查询版本避免操作旧物品。connectionGeneration／gameGeneration／areaGeneration沿用联网通用代次校验。

| action | 参数与范围 |
| --- | --- |
| pickup | itemId；toCursor默认false，true拾到Cursor；地面物品、空Cursor及原50单位距离，服务端自行接近 |
| take | itemId；从本人背包、腰带、当前装备或已确认打开的箱子／方块拿到空Cursor；玩家交易打开时仅允许本人背包／报价（位流page3），对方page2副本只读 |
| place | Cursor itemId、x／y从0计；page为native编号0背包／2玩家报价／3方块／4箱子，默认0；报价需原服Open且本人未同意，其余存储需服务器确认打开，按当前MPQ尺寸与空格校验 |
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
| trade-open | 无itemId；当前NPC交谈且空Cursor，gamble默认false；true要求原赌博NPC，发送0x38 action=2，库存由原服生成 |
| buy | 当前货架itemId；gamble与当前货架及原物品标志一致；普通／赌博购买，multibuy默认false；true发送原mode高位，赌博禁用；发送前共用报价并绑定itemRevision，服务器决定价格／安置 |
| sell | 本人背包、装备或Cursor itemId；当前货架／交谈，Cursor只允许待售原件，非任务物品；发送前共用报价并绑定itemRevision，原服定价 |
| repair | 本人可访问物品或装备itemId；当前原铁匠货架／交谈，最终资格由原服决定 |
| repair-all | 无itemId；当前原铁匠货架／交谈及空Cursor |
| identify-all | 无itemId；当前五幕凯恩交谈及空Cursor；任务资格／费用／效果由原服决定 |

`online.inventory`含revision、gameGeneration、columns／rows、stashColumns／stashRows、cubeColumns／cubeRows、beltSlots、cursor、weaponSet、items和request。storage列kind／requested／source／requestedSource／revision；shopRequested／shopSource绑定货架；tradeResult列原result／flags／itemId／gold／revision。物品含基础name／artKey、品质／词缀、位置／所有者、数量／耐久、防御／金币、孔数及统计列表；0x3E baseStats保留服务器单位。未知／截断数据decoded=false并给出reason，不允许操作。

accepted仅表示请求已排队。request.sequence区分连续操作；Pending等待相关回包，Updated需再查位置／数量／余额；TimedOut结果未知，Interrupted为死亡／换幕／退局，Rejected为NPC明确失败（原码在tradeResult／online.error），SentNoAck为存储关闭已发送。买卖／维修／批量鉴定等待0x2A；初步物品变化不提前结束请求。待请求结束后再提交下一项，不自动重发；storage-close例外允许取消。拾取／合成应同时核对所有权、原材料、产物及world.attributes，不能把地面消失或Updated直接当成功。箱子先从scene.mapTargets查询interaction=stash的真实单位，再用online-interact；商店先用online-npc-interact并等npcConversation。公共背包／货架及玩家交易UI已接，原服药水／金币交易有限运行证据见联网交付；全部物品交易、赌博／多买、佣兵装备仍未完整认证。

```powershell
# 查看原服物品；返回全部物品，按 mode／owner 筛选
.\scripts\Send-D2XCommand.ps1 online-items
# 下面的 ID／revision 均替换为上述查询值，每一步后重新查询实际结果
.\scripts\Send-D2XCommand.ps1 online-item-action -Arguments @{action='take';itemId=123;itemRevision=7}
.\scripts\Send-D2XCommand.ps1 online-items
.\scripts\Send-D2XCommand.ps1 online-item-action -Arguments @{action='place';itemId=123;x=0;y=0}
.\scripts\Send-D2XCommand.ps1 online-item-action -Arguments @{action='switch-weapons'}
```


## 资源工具

既有d2x_assets只读地图诊断，不是游戏旅行／导航接口：

```text
d2x_assets <MPQ目录> native-layout <幕:1-5> <地图种子> [难度:0-2]
d2x_assets <MPQ目录> native-outdoor <区域> <地图种子> [难度]
d2x_assets <MPQ目录> native-room <区域> <地图种子> <tileX> <tileY> [难度]
d2x_assets <MPQ目录> native-map <区域> <地图种子> <难度> [原版导出.json|complete]
d2x_assets <MPQ目录> save-info <file.d2s>
```

原版JSON只重放调用事件，不读取参照地形作输入；complete生成连续组件报告，孤立房间不认证激活顺序／共享边界。save-info不修改角色文件，不恢复联机角色。全部现有资源命令见src/asset_tool.cpp的usage，样本见[地图对照](../architecture/MULTIPLAYER.md#原版-dll-对照方法)。

## 协议和安全

- `\\.\pipe\<name>`，Windows byte-mode named pipe，UTF-8 单行 JSON。请求如 `{"command":"online-status"}`，响应包含 `ok` 与 `online`；失败包含 `error`。
- 每次连接一条请求及一条响应，响应换行后客户端关闭。客户端断线或 15 秒超时后回收连接。请求上限 16 KiB，响应上限 4 MiB，单实例连接。
- EXE 不指定管道时无监听；Play.cmd 默认显式传入管道参数，可用 `-NoDebugPipe` 关闭。ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令入口和呈现在客户端线程；RealmSession 的后台 worker 持锁消费原服回包并更新权威副本，界面只读取客户端快照。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由用户授权新增。

局前 `ui-input` 与游戏内共用帧输入契约；`entryText` 为至多255个可打印ASCII字节，`tab` 切换字段，`wheel` 为有限滚轮增量。`key=1–4`复用腰带列快捷键，`f1–f8`选择已绑定技能或绑定选择器悬停项；原 `text` 仍只用于数字输入。局前坐标同样使用逻辑视口，应用转换到800×600原图布局；回执 `frontend.page/notice` 不包含字段内容或密码。测试暂停拒绝所有 UI 输入。

LAN宿主的server-status新增rooms／participants摘要：实例slot／generation、容量、玩家、区域、阶段及失败信息。宿主未连接本机角色时也可查询；管理远端角色须同时指定hostSlot、hostGeneration、hostPlayer，不能按名字猜绑定。save／grant-experience沿原管理入口处理对应服务端角色；断线保存失败保留实例和租约，显式save成功后才继续回收。load需要主内存客户端重建入局，不能用另一人的请求操纵本机客户端；共享房间禁止重载。调试管道仍只在本机用户授权范围内，LAN不开放它。

## 验证边界

只使用已有程序／命令／参考服；不新增测试脚本、用例或专用程序。实际观察统一见[联网记录](../modules/NETWORK.md)，最新未入包源码不能引用旧冒烟作认证。Windows管道不能替代原服协议；跨用户ACL拒绝与Linux实际运行未完整验证。

P1源码的healingQueued／manaQueued表示仍有恢复时钟的原药水状态数；连续喝同状态药水合并时长与恢复率，不再按瓶数增长。观察药水须同时核对资源、原state、剩余时长及物品消费；本轮没有运行证据。

P5 server-snapshot增加chargedSkills（item／revision／skill／rank／charges／maximum）、player.selectedSkillOwners及itemTriggers.pending／deferred。deferred是最近暂缓程序原因，pending是未结算事件数；不修改实际技能或次数。正常施法仍经online-*原包，管理快照不能作为原服行为认证。

区域快照的objectDeferred列出尚未实现的原物件预设回调身份（574–582）；这些不是已生成的物件，不参与碰撞或掉落。普通缺失表项仍使内容准备失败。

冒烟使用已有宿主管理准备与online-*原包，不新增客户端状态旁路。怪物准入／规则快照用于观察，不能代替逐只普通攻击或完整AI认证；本批证据与边界见[基线](../../BASELINE.md#当前运行包与有限冒烟)。

server-snapshot的怪物identity包含rank／superUnique／spawnKey／ownerSpawnKey；enchantment包含词缀、nameSeed、等级、增伤／命中／速度／抗性及光环参数；home／skillPositions保留原出生点和DS1技能节点。这些为宿主管理观察，不是新增游戏消息；实际验证范围见怪物模块。

### 女巫战斗诊断

管理命令`missile-hit`接收`source`（当前区域真实活敌怪GUID）、`amount`（正伤害生命单位）、`missile`（当前MPQ原弹体ID）。它调用effects伤害／护盾及ReturnFire反击入口，用于隔离观察ChillingArmor反击链；不生成怪物AI、不增加客户端私有消息。`player-damage`也走物理伤害／护盾入口。server-snapshot怪物增加chilledUntil／frozenUntil／knockedUntil／nextHitTick／owner，弹体增加skill／rank／program／age／lifetime／remainingHits。诊断操作不能替代通过原技能包进行冒烟。
