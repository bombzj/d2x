# 调试AI诊断

本页维护AI状态、行为日志、导航采集格式及解释边界。命令语法见[使用入口](DEBUG_AI.md)，传输／原包／宿主快照见[调试管道](DEBUG_PIPE.md)，启动采集见[构建运行](BUILD_AND_RUN.md#诊断启动与任务-ai)，验证授权见[测试指南](TESTING.md)。诊断不会向AI注入Host真值或修改导航知识。

## 状态与事件

先读取`debug-ai-status`及活动子导航`debug-walk-status`，区分任务阶段、未受理输入、已提交请求、后台求路与实际位置。快照是采样，不是逐包历史；子导航结束后的位置可能停留在最后决策帧，实时位置另查online-status。

| 字段 | 解释 |
| --- | --- |
| state／goalIndex／goals／phase／reason | 计划、零基目标索引、队列及当前阶段；goals含面板questNumber及服务选项 |
| questPlan／questProgress | 条件图、就绪节点及估价；conditions模式的step为节点索引。pending含operationPending／operationMode，inventoryWork为独立库存工作 |
| actions | inputBlocked、actionRecoveryBusy、continuation、逐动作owner／inputs／interruptors／waitingFor／clock／waitRemainingMs／resultObserved／recovery；submissions仅为尚未受理的输入 |
| goalExecutionRemainingMs／executionSuspendedMs | 父执行预算；子导航为executionRemainingMs／suspendedExecutionMs，不等同请求现实期限 |
| navigationDiagnostics | search为通路选择，approach为目标路线，mobility为实际位移候选，travel为区域路线和传送点选择 |
| navigation.targetSearches／targetSearch | 本轮范围、触发、核查与候选；absenceProven=false明确不证明目标不存在 |
| exploration | frontier通路身份／证据／站位／路线／无进展；frontierAttempts为具体接近的短期重试，不是通路证据 |
| populationMemory／waypointMemory／rememberedDrops | 房间和敌人线索、唯一决策解锁表及最近完整63修订、当前计划已见待拾物品；均不授权攻击或写档 |
| travel／travelDiagnostics | route、visitedAreas、failedConnections、movement及连接开口；估价不是秒数或可通证明 |
| routeExecution／blockedStep／pointerLane | 局部任务模式、阶段、受限目标代次、原导航意图和请求；blockedStep给出碰撞归因 |
| townLane／pointerSelections | 双段转角的bend／resume／target；最近32次普通短程受理的request、from、feet、raw、lane、submitted、resume及branch。宿主保护可再缩短指针，不等于发包历史 |
| movementRecoveries／recoveryAnchor／localRelayRecovery | 双端失配接回次数、锚点及局部重规划；位置校正不等于原目标到达 |
| questProgress.worldWait／encounterStance／encounterStandoff | 实际锚点、expected／progress／lastProgress／trackedEnemies／期限，站位候选与路线；clearanceRevision为现场核查轮次 |
| passageWaits | 通行阶段、复试次数、下次复试及总余量 |
| combatFeedback／hazardFeedback | 目标身份、生命新低／回升、射线、技能序列、输入拒绝、实际攻击距离及危险候选；无伤害不证明免疫 |
| combatPolicy／eventPolicy／configuration | 当前有效策略、出城技能、技能距离覆盖、职业、成功加载文件及经验区域数 |
| supplies.vitals／lastPotion | 当前资源与显示上限、储备；consumed与recoveryObserved分开，书页容量未知保留unknown |
| behaviorLog | 本计划日志绝对路径及写盘错误；history最多128条 |

动作输入位：移动1、技能2、库存4、交互8、菜单16。中断位为goal／navigation／door／waypoint／mobility／potion／supply／combat的0–7索引；这些是本地契约，不是原协议字段。

## 行为日志

每次start建立UTF-8 TSV `events.log`；append／pause／resume沿用，新计划另建。默认目录为工作目录的`logs/ai-<启动毫秒>/`，启用game-packet-log时位于抓包文件旁。状态给绝对路径；写盘失败报告错误但不停止AI。

每行：计划经过毫秒、历史序号、目标索引、区域、本地脚点、最近Host脚点、事件、详情。缺失为`-`，头部有启动Unix毫秒；额外阶段／导航事件无历史序号。详情先还原TSV的`\\`、`\t`、`\r`、`\n`转义，再按该事件格式解析JSON。两套脚点都是当时样本，Host可滞后。

定位失败先看`failed／walk-failed`，再回溯`goal-started／quest-condition-selected／walk-goal`。`phase+`只在阶段或原因改变时写入，`phase-`记录持续时间。当前实现逐行flush；正常结束仍活动的计划记`session-ended`，不保证异常终止零丢失。

| 事件组 | 内容及限制 |
| --- | --- |
| observation-viewpoint-deferred | 主reason按短路顺序选择，conditions保留同时成立条件；含目标距离、取消前child、观察增量及approach诊断 |
| observation-viewpoint-released／viewpoints-cleared | 前者为到期／房间观察增长／容量释放，后者为步骤或区域批量清理；到期清理在下次探索发生，不保证精确30秒 |
| navigation-frontier-attempt | target、position、reached、retainAfterMove、outcome、5秒／30秒重试、几何修订及relayGoal；不是旧文本坐标格式 |
| exploration-direction-*／backtrack | 通路选择、观察、暂缓及回走；暂缓按通路和局部证据，不按整区覆盖增长清空 |
| known-target-direction-selected／interaction-ready | 目标方向、成本或已验证交互站位；fromCell／sourceMask／sourceStandable标识实际规划起点 |
| target-search-awaiting-appearance／revisit／stopped | 出生等待、仅退订范围重访、截止或范围耗尽；已查瓦片及未知范围不证明缺席 |
| stall-snapshot | Host样本8秒内位移不超过2格且不属于对话／治疗／已提交任务等自有期限等待时记录；16／24／32秒续记，同停留最多4次。附动作、子导航、任务、旅行及战斗等现场 |
| quest-alert | 原0x8A提示集合变化，含新增／移除NPC GUID与class及阶段；感叹号不是完成证据 |
| route-*／mobility-*／town-exit-* | 路段交接、requested与actual落点、relayReleased、出城恢复；偏移不等于施法失败 |

## 地形导出

`debug-ai-terrain-export`将当前观察记忆写到计划目录的`terrain-<区域>-<决策号>.json`，格式`d2x-ai-terrain-v3`。包括knowledgePolicy、储存矩形、碰撞掩码与区域归属、自然图元笔画、townServices、通路kind与证据、暂缓状态、单位观察范围、已走边及可选experienceBounds。

`0xffff`为未知，储存范围不是关卡边界。`unitObservationDirections`是待观察通路数，`unitObservationTiles`是实际进入的订阅覆盖；旧v2绘制范围模型及已移除的room-tree／backtrack-plan／exploration-plan不能当作当前输入。本导出不写D2S、不回填AI。

## 慢搜索与无路现场导出

图形客户端启动前设置`D2X_NAVIGATION_DUMP`为取证根目录。诊断启动器自动设到本次navigation目录并记入run.json，结束恢复原环境值。未设置不启动诊断线程。

Worker创建`capture-<Unix纳秒>/`，常驻监视器约100ms检查活动请求，超过1000ms时异步导出冻结输入；完成后的慢计算、慢排队和空结果也可取证。普通path、teleport-route、精确交互、observed-passages及连通查询共用入口。空结果、需观察和资源受限不自动等于卡死。

| 输出 | 格式与限额 |
| --- | --- |
| search-N.json及chunk-N.u16le | d2x-navigation-capture-v2；32×32分块、小端uint16、行优先；每Worker最多128次普通异常快照 |
| slow-search-N.json | 已完成计算至少100ms，按区域／查询种类最多8份、每Worker最多512份；旧接口区域由context代次表示 |
| timeline*.jsonl | 成功与失败作业均记录；每32MiB分卷，最多8卷／256MiB，不受128快照上限限制 |
| game-<代次>-seed-<种子>/level-<区域>.jsonl | 每250ms采客户端已取得碰撞，d2x-acquired-collision-v2，按区域增量保存 |
| positions.jsonl | clientPosition正常路径脚点、serverPosition最近原包脚点，缺失null |
| captures.error／dropped | 队列、文件限额、丢弃及写盘错误；必须随材料保留 |

写线程队列最多256项／估计64MiB，满时报告dropped及incomplete。时间线每64事件刷新，异常退出可丢尾；持有快照、构造日志和写入本身都有开销。没有Grid的旅行估价只记录时间线／profile，不生成碰撞包。

### 时间线

每行有sequence、单调steadyUs及unixMs，后台事件用request关联：submitted → started → computed → result → returning → consume-begin／consumed，另有discarded、cancelled、exception。discarded设置协作取消，精确规划及连通查询在安全点退出，旧普通path可能稍后完成。

| 字段／事件 | 含义 |
| --- | --- |
| queueMs | 排队时间，不是算法耗时 |
| computeMs／suspendedMs | 算法调用经过时间扣除优先任务抢占；抢占另列，不能与profile阶段直接混算 |
| workerMs／consumeDelayMs | worker含诊断排队；returning早于future就绪，之后还含发布和前台调度，不全是可节省停顿 |
| result | outcome、expanded、候选／选中项、costUnit与成本、endpoint、点数及几何routeLength；旧path仅部分字段 |
| decision | 正常决策编号、阶段、脚点、elapsedMs及pending ready／computed；只计tickPlan，未决策调用至少10ms也记。达到20ms标runtimeSlow并在计时结束后附快照 |
| freeze-begin／end、snapshot-shared | 兼容快照物化及不可变发布号、扫描格、复制块字节、复用块；发布对象计数不是每次查询新复制量 |
| reset | 游戏／区域／geometry-changed及旧请求；未记录细分原因不猜测 |
| teleport-route的replan | 新目标、输入域或起点／耗尽变化及旧路线／游标，区分snapshotChanged与roomsChanged |
| teleport-route-transition／plan-evicted／cache-cleared | 后缀复验、续段采纳、离开终点；capacity／explicit-discard淘汰；坐标原点变化 |

request为0表示无关联请求；淘汰与reset中的continuationRequest／discardedRequests可对应随后取消。传送事件坐标沿本地格中心与coordinateOrigin解释，explicit-discard使用同context最近原点；observed-passages的points已经是全局原生脚格，不能再加原点。

观察规划`result.profile`分prepareMs（掩码／实时覆盖／目标准备）、landingsMs、searchMs、finishMs，累计calls／structuralCalls／doorBypassCalls／storageCells／expanded／landingCandidates／castCandidates。递归阶段父计时暂停，避免双计；阶段经过时间仍含抢占。plannerInput保存冻结掩码、落点碰撞、房间邻接、通路、已走边、图元及目标站位，由写线程序列化。

`navigationSearch.observedCompletions`保留最近8次消费，含修订、排队／计算／抢占／消费延迟、结果和profile；routePrefix最多16个全局脚格。observedRejections最多16个拒绝样本，含首次失败边／点、分类与中心掩码；sampleFootprint另有周围3×3掩码、原点、unknownMask及movementMask／movementSize。中心格不能证明完整角色可站。

observationPerformance记录采样、边界重建、快照、复验和拒绝原因；backgroundPerformance记录已开始任务的排队、计算及成功结果消费延迟。观察采样按游戏清空，查询／后台计数随导航对象累计，不能与旧普通path计数合算命中率。

### 碰撞与重放

增量碰撞每格为`[全局subtile x, 全局subtile y, uint16掩码]`；按行重放、同坐标后值覆盖，截到故障unixMs得到当时已取得知识。只写完整页非0xffff格，包括步行接缝两侧；不按视口／小地图过滤。离区保留最后值，不推测之后变化，累计归档独立于异常快照限额。

普通捕获的全局脚点为本地格中心加coordinateOrigin再减`(0.5,0.5)`。旧v1整图无头、小端uint16、行优先，字节数`(width+2)*(height+2)*2`，首格(-1,-1)。未知不可还原为通行，其余保留原生位，不把所有非零当墙；普通玩家mask为0x1c09、size2十字占地。

已有资源工具`d2x_assets navigation-replay <search-N.json>`仅支持candidate-plan与teleport-route，验证缺块、重叠、尺寸及输出。observed-passages尚无离线重放分派。捕获是该次算法实际持有的全部输入，不是完整世界真值，不包含未加载地形、贴图、怪物AI或任务规则，也不区分已合并掩码来源；静态传送规划不含动态单位占位。

1000ms监视器不覆盖冻结前准备、同步扫描、主线程卡顿或尚未开始的排队；永久卡住可留输入但不会自动修复或取消。性能定位先对齐request的computeMs，再结合frame-profile、动作恢复、原包与位置；具体方法见[跨区诊断](DEBUG_PIPE.md#跨区移动诊断)。固定输入重放不认证跨帧任务与执行，也不能单独证明另一条路线更快。