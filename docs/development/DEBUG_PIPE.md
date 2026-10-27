# Windows调试管道

PD2专项内容：[运行与部署](../pd2/OPERATIONS.md)。本页保留原版／公共规则及必要的版本边界。

直接运行EXE只在显式--debug-pipe时启用，Play.cmd默认d2x-debug。原服和Single Player均使用同一RealmSession，因此共用online-*、UI输入、截图、状态、quit及表现pause/resume命令。命令名称保留online不表示只允许TCP。另有显式嵌入宿主管理能力，执行端位于服务端，管道不直接读写D2S或客户端副本；原服连接拒绝这些管理命令。自研暂未实现的操作不会产生成功结算；功能范围及缺口见[基线](../../BASELINE.md)，构建／运行方法见[开发指南](BUILD_AND_RUN.md)。

Single Player的online-status仍只读原协议OnlineView；server-status单独返回宿主诊断身份和tick，不能作为客户端世界同步旁路。ui-input沿现有32帧队列和SceneController，F11／Ctrl+F11与pipe save／load共用AdminRequest管理入口。单机ESC／失焦暂停权威；应用将显式调试暂停一并用于单机宿主，原服pause/resume仍只影响表现。入局／退局清旧UI帧。显式`server-pause`不会因退局自动解除；关闭商店等已受理命令仍需逻辑帧执行，验证正常退出前应`server-resume`或单步推进待办。等待选角阶段及角色租约释放后再停止进程，不能将暂停下的结算超时当作正常退出结果。
## 按任务与权限查找

`online-status.characterUi`只读实际 CharacterView，提供经验／下级门槛、四属性、抗性、防御／格挡、三项当前资源及上限、武器组、余点、左右动作和 skills 的基础／有效等级、树／选择器提示。`server-snapshot.player`另提供权威抗性、格挡、逐手 weapons 的原始8.8伤害、命中、支配及 nativeCombat；missiles 的 minimumDamageRaw／maximumDamageRaw 是技能范围，nativeDamageRaw 才是创建后最终通道范围（如锤的专注加成）。这些出口只序列化既有值，不在调试层重算公式，也不将服务端快照喂给客户端显示。

| 任务 | 入口与权限 |
| --- | --- |
| 连接管道、请求格式与限额 | [启动与调用](#启动与调用)、[协议和安全](#协议和安全) |
| 观察实际原包与耗时 | [原服收包记录](#原服收包记录)、[诊断与异步含义](#诊断与异步含义)；入队不代表执行成功 |
| 权威快照、暂停、单步和管理操作 | [嵌入宿主管理](#嵌入宿主管理命令)；仅具有自研宿主能力的连接，不能用于远端原服 |
| 客户端请求、会话与物品确认 | [联网命令](#联网命令)、[联网物品操作](#联网物品操作)；只读副本不授予权威修改 |
| 离线资源查询 | [资源工具](#资源工具)；不将工具输出作为玩法已完成证明 |
| 允许执行哪些检查 | [验证边界](#验证边界)与项目验证指南；本页列出命令不等于授权执行 |

## 启动与调用

`--headless`使用同一管道的`headless-status`、`headless-start`和`quit`；房主另支持既有`server-*`及宿主管理命令，客端没有该能力。`headless-start`只允许所有本进程会话入局后启动一次，可传`delayMs=0..60000`（默认1000），或未来60秒内的整数`atUnixMs`供多个进程共用同一启动时刻；两者互斥。此模式不接受普通表现／UI命令，配置和统计口径见[无窗口协议客户端](BUILD_AND_RUN.md#无窗口协议客户端)。

`headless-input`向本进程所有已就绪会话发送原协议输入：`action=stop`停止动作，`select-profile-skill`选择该角色配置中的第一个施法技能，`waypoint-open`需当前实际物件`unitId`，`waypoint-travel`需目标`level`与原表`waypointNumber`。可用未来60秒内的`atUnixMs`共同排期，每进程最多32项；排期返回queued只表示登记，立即输入的accepted也只表示客端提交，须另核对服务端位置／区域及收到的原包。首次交互可能仅激活传送点，应核对每个角色的`waypointSource`后再旅行。状态还提供当前act及原包分配的assignedLevels，后者不等同本人当前位置。

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

自定义管道用-PipeName，超时-TimeoutMs范围100–60000、默认10000。脚本遇ok=false／连接失败／超时抛异常。管道由独立重叠I/O线程收发有界文本，主线程每轮最多领取一个命令；线程不读取玩法、MPQ或GPU。响应不再按渲染帧限制每帧4 KiB，调用脚本按字节块写入MemoryStream而非逐字节PowerShell循环，仍保留16 KiB请求／4 MiB响应上限、当前用户ACL及断开后的代次复验；quit等待本次响应交付或连接截止。登录／建房用UI、快捷参数或下面原命令，每一步查询阶段再继续；不能连发非幂等请求。测试服账号／端口只维护在[本机部署](PVPGN_SERVER.md#本机部署与验证入口)。

## 原服收包记录

`online-status`和`server-status`另返回`performance`：最近120个已完成呈现帧的`milliseconds`按host、network、client、debug、scene、present、other和total记录mean／p95／max，`completedFrames`及`over100ms`为本进程累计计数。present包含垂直同步／限帧等待，host包含嵌入宿主工作，scene包含场景及UI更新／绘制；这是实际墙钟耗时，不是25 Hz权威帧。统计有界保存且不逐帧写磁盘；查询看到上一完整帧为止的样本，暂停或切换场景后须等待窗口刷新再比较。

需要核对原服实际消息时，可在启动客户端时指定`--game-packet-log <新.jsonl>`，记录D2X客户端成功入发送队列的C2S和完成分帧、消费之前的S2C；`--host-packet-log <另一新.jsonl>`记录嵌入／TCP/IP宿主完成分帧、校验分派之前的C2S，以及成功入输出队列的S2C（批量输出拆成原包）。两者独立按JSONL记录sequence、相对启动微秒、endpoint、direction、包号、含包头长度和十六进制字节；客户端用gameGeneration区分入局，宿主用connection区分每次游戏字节连接，包含内存房主和TCP客人。握手和离局同样记录；入发送队列不表示对方已执行，收到C2S也不表示事务提交成功。

应用层持有日志文件，network／宿主只有可选字节观察回调，默认关闭。已有文件拒绝覆盖，每文件达到64 MiB或写盘失败明确停止对应连接，不静默丢包。C2S68票据／令牌字段及C2S6C、S2CB3的角色字节置零，`redacted.offset/length`标明原字节范围；保留原长度／分片头，不把置零部分视为实际发包。BNCS／MCP认证内容不记录；未知长度导致不能分帧的TCP块也不在日志中。文件放忽略目录`artifacts/`，不提交。

`online-status.gameServerNotices`保留最近八条原系统通道0x26消息，含相对入场`elapsedMilliseconds`、原字段及`senderBytes/textBytes`字节数组；中文可能为GBK，不能假定UTF-8。`removalAnnouncedMilliseconds`只标识首次完整匹配已知管理移除通知的时刻，不代表原因码或保存成功。故障后与`gameHeartbeat`一并保留；若先有移除通知、后有断流，终态错误优先显示服务器已通知移除并保留底层断流信息。下一局或显式退出清空，不能用上一局公告归因新连接。

根目录`Play-Diagnostic.cmd`调用现有`play.ps1 -Diagnostic`：使用与Play相同的EXE、MPQ和角色目录，开启默认`d2x-debug`管道，每次创建独立`artifacts/diagnostics/<时间>-<随机标识>/`，保存`client-packets.jsonl`、`host-packets.jsonl`、`stdout.log`、`stderr.log`及含实际EXE路径／SHA256的`run.json`。该入口不构建、迁移角色或覆盖旧日志；Windows PowerShell原生stderr按诊断输出记录，不因非致命提示中断游戏。自选文件可用`play.ps1 -GamePacketLog <新文件> -HostPacketLog <另一新文件>`，不能与Diagnostic合用。宿主无游戏连接时宿主日志可为空；原服Join只产生客户端游戏消息。

旧版`s2c.jsonl`只有S2C，不能补取过去的请求字段。问题出现后保持原局，读取`online-items`／`online-status`的最近请求及副本、`server-snapshot`的真实库存／Cursor和命令状态，另看`server-protocol`拒绝计数；与原消息顺序一起保存。最近物品请求另含x／y／page／body／equipVariant，避免混淆原InvPage与S2C的page=InvPage+1。默认Play不开启文件记录；超时只表示缺少关联确认，不能仅凭物品暂时不显示认定服务端已删除。

## 嵌入宿主管理命令

`server-snapshot.animationTiming`只读返回当前武器类下的基础动作帧数、起始帧、8.8速度、结束及事件tick，另有`recoveryUntil`／`lastBlockTick`用于观察玩家恢复锁。普通动作与序列的结束帧算法不同，字段不替代特殊序列调度；规则见[动画帧时钟与恢复](../gameplay/skills/COMMON.md#动画帧时钟与恢复)。

`server-snapshot.monsters`另含当前难度、MPQ Drain和实际physicalResistance；`openWounds`只读返回固定点每帧rate、until／next及实际source／credit，空状态为零。rate单位为1/256生命，快照tick为下一待执行帧；事件背压时next可落后，恢复后补齐期限内应付伤害。字段不允许直接修改怪物或授予效果，结算见[武器命中效果](../gameplay/combat/WEAPON_EFFECTS.md)。

刺客陷阱／影子的怪物记录另含可选 `assassin`：`parameters` 为实际AI参数，`nextDecision`、`expires`、`removeAt`、`lastCorpse`、`missile` 分别观察决策、期限、撤出、所选尸体与附属飞弹。数据只读复制伙伴运行态，不是第二份AI、客户端协议或修改单位的入口。活动单位可能仍在移动／动作中，不能仅以 `nextDecision` 小于当前帧判定阻塞；应一并查看固定步进度、实际动作／事件及队列。实现范围与原版依据见[刺客](../gameplay/skills/ASSASSIN.md)。

当前源码的`server-snapshot`增加`combatMetrics`与`spatial`。`combatMetrics.queueDepth`为完整近战等待数，`targetAttempts`为本步进入目标处理的次数（包括资格拒绝，不等于命中数）；`stages`按系统名输出本步`nanoseconds`、实例累计`calls`／`blocked`，覆盖空间、伙伴、AI、技能、怪物、弹体、效果和战斗。空间在帧事件前、Actor阶段和移动后同步，其耗时相加。`spatial`给出索引tick／generation、活单位数、本步查询数／被精筛候选数／返回数。快照查询不推进随机和玩法，计时仅观察，不参与调度；范围只含已接索引路径。后续同负载采样才能计算p50／p95／p99，不将单步耗时当作性能提升证明。

`online-world.scene.monsters`返回当前已解析怪物的原classId、实际姓名／词缀和最终transform／randomTransform，复用显示姓名与调色路径；不是服务端真值或准入列表。跨进程探索保存只在本地单机／TCP/IP角色入口工作，规则与侧文件边界见[自动地图](../gameplay/world/AUTOMAP.md#跨进程探索保存)。

`online-world.scene.localCast`为本人当前施法表现的skill／age；age=-1表示仍待接近／开始，动作结束或真正中断后为null。`scene.clientMissiles`返回当前视觉队列的id、全局position、age和remaining；负age表示还没到释放帧，队列包含飞行及命中图形，不包含服务端伤害或命中真值。用它核对普通点击后Stop仍保留待释放飞弹、引导停止清理及生命周期；结合截图检查实际原图，不能仅凭服务端missileCount／伤害宣称客户端显示完成。

JSON只在app/debug解析一次；宿主收到类型化操作与GameHandle／PlayerId绑定。管理调用在当前宿主调度线程执行，网络worker仍只经字节队列访问服务端。失败返回ok=false和明确status，不以HTTP式私有ACK修改原MCP／D2GS。

| 命令 | 参数和当前结果 |
| --- | --- |
| `server-snapshot` | 同一调度tick的权威人物、资源／成长／库存revision、物品位置、当前区域怪物及AI、技能动作、待释放数量、弹体、伤害队列、换区和已准备区域／出口。limit默认128、范围1–256；Count是过滤前总数，列表达到limit时不能视为完整。不给坐标时path为剩余权威路径；可给全局x／y，只读计算地形辅助nativePlayerPath（最多1024点），同时返回以当前人物格为中心的49×49 collision窗口（mask／objectMask／playerClear）。mask读取常驻碰撞格，playerClear包含当前实际单位占位，未临时撤下自身；读取不执行Set／Reset或移动角色。辅助路线不代替实际移动命令生成的权威route。objects.collision显示权威尺寸、掩码及当前模式占位。monsters.rules列AI家族／searchDistance／aip、再生／Threat／速度／碰撞／格挡及动作／技能；componentCounts为MPQ十六槽选择数，componentVariants为当前区域该类型池的组合数（0表示原单体路径）；运行列components为实际十六槽选择，另含盾牌、巢生计数、蛛网和中断 |
| `server-events` | 返回已提交领域事实及命令入队／执行记录；resources类型的value／secondary为生命／法力raw值，当前耐力读取snapshot.player.staminaRaw；since／commandSince是各自观察序号，默认0，limit同上。事件环1024条、命令环512条，first／last／gap表示覆盖缺口。记录不被网络发送／ACK清除，不代表客户端已收到；原回包另查online-*。事件value按type解释：life／mana为256固定点，attack为技能ID，hit为原比例生命（人物100、怪物128；怪物0C编码另减一），character为经验且secondary为等级，travel为源／目标区域；object的target为物件GUID、value为固定帧冻结模式、secondary为Shrines.Code或-1，actor为操作者（普通箱可为0），不能拿后来快照反推操作 |
| `server-pause`／`server-resume`／`server-auto-pause` | 对选中玩家所在整个实例设置调试时钟覆盖：pause保留当前行动并停止推进；原协议命令仍可入队，在step执行。resume连续推进；这两者显式覆盖失焦／ESC自动暂停，不冻结客户端表现。auto-pause解除覆盖，恢复应用自动暂停策略；若恢复到暂停会沿原规则取消行动。所有参与者共用时钟，仅本机宿主管理可调用 |
| `refill-resources` | 经人物事务恢复当前玩家生命／法力／体力至派生最大值，并发送原人物增量；只允许存活、已入局角色，不承担复活 |
| `server-status` | 只读phase、tick、paused、hostSlot／hostGeneration／hostPlayer、实际command序号／结果、最近分派和失败、characterIssues；command是最新诊断，不能作为可靠事务回执 |
| `server-protocol` | 返回已登记C2S／S2C／MCP和4F／38子命令；profile、阶段、分帧、固定长度或长度偏移／最低包长、实现状态、领域和收发／queued／stub／rejected／malformed计数。partial区分部分子操作；frameSupported独立于宿主实现。unknownGameRequests只列到达服务分派层的未知请求。定义见[消息速查](../modules/MESSAGES.md)，不输出原始载荷 |
| `server-commands` | 当前管理命令、参数类别及implemented标志；item-spawn另返回itemGeneration，说明seed、affixes行号／rolls和两侧数量限制 |
| `server-systems` | 29个内核目录项的name／phase／scope／lastStep；执行范围以各项scope和内核子系统文档为准。lastStep=null表示未调度或没有固定步入口，不表示实现；详见[内核子系统](../modules/SERVER_SYSTEMS.md) |
| `save` | 无参数；从服务端导出当前角色，校验租约版本并原子保存／备份。applied表示保存已完成，失败保留实例和租约 |
| `load` | 无参数；准备暂停候选实例，成功后经原69／MCP／D2GS离局重入；applied仅表示准备成功且原离局已排队。最后是否入局仍查online-status，候选复用且保留当前难度 |
| `cancel-load` | 原离局尚未接受时释放候选；已经离局后须关闭宿主或完成重新入局 |
| `step` | frames默认1，范围1–250；只允许已入局且权威paused的实例。当前源码先返回queued及server.fixedSteps任务序号，每次宿主调度只尝试一个1/25秒固定帧，帧间继续发布／消费原包、心跳和绘制。completed只计真正完成的逻辑帧，背压重试不扣配额；同实例只保留一个未完成任务。原脚本默认提交一次后查询到完成，-NoWait只返回受理；TimeoutMs限制单次请求及连续无进展时长，进度增加重置等待计时，超时尝试按原任务序号取消，不重发step。普通pause会清路径和待执行移动，step不恢复这些行动；建议用server-pause准备，原服拒绝 |
| `server-cancel-step` | amount为server.fixedSteps.sequence；只取消匹配的未完成配额，不回滚已提交的帧／帧内事实，不改变暂停策略。恢复运行、任务发起者离局及实例撤出取消对应任务；任务期间其他管理变更拒绝，只读查询及暂停／恢复／取消仍可用。fixedSteps返回requested／completed／pending／blocked／cancelled；最后结果保留至下一任务或实例撤出 |
| `grant-experience` | amount为正的有符号整数；宿主progression授予、封顶／升级／余点事务，原包更新同一客户端；暂停时可用，原服拒绝 |
| `grant-gold` | amount为1至UINT32_MAX的正整数；通过人物事务授予金币，复验钱包上限 |
| `set-players` | amount为1–8；设置当前本地实例虚拟人数，与游戏内/players共用类型化宿主入口。仅已入局的本机宿主角色可用，客人及账号Realm拒绝；不写D2S，人数规则见[命令分流](../modules/NETWORK.md#游戏内命令分流) |
| `monster-spawn` | code为当前MPQ monstats身份，x／y为当前区域全局subtile。复用自然人口的怪物准入／数值／动作准备，再交population／monsters；返回entityId。rank默认normal，可选champion／unique／boss／superunique；superunique须提供当前SuperUniques精确身份superUnique，且code必须匹配原Class。仆从只由自然领队人口生成，不提供无领队调试替身。不接受level覆盖，等级随区域／难度，不支持身份、城镇、碰撞和容量明确拒绝 |
| `item-spawn` | code必填、level可选及世界x/y；quality默认normal，可选magic／rare／unique／set／superior／inferior按当前MPQ生成，sockets可选0..6并校验原上限；`ethereal:true`按原无形规则设置标志并应用防御／耐久效果，不适用的基底或品质明确拒绝；durability可显式指定0..实际最大耐久。seed可选0..UINT32_MAX；affixes可指定magic／rare原表词缀及范围内rolls，详见下文。仅item-spawn接受这些项，缺生成规则明确拒绝；走统一地面安装入口 |
| `player-damage` | amount为非负整数生命点，转换为原固定点后走人物伤害事务及正常死亡结算 |
| `grant-shrine` | code，level可选；在当前位置附近生成当前MPQ祭坛，效果仍须正常交互领取 |
| `object-spawn` | code为Objects定义的十进制字符串，x／y为可选原世界坐标，shrine／trap可选；仅本地管理入口，生成原物件以准备观察条件，不直接授予祭坛效果或结算陷阱 |
| `grant-hireling` | code为当前MPQ Hireling原Id十进制字符串，level为1至主人等级；按原Id保留来源难度并选等级段、姓名与成长，原子替换旧佣兵和装备。仅宿主管理准备，不代替NPC雇佣验证 |
| `monster-damage`、`monster-kill` | id为当前区域存活怪物；damage另需amount正整数生命点，kill扣除其剩余生命。走monsters正常伤害事实，后续固定步执行死亡、经验、掉落及任务统计，不直接改任务或客户端 |
| `travel` | level为当前宿主准备的Levels身份（原版1–136，PD2包含扩展预设区域），可选x／y为目的地局部subtile；本地宿主准备真实MPQ地图后按碰撞定位，等待内容时返回Unavailable，可检查server-snapshot后重试。快照area／areas另含townRegion、portalArrival全局锚点及portalSupported，锚点存在不证明对应房间当前驻留或可放门。扩展区域定位见[PD2管理旅行](../pd2/OPERATIONS.md#管理旅行)；通过原旅行事实同步，不授予任务或传送点。用于准备冒烟条件，不能当作开图、事件或首领资格认证 |
| `unlock-waypoints` | 必须二选一：`level`为当前MPQ中带传送点的原区域ID（1–136），或`all:true`解锁完整目录。只修改所选角色当前难度的个人激活记录，重复调用保留已有记录；不要求目标地图已生成，不授予任务或改变物件模式。经人物事务提交，当前有有效传送菜单时发送原0x63刷新，否则下次正常打开菜单时显示；沿原D2S保存，命令本身不写盘 |
| `reset-attributes` | 无玩法参数；退还已分配的四项基础属性点，保留技能分配。经progression／人物事务重算装备资格和派生值，生命／法力／体力限制到新上限，不补满资源；只允许存活、已入局且没有活动技能的角色 |
| `reset-skills` | 无玩法参数；退还已分配的基础技能点，清除两套武器的技能选择、来源GUID和16个技能快捷键，保留属性分配；固有／物品授予技能仍由规则派生。资格同reset-attributes，两项命令均不消耗Akara次数或Token、不改任务资格；重复调用不重复发点，正常保存或save写入原D2S |

`server-snapshot.pd2Map`的`sceneOpening`表示专属入场续办未结束，`sceneParticipants`为受理名单冻结的出生缩放人数，`sceneEntrants`为实际已确认入场的PlayerId数组。前两阶段人数可能因落点失败而不同，沿原口径分别观察；它们不是虚拟players、客户端报告或可写的调试值。


暂停状态下用`travel`准备截图时，`applied`只说明人物旅行已提交，不能据此认定目的房间的物件恢复已经执行。恢复可能等待正常逻辑帧；先推进必要帧数，并检查目标玩家的`server-snapshot.objects`及客户端实际物件分配，再固定画面。原版与D2X须使用同一批已恢复物件，避免把准备时序造成的火把缺失误判为生成或光照差异。

查询及修改命令可附hostSlot／hostGeneration／hostPlayer，取server-status；三个字段须同时指定；不匹配返回invalid-target。未提供时由应用绑定当前宿主角色。这些不是online.gameGeneration，不能互换。save／load不接受path覆盖，固定使用服务器持有租约的角色文件；其他角色加载使用局前入口。命令目录的implemented表示管理入口已接通，具体玩法资格、规则或事务容量仍可能拒绝。

解锁火焰之河或当前难度全部传送点：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command unlock-waypoints -Arguments @{ level=107 }
.\scripts\Send-D2XCommand.ps1 -Command unlock-waypoints -Arguments @{ all=$true }
```

`level`与`all`不能混用；省略两项、`all:false`、非布尔all、未知字段或没有传送点的区域明确拒绝。需要立即持久化时另发`save`；正常保存退出也会沿原当前难度WS位保存，其他难度不变。

单独重置属性或技能：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command reset-attributes
.\scripts\Send-D2XCommand.ps1 -Command reset-skills
```

item-spawn的affixes为非空数组，每项包含kind（prefix或suffix）、row（对应MagicPrefix／MagicSuffix去除Expansion分隔行后的零基原生记录号，不是合并后的原包ID），可选rolls数组。指定后替换随机词缀集合；magic每侧最多1条，rare每侧最多3条，总计最多6条。宿主校验当前MPQ可生成记录、物品类型／职业／等级／rare资格、互斥组及rolls长度／数值范围；不满足时整体拒绝、不落地。rolls按该行非空mod1至mod3顺序，省略时随机；非直接掷值程序只能提供原minimum（空值为0），技能等固定参数仍由原表决定。AutoMagic与StaffMods继续按生成器产生。seed固定本项目当前入口随机，尚不承诺与原版实例全局种子一一对应；这些条件准备不能作为原版自然掉落的依据。

### 按目标属性生成

同一item-spawn提供三个步骤，不新增私有游戏协议：

1. `propertyQuery`为Properties代码子串，空字符串列出全部；按code／level／quality返回合法词缀行、组、需求等级及每条属性的code／param／min／max／input。默认quality=magic，可指定rare。只读查询不创建物品；不能与properties、affixes、durability、sockets、ethereal或坐标混用。
2. `properties`为1–12个目标，和affixes互斥，默认quality=magic，仅支持magic／rare。单值属性使用`{code,value,param?}`；固定复合属性使用`{code,min,max,param?}`，两种形式互斥。param可为非负整数或原Skills精确技能名称；空参数按0匹配。属性代码使用Properties原键，不接受中文显示文案、任意stat ID或浮点值。未知字段明确拒绝。
3. `preview:true`完整准备并验证后返回item，不落地、不登记暗金、不分配世界物品ID。使用返回的seed重发相同参数并设preview=false，可复现本项目生成入口；当前MPQ与暗金可用性等输入必须保持一致。预览不验证地面落点，实际生成仍可能因位置／容量拒绝。

目标匹配当前MPQ的spawnable词缀、基底类型／职业、物品affix level、品质rare许可、互斥组、前后缀容量和原数值范围。每个目标对应一条词缀中的一个Properties槽，不把多个词缀数值拆分求和；min/max必须匹配原固定参数，不能拿它请求随机范围。Magic每侧最多一条；Rare每侧最多三条，至少三条，总计最多六条（珠宝最多四条），不足时补入不重复目标属性的合法词缀。补充词缀、选中词缀自带的其他属性以及StaffMods／AutoMagic会保留；因此目标值不等于最终面板或同stat总和。搜索有100000节点上限，超限明确拒绝，不假称无解。该模式不是自然掉落概率模拟，也不是任意自定义装备编辑器；暗金／套装沿原生成路径，不用目标属性改写。

返回item包含code、quality、seed、itemSeed、level、requiredLevel、defense、durability、maximumDurability、sockets、ethereal、affixes及stats。affixes列原行号、名称、实际rolls；stats列原ItemStatCost名称／ID／参数层和保存值（不是角色面板换算值），用于检查附带属性。所有item-spawn产物在安装前通过现有D2S导出／编码／解码与装备规则准备；PD2属性按原生位宽饱和写出；重复属性层、不支持布局及其他不能原生编码的字段会拒绝。输入错误返回invalid-arguments，目标缺候选包含properties下标和代码；组／容量无解单独报告。编码通过不等于已完成真实保存／重入或所有属性消费者认证。

下面的短剑20%攻击速度示例依据当前MPQ的swing2词缀；完整执行须按各步返回值确认：

```powershell
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments @{
	code='ssd'; level=50; quality='magic'; propertyQuery='swing'
}
$spec = @{
	code='ssd'; level=50; quality='magic'; seed=12345; preview=$true
	properties=@(@{code='swing2'; value=20})
}
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments $spec
$spec.preview=$false
.\scripts\Send-D2XCommand.ps1 -Command item-spawn -Arguments $spec
```

复合属性先查询其代码，再把返回的min／max／param作为目标，例如dmg-cold的min／max为伤害端点、param为原帧数；charged及触发技能的min／max／param按原表含义匹配，不将min/max解释成通用随机区间。server-commands的itemGeneration可发现这套参数；目标匹配不提供全属性消费者或自然掉落的原版依据。

后三幕新增只读诊断为questExecution.actThree／actFour／actFive，分别显示任务掉落／宝珠、封印／熔炉、囚犯／古代人／五波；npcs显示hidden／moving；area.populationDeferred提供原型准备说明，populationMissing为实时人口coverage的暂缓来源数；它不再是宿主旧规划的未放置计数，populationPendingRooms为尚未完成的相关房间数，两者均从权威coverage计算并阻止提前清场。actFive不再输出旧计时器的wave／wavePrepared字段；当前王座波次由BaalThrone AI维护。不提供直接改任务的命令。第二幕诊断在server-snapshot的questExecution.actTwo：eclipse、altarDestroyed、journalRead、staffTombOffset、tombAt、tombOpen与durielSlain；它们只读实例运行态，不作为客户端输入。online-status的world.eclipse／quests.staffTombOffset／staffInteraction来自原包。插杖仍须正常交互取得租约、将真实法杖移到Cursor，再用online-staff-update提交；没有直接授予任务完成的管理入口。规则、原版依据与缺口见[第二幕](../gameplay/quests/ACT2.md)。

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
| `ui-input` | 复用既有FrameInput诊断格式：x／y逻辑坐标、button=left／right、leftHeld／leftReleased／rightHeld、key、shift／control／focused等；frames可一次排队1–32帧，每绘制帧消费一项。技能树用key=skill-tree（实体T），右键技能选择菜单用key=skill-picker（实体S）；聊天用key=enter／escape／message-log（或m）、entryText可打印ASCII、backspace、delete／left／right／home／end编辑；金币输入另支持key=select-all（Ctrl+A）及Shift选区；日志支持up／down／page-up／page-down及wheel。允许局前页面及已显示、未测试暂停的局内UI；游戏或区域代次切换／暂停清队列，普通控制器提交原服意图，不直接改权威副本 |
| `pause` / `resume` | 只在显式调试管道启用；pause要求在线ProtocolReady，冻结画面和界面输入。回执presentationPaused／networkRunning=true；不暂停原服。Single Player在下一宿主调度帧应用权威暂停，以server-status.paused为准；恢复采用最新副本，旧动作不重放 |
| `online-status` | 顶层presentationPaused；online.protocol按SID／MCP／game返回包ID、received／sent／unconsumed、逻辑字节数和lastReceived；只读 `online`：stage、error、revision、connectionGeneration、gameGeneration、Realm／角色／游戏列表、load、延迟（首个pong前null）、gameQueuePosition、gameListComplete、world／scene；联网模式的 `status` 是其别名 |
| `online-social` / `online-chat` | 同一完整只读快照的accountChat及world.social：名册身份及字段可用性、队伍／关系原值、公开位置、聊天原语言nameBytes／textBytes；只读、不刷新。共享界面就绪后额外chatUi返回ready／inputOpen／logOpen／draft／scroll／rows／unavailableMessages／reason，只有表现和草稿，不是发送回执。scene.automap.markers只读标记类型／名称／位置／cel；当前范围、原服参考与缺口见[联网模块](../modules/NETWORK.md) |
| `online-chat-input` | message经过与游戏UI相同的通用分流，支持/nopickup、/players、房间／头顶／私聊和BNCS命令。模式错误明确拒绝；accepted只代表本地执行或传输入队，远端结果查看accountChat及world.social。scene.noPickup显示本机开关；server-snapshot.virtualPlayers和怪物admittedPlayers／experienceBase提供人数观察 |
| `online-send-chat` | message为1–255字节可打印ASCII且不全为空格；可选receiver为15字节内角色名，发送原0x15私聊；可选overhead=true发送原0x14且receiver须为空。accepted只代表入队，消息取真实0x26及chatSequence，不插本地回显／自动重试。普通广播、私聊、头顶原包与屏蔽由共享服务端social执行；中文编码、表情与BNCS频道暂缓。头顶消息值为world.social.hover，图形气泡未新增 |
| `online-party-action` | unitId为当前名册真实GUID，action为invite／cancel／accept／leave（leave目标为本人）；可选gameGeneration复验本局。与P界面共用RealmSession.party_action，只发送原5E；accepted不修改名册／队号，真实结果读取75／8B／8D。 |
| `online-chat-relation` | unitId为当前名册另一玩家GUID，action为ignore／squelch／loot／hostile，enabled布尔值；原5D/action2／3／1／4。真实关系等75／8C；忽略者阻止自己的话到达目标，squelch者屏蔽目标发来的消息，沿原PlrMsg方向条件；loot只接受存活HC，用于授予／撤销本人尸体许可；hostile仍受原等级／城镇规则约束，队伍使用online-party-action |
| `online-trade-respond` | accept布尔值、revision为当前world.playerTrade.revision；true接受邀请（button3），false拒绝／取消（button2）。accepted仅表示发送；真实身份等0x78，完成等0x77/13。 |
| `online-trade-offer` | action为agree／revoke／gold，revision绑定当前交易，gold另需amount 0–INT32_MAX；原button4／7／8。world.playerTrade返回ownGold／peerGold、ownAgreed（请求已发）／peerAgreed、agreementLocked及response；最终交换只由服务端确认，自研权威边界见[玩家交易](../gameplay/items/PLAYER_TRADE.md)。 |
| `online-world` | 可选fromX／fromY／x／y及radius（0–24）只读当前客户端原生路径和碰撞窗口，返回navigation；不移动角色或改碰撞。只读同一快照：world.units／rooms／equipment／attributes、本人全局 subtile 坐标与当前生命／法力／体力；scene 含原 DS1、原点、候选／地标、碰撞／显示／移动可用性、本人是否绘制与缺外观数量；当前源码units增加nativeMode、direction、actionSkill／actionSkillLevel，区分实际模式、原路径面对方向及当前技能动作 |
| `online-resurrect` | 无参数；要求原服报告死亡。等待DEAD后发送原0x41，重复请求去重；world.respawnRequest记录WaitingForDeath／Sent／Confirmed／TimedOut及sent，确认前不恢复本地资源。Hardcore执行退局 |
| `online-recover-corpse` | unitId为world.corpses及scene.mapTargets中本人或已授权HC的真实可见尸体GUID，自动type0；先按共同路径靠近，再原0x13请求取回。以服务端库存／装备与尸体回包确认，accepted不表示回收完成 |
| `online-items` / `online-ground` | 只读同一完整快照；online.inventory.items含地面和各所有者物品，依mode／owner筛选，不请求服务器刷新；尺寸、数量、耐久、词缀、孔内所有者、decoded／reason及revision见下文 |
| `online-item-action` | action和真实itemId，可选itemRevision／targetRevision；配对动作还需targetId。28种原物品／城镇请求及格子／部位参数见[联网物品操作](#联网物品操作)，accepted仅入队 |
| `online-item-quote` | action=buy／sell／repair／repair-all；前三者需本局itemId，可选itemRevision。返回known及price，缺数据为null；复用UI报价，不发送询价包或推进待发库存操作。赌博购买报价取当前真实货架模式 |
| `online-combat` / `online-skills` | 只读同一快照；combat.skills列MPQ技能名、基础／装备加成／有效等级、innate、左右键／城镇资格；combat.states列原服状态和原单位属性；world.itemTargetingSource为原3F来源（null表示无准备，不表示鉴定成功），itemSkillQuantities为原物品技能数量；combat.events为最多256条有序战斗事件，sequence递增，消费者自行检查缺口；0x73首路径点为missileDestination（旧missileOrigin名称已更正），不是飞弹出生点 |
| `online-select-skill` | skillId（0–65535）、hand（left／right，默认right）；可选ownerId为已装备物品原GUID，省略为普通技能源FFFFFFFF；当前MPQ、原等级或已解码充能校验，原0x3C选择，等combat.request.state=Confirmed再施放 |
| `online-bind-hotkey` | slot（0–15）、skillId、hand及可选ownerId；校验原普通／充能来源，原0x51保存绑定，无即时ACK，request为SentNoAck；重入world.skillHotkeys读取0x7B。UI使用F1–F8 |
| `online-cast` | hand；坐标x／y或真实unitId／unitType（默认1）二选一。stationary默认false；单位目标false允许原服靠近，true原地请求。repeat默认false，true发原Hold包一次，调用方负责继续提交／停止，不创建客户端循环。各轴≤50、活动地图及MPQ城镇限制，单位目标按MPQ资格区分PvE敌怪／尸体、Enchant友方、Unsummon本人7A／PetType许可的召唤物及Telekinesis物件／物品；自施技能用本人坐标 |
| `online-attack` | 同cast目标及stationary／repeat，固定左手；先选择MPQ Attack技能并等确认。伤害、追击与命中由原服处理 |
| `online-stop-skill` | 发原0x12停止地狱火状态；停止重复提交Hold请求由调用方负责。这不是全部技能的通用撤销包 |
| `online-learn-skill` | skillId；检查职业、已学前置、等级、属性、MPQ最大等级和原服可用点。原0x3B；CN编码见[成长请求](../pd2/CLIENT_INTEGRATION.md#客户端接入成长请求编码)；等待基础技能等级增加和余点减少，允许两类回包按不同顺序到达；原服无通用失败包，超时结果未知，不自动重试 |
| `online-spend-attribute` | statId：0力量／1精力／2敏捷／3体力；count：1–100，默认1，需有足够原服属性点。原0x3A打包count-1于高字节，等待对应绝对属性增加和余点减少；CN边界见[成长请求](../pd2/CLIENT_INTEGRATION.md#客户端接入成长请求编码) |
| `online-move` | x、y为服务端全局subtile整数（0–65535），run默认true；ProtocolReady、角色存活、目标属于已加载原地图且各轴距本人采样≤50时接受。直接发原0x01／0x03目标，至少间隔100ms，不切18格短段等待回包；可达点／障碍由原服处理。accepted表示命令已发送，不表示已到达；鼠标使用同帧显示坐标作投影范围提示，原服仍按真实位置校验50格范围 |
| `online-move-to-unit` | unitId、unitType（默认2，支持1／2／5）、run（默认true）；限定当前scene.mapTargets真实单位及各轴50-subtile范围，直接发原0x02／0x04跟随真实GUID，不发交谈 |
| `online-use-exit` | unitId为online-world返回的真实type5单位ID，要求原生地图可用／角色存活；发送0x13交互，不接受用LvlWarp类型编号替代单位ID |
| `online-interact` | unitId、unitType（默认2，支持1／2／5）、run（NPC靠近默认true）；目标必须在scene.mapTargets中。近处发原0x13；远处先按真实GUID发02／04，本地普通单位路径到达后才13，交互资格及菜单仍由服务端确认；accepted不代表已操作、交谈打开或换区 |
| `online-town-portal` | 无参数；要求存活／原生地图可移动／非城镇及原服已报告可用卷轴或书技能、右技能。选原右技能并施放，后续world.townPortalPending及实际type2门户回包给出等待／分配状态；不发放卷轴 |
| `online-npc-interact` | unitId、run（默认true）；online-interact的type1入口。只接MPQ允许交谈的实际NPC，远处先靠近；等待scene.npcConversation |
| `online-npc-message` | stringId（0–65535）、可选npcRevision；必须为当前服务端对白列表且未确认，版本不符拒绝。发0x31，acknowledged只表示入队，不是任务成功回执 |
| `online-npc-close` | 无参数；取消尚在靠近的交谈意图，或发0x30关闭当前NPC。取消靠近意图不会传送角色或撤销已提交的服务端寻路 |
| `online-npc-travel` | 无参数；要求当前scene.npcConversation.travelLabel与travelDestination来自瓦瑞夫／马席夫真实身份和任务资格；发原0x38 action=0、NPC、目的地并结束临时交谈，随后查询实际幕／位置 |
| `online-staff-update` | cancel布尔（默认false）；要求已收到原0x58的插杖来源，提交时只使用实际本人Cursor物品GUID，发送原0x44 state3；cancel发送state2。入队不代表材料消耗或古墓开墙，应核对库存、0x58结果和权威机关 |
| `online-npc-respec` | 无参数；要求原slot41 pending且Akara菜单有原重置选项；发送原0x38 action0、NPC、parameter0，随后核对属性／技能／热键与存档 |
| `online-waypoint-travel` / `online-waypoint-close` | travel的level为原区域ID1–136，必须有当前服务端0x63菜单且目的地已解锁；close无参数，可取消当前待确认请求或关闭已确认菜单。发送原0x49，level=0为关闭，不解锁任务或传送点 |
| `online-automap` | 可选visible／large布尔值，visible省略时开关、large省略时保留；只改变显示。scene.automap返回大小／显示、stamps／towns数量及当前连续层revealedCells |
| `online-login` | 必填 account、password；原版文件／认证模式／端口读取 `--online-config` 私有配置，只在 Idle／Failed／Cancelled 接受；自动选择配置中的 Realm |
| `online-register` | account、password（各2–15）；配置和允许阶段同登录，注册成功自动登录；服务端拒绝码在 error 中 |
| `online-create-character` | name（1–15个ASCII字母／数字／连字符／下划线；最终资格由服务端判断，自研暂不接受下划线）、classId（0–6，默认0）、hardcore（默认false，旗标0x04）、ladder（默认false，旗标0x40）；CharacterSelection 接受，资料片固定0x20，专家级／天梯按请求提交，服务器生成初始数据后刷新列表；自研服务端只接受0x20／0x24，含0x40的请求被拒绝 |
| `online-delete-character` | name 和完全相同的 confirmName；CharacterSelection 接受，必须来自当前列表；不可撤销，成功刷新列表 |
| `online-return-realms` | CharacterSelection 关闭 MCP 并重新取 Realm 列表；保持 RealmSelection 等待显式选择 |
| `online-cancel-list` | ListingGames 取消等待并返回 Lobby，不关闭 MCP，列表完成标记为 false |
| `online-realms` / `online-characters` / `online-games` | 只读当前服务器快照，不发起刷新；未知角色字段为 null |
| `online-select-realm` | name；只在 RealmSelection 接受；首次登录自动选择配置项；切换 Realm 或配置项不存在时等待显式选择 |
| `online-select-character` | name；只在 CharacterSelection 接受，限定可玩的非 Ladder LoD 角色 |
| `online-list-games` | 可选 filter（≤15）；Lobby／ListingGames 发起或替换真实列表请求，filter 为本地名称包含筛选，完成后返回 Lobby；无终止包时超时保留已收到列表，并标记不完整 |
| `online-create-game` | name（≤15）、可选 password（≤15）／description（≤31）／maximumPlayers（1–8，默认4）／levelDifference（0–99，默认4）／difficulty（0普通／1噩梦／2地狱，默认0；按服务器角色进度解锁）／gameServer（CN专用1–9，省略时用登录配置默认值，原版显式传入会拒绝）；只在 Lobby 接受，成功自动取票入局 |
| `online-game-info` | name；Lobby／ListingGames 发原 MCP 0x06，online.gameInfo 返回 Pending／Ready／TimedOut、真实难度标志、人数上限、等级限制、说明与角色。独立请求编号／超时，迟到回复不替换新选择；无回复不推断房间已删除 |
| `online-join-game` | name、可选 password；Lobby／ListingGames 接受，复验输入后退役未完成列表并调用原 MCP 取票接口；UI 同一入口，可按名字直接加入 |
| `online-leave-game` | 创建／加入／连接游戏／握手阶段取消并重新取服务器角色列表；LoadingGame／ProtocolReady 请求原0x69保存退局；LeavingGame重复请求不重发，退局超时标记保存结果未知 |
| `online-return-characters` | Lobby 返回服务器选角，重新取票 |
| `online-cancel` / `online-logout` | 关闭连接；前者进入 Cancelled，后者清空会话回主菜单 |
| `screenshot` / `quit` | 联网截图使用 path 参数；quit 立即返回 accepted，应用在 LoadingGame／ProtocolReady 先正常退局，等响应／关闭或期限后注销退出；不是保存成功回执 |

## 诊断与异步含义

`online-item-action`的`storage-index`要求`amount=0/1/2`表示普通／索引／主索引；页号与域绑定当前确认页，不能修改其他页。要求空Cursor、普通仓库已同步且无其他待办，等待9D/18的flags确认；超时不重试。`storage-rename`及聊天`/rp`明确拒绝且不发包：当前服务清空页名后发生断流，兼容性尚未闭合。`inventory.storage`提供只读`pageName`及可空`sharedGold`，个人goldbank与该共享余额分开。

合成请求保存提交时方块材料的GUID／revision，仅相关材料变化或新方块物品更新结束等待；`Updated`仍只表示相关服务端更新，不表示完整配方或全部产物已经验收。普通金币请求按提交前余额及金额核对已知双方变化，材料升级核对双方计数，可按任意顺序收包。PD2专项说明见[运行与部署：无限书确认](../pd2/OPERATIONS.md#运行与部署无限书确认)。

`online-item-action`新增`belt-auto`（本人背包`itemId`，原0x63）及`cube-store`（Cursor／地面`itemId`、本人已存方块`targetId`，原0x2A），复用现有身份／revision与请求等待。两者只发送原请求，不生成材料或授予方块合成资格。PD2专项说明见[运行与部署：仓库翻页命令](../pd2/OPERATIONS.md#运行与部署仓库翻页命令)。

传送点观察需区分首次激活与菜单：野外中性物件第一次`online-interact`只启动／激活，收到原0x0E后结束等待；第二次点击mode1／2才出现0x63菜单。城镇传送点初始mode2，可直接打开。`server-snapshot.area`及`areas`新增`waypointIndex`和可空`waypointAnchor`（全局subtile）；`waypoints`仍列本人当前难度激活区域，`objects`列真实模式，`travel`列待过渡。用于观察原流程，不授予激活权限；源码规则及未认证范围见[传送点](../gameplay/world/OBJECTS.md#自研传送点)。

accepted仅入队或发送，不等待原服。online-status的ProtocolReady只表示协议初始化；另查scene.nativeMapReady／movementAvailable／playerDisplayed、原服坐标、请求状态与reason，不能以界面或命令成功代替实际结果。

连接／游戏／区域／交互代次用于本地迟到意图防护，不是原事务号／ACK；物品GUID／revision从当前快照取得。所有权、NPC、Cursor和目标上下文失效时终止未发部分，不撤销原服结果。超时未知，不自动重试。

world.mapEventSequence／First／Count与scene.nativeMapReason定位地图缺口；房间序列最多4096条，失去连续性须重入。playerDisplayPosition为绘制浮点坐标，world.playerPosition为原服整数样本；verifiedDestination／pathVerificationRevision不表示最新鼠标请求已确认。15秒无原位置进展才超时，0x15明确校正及受击／死亡仍服从原服。

world.dead／deathPhase／respawnRequest／corpses描述死亡、回城请求及尸体归属；resources／repositioned组合用于回城确认。scene.effectLimitations只列遇到的未支持客户端程序，空数组不表示全技能认证。network包计数／unconsumed按SID／MCP／game诊断，不回显密码／key／票据或原认证包。

world.quests.npcIntroductionReports按零基幕号返回最后收到的0x91原报告（12个WORD，未收到为null，65535为空项而非清除指令），换局清空，未联动对白或任务奖励。world.ignoredPackets及protocol.game.packets[].unconsumed统计已完整分帧但尚未消费的消息；未知包长和损坏消息仍按协议错误处理；PD2未消费消息边界见[客户端接入](../pd2/CLIENT_INTEGRATION.md)。

scene.players 按实际已指派玩家返回 id／name／classId、local、visible、moving、dead、显示 position 及不可见 reason。visible 只表示本帧原图进入视口，名册存在不表示可见；位置不写回副本或授权操作。world.units.attributes 保留原 1.13c 0x20 的玩家公开属性，不能当作本人的私有属性／库存。

## 联网物品操作

`online-status` 附带 `characterUi`／`hirelingUi`，读取当前共享 UI 端口的实际数值（含伤害／命中与 unknownStats、佣兵 known），便于和页面截图核对。它们不读取服务端 Totals、不补私有字段、不改变人物；原包事实仍在 online 下，宿主权威对照使用 server-snapshot。

`online-status`的`online.gameHeartbeat`给出游戏心跳sent／received／pending、相对入局的elapsedMilliseconds及最后packet／ping／pong时间，不含认证或消息载荷。PD2专项说明见[运行与部署：心跳判断](../pd2/OPERATIONS.md#运行与部署心跳判断)。

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
| use | 背包、已打开仓库／方块或腰带itemId；mercenary默认false，true仅腰带；方块用cube-open，回城卷轴／书可按原0x20使用，技能快捷入口仍用online-town-portal |
| belt-place | Cursor itemId、beltSlot从0计；不得超当前MPQ腰带容量 |
| belt-swap | Cursor itemId、腰带targetId；槽位取目标服务器位置 |
| stack | 源itemId、targetId；原兼容堆叠请求，不支持自选拆分数量 |
| book | 卷轴itemId、书targetId；必须为当前MPQ Books原配对 |
| socket | Cursor填充物itemId、宿主targetId；鉴定、归属和已知空孔校验 |
| identify | 可访问容器内鉴定卷轴／书itemId、未鉴定targetId；目标为可访问容器或装备，Cursor须空；先0x20准备，0x3F只确认准备，0x27才提交目标 |
| switch-weapons | 无itemId；空Cursor，0x97确认后更新weaponSet |
| cube-open | 本人背包或已打开仓库方块itemId；按当前MPQ pSpell=7提交使用，0x77确认后开放方块格 |
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
| quest-service | 本人Cursor itemId；真实Charsi／Larzuk／Anya交谈；复用原0x38 action0与0x58确认，服务器复验资格、一次性消耗及结果。取物到Cursor需先用take并等待位置更新 |

`online.inventory`含revision、gameGeneration、columns／rows、stashColumns／stashRows、cubeColumns／cubeRows、beltSlots、cursor、weaponSet、items和request。storage列kind／requested／source／requestedSource／revision；CN分页字段及确认边界见[仓库诊断](../pd2/OPERATIONS.md#仓库诊断)；shopRequested／shopSource绑定货架；tradeResult列原result／flags／itemId／gold／revision。物品含基础name／artKey、品质／词缀、位置／所有者、数量／耐久、防御／金币、孔数及统计列表；0x3E baseStats保留服务器单位。未知／截断数据decoded=false并给出reason，不允许操作。

accepted仅表示请求已排队。request.sequence区分连续操作；Pending等待相关回包，Updated需再查位置／数量／余额；TimedOut结果未知，Interrupted为死亡／换幕／退局，Rejected为NPC明确失败（原码在tradeResult／online.error），SentNoAck表示存储关闭或已核实无限书等没有独立ACK的请求已发送，不代表执行成功。买卖／维修／批量鉴定等待0x2A；初步物品变化不提前结束请求。待请求结束后再提交下一项，不自动重发；storage-close例外允许取消。拾取／合成应同时核对所有权、原材料、产物及world.attributes，不能把地面消失或Updated直接当成功。箱子先从scene.mapTargets查询interaction=stash的真实单位，再用online-interact；商店先用online-npc-interact并等npcConversation。公共背包／货架及玩家交易UI已接，原服药水／金币交易消息见[联网参考](../modules/NETWORK.md#原服物件与交易观察)；全部物品交易、赌博／多买、佣兵装备仍待完整核对。

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

原版DLL重放、实机房间恢复步骤及诊断限制见[地图排查](MAP_PARITY.md)。

既有d2x_assets只读地图诊断，不是游戏旅行／导航接口：

```text
d2x_assets <MPQ目录> native-layout <幕:1-5> <地图种子> [难度:0-2]
d2x_assets <MPQ目录> native-outdoor <区域> <地图种子> [难度]
d2x_assets <MPQ目录> native-room <区域> <地图种子> <tileX> <tileY> [难度]
d2x_assets <MPQ目录> native-map <区域> <地图种子> <难度> [原版导出.json|complete|prepared]
d2x_assets <MPQ目录> save-info <file.d2s>
```

原版JSON只重放调用事件，不读取参照地形作输入；complete生成连续组件报告，prepared走实际AreaGenerator准备路径，输出冻结碰撞及房间初值。save-info只读角色文件。完整用法见src/asset_tool.cpp的usage，导出及比较方法见[地图排查](MAP_PARITY.md#已恢复的dll导出与独立重放)。

## 协议和安全

- `\\.\pipe\<name>`，Windows byte-mode named pipe，UTF-8 单行 JSON。请求如 `{"command":"online-status"}`，响应包含 `ok` 与 `online`；失败包含 `error`。
- 每次连接一条请求及一条响应，响应换行后客户端关闭。客户端断线或 15 秒超时后回收连接。请求上限 16 KiB，响应上限 4 MiB，单实例连接。
- EXE 不指定管道时无监听；Play.cmd 默认显式传入管道参数，可用 `-NoDebugPipe` 关闭。ACL 仅允许启动进程的 Windows 用户，拒绝远程客户端。相同用户的其他进程仍能调用，不能把它当对同用户恶意进程的安全隔离。
- Win32 管道只在 app 模块，处理非阻塞；JSON 使用 nlohmann/json 3.11.3。命令入口和呈现在客户端线程；RealmSession 的后台 worker 持锁消费原服回包并更新权威副本，界面只读取客户端快照。
- 该脚本是通用调用入口，未加入专用测试脚本／测试程序。调试接口由用户授权新增。

局前 `ui-input` 与游戏内共用帧输入契约；`entryText` 为至多255个可打印ASCII字节，`tab` 切换字段，`wheel` 为有限滚轮增量。`key=1–4`复用腰带列快捷键，`key=belt`／`expand-belt`／`~`切换腰带展开，`f1–f8`选择已绑定技能或绑定选择器悬停项；原 `text` 仍只用于数字输入。局前坐标同样使用逻辑视口，应用转换到800×600原图布局；回执 `frontend.page/notice` 不包含字段内容或密码。测试暂停拒绝所有 UI 输入。

LAN宿主的server-status新增rooms／participants摘要：实例slot／generation、容量、玩家、区域、阶段及失败信息。宿主未连接本机角色时也可查询；管理远端角色须同时指定hostSlot、hostGeneration、hostPlayer，不能按名字猜绑定。save／grant-experience沿原管理入口处理对应服务端角色；断线保存失败保留实例和租约，显式save成功后才继续回收。load需要主内存客户端重建入局，不能用另一人的请求操纵本机客户端；共享房间禁止重载。调试管道仍只在本机用户授权范围内，LAN不开放它。

## 验证边界

只使用已有程序／命令／参考服，不新增测试脚本、用例或专用程序。实际执行在当次交付中说明，长期文档保留当前实现、原版依据与具体缺口。Windows管道不能替代原服协议；跨用户ACL拒绝与Linux实际运行仍待核对。

P1源码的healingQueued／manaQueued表示仍有恢复时钟的原药水状态数；连续喝同状态药水合并时长与恢复率，不再按瓶数增长。观察药水须同时核对资源、原state、剩余时长及物品消费。

P5 server-snapshot增加chargedSkills（item／revision／skill／rank／charges／maximum）、player.selectedSkillOwners及itemTriggers.pending／deferred。deferred是最近暂缓程序原因，pending是未结算事件数；不修改实际技能或次数。正常施法仍经online-*原包，管理快照不能作为原服行为认证。

区域快照的objectDeferred列出尚未实现的原物件预设回调身份（574–582）；这些不是已生成的物件，不参与碰撞或掉落。普通缺失表项仍使内容准备失败。

诊断使用已有宿主管理准备与online-*原包，不新增客户端状态旁路。怪物准入／规则快照只提供当时状态，完整普通攻击、AI和原版逐随机行为仍须独立核对。

server-snapshot的怪物identity包含rank／superUnique／spawnKey／ownerSpawnKey；enchantment包含词缀、nameSeed、等级、增伤／命中／速度／抗性及光环参数；home／skillPositions保留原出生点和DS1技能节点。这些为宿主管理观察，不是新增游戏消息；身份、原版依据与缺口见[第一幕怪物](../gameplay/monsters/ACT1.md)，管理准备不能作为真实首领战斗依据。

### 女巫战斗诊断

源码中的`server-snapshot.frameEvents`观察实例玩法定时器，和可靠输出历史`eventHistory`分开；包含总数、当前帧／阶段、执行／下一游标及受limit限制的记录，记录按事件身份排列，不代表派发顺序。事件记录暴露单位类别、事件号、期限、持续标记、原custom参数、本地状态事件payload及延迟删除标记；怪物AIRESET列表同时暴露独立identity。诊断不写入存档或原客户端消息，原始调度入口及缺口见[技能公共规范](../gameplay/skills/EFFECTS.md#帧事件底座与持续效果期限)。

管理命令`missile-hit`接收`source`（当前区域真实活敌怪GUID）、`amount`（正伤害生命单位）、`missile`（当前MPQ原弹体ID）。它投递公共combat命中候选，单步推进后执行伤害／护盾及ReturnFire反击，用于隔离观察ChillingArmor反击链；不生成怪物AI、不增加客户端私有消息。`player-damage`也走物理伤害／护盾入口。server-snapshot怪物增加chilledUntil／frozenUntil／knockedUntil／nextHitTick／owner，弹体增加skill／rank／program／age／lifetime／remainingHits。诊断操作不能替代通过原技能包进行冒烟。
