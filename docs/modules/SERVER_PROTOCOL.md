# 自研服务端协议与管理入口

更新：2026-10-11。本文维护服务端收发所有权、源码接口与扩展约定；消息ID／长度／参数／预留统一见[消息速查](MESSAGES.md)，功能顺序见[总计划](../architecture/MULTIPLAYER.md)。[基线](../../BASELINE.md)与玩法专题维护当前实现、原版依据及缺口；结构和编译完成不代表全部消息语义或玩法完成，自研运行只在当次交付中说明。

## 所有权

客户端只有既有RealmSession／Remote*链。TCP和内存连接均传原1.13c MCP／D2GS字节；GameHost、PersistentCharacter和内部投影不交给客户端。此处不实现BNCS账号服务；原服账号路径保留，独立账号宿主见总计划P10。

| 源码入口（相对src/hosting） | 职责 |
| --- | --- |
| embedded_realm | 组装多个内存端点与TCP/IP游戏监听，集中泵送共享宿主；持有Join本机角色租约，校验并原子写入完整回传；TCP/IP主宿主退局先保存并结束客人 |
| detail/native_realm_service | 每个参与者独立协议阶段、票据、角色名册版本／文件租约及入场准备，不拥有共享实例调度器 |
| detail/native_realm_host | 共享只读MPQ、GameHost、房间／地图目录；集中内容准备、事件路由和一次固定步推进 |
| detail/realm_connection、lan_realm | realm_connection保留内存MCP／GS分帧；lan_realm仅监听游戏端口，原6A／68／6C直连准入与回传，不提供远端MCP；断线恢复和超时，TCP实现留network/tcp_listener |
| detail/world_replication、player_replication、monster_replication | 原房间／物件／瓦片出口增删、跨区位置及多人名册／位置／穿戴外观；私有数据不广播 |
| detail/realm_protocol、realm_characters、realm_games | MCP分派与具名处理器；账号／角色、游戏票据与领域执行分开 |
| detail/game_protocol | Connected → LoggedOn → Entered → Closed；验证票据、版本、阶段，处理原握手／心跳／保存退出 |
| ../network/protocol/message_schema、client_messages.inc、server_messages.inc、lod113c | 两端共享原包身份、长度与分帧；不依赖hosting或领域 |
| protocol/message_catalog、client_messages.inc、server_messages.inc | 宿主领域、阶段、实现状态；编译期核对共享身份／长度，缺失S2C实现自动预留为stub，C2S必须有具名处理器 |
| protocol/client_stream | 有界增量分帧；块可拆开或合并消息；已消费偏移在追加时整理，避免逐包搬移整个缓冲 |
| protocol/gameplay_dispatch、handlers_* | 移动、战斗、库存、交互、成长、社交具名入口；只得到宿主、已认证玩家绑定、坐标原点和序号 |
| protocol/inventory_requests、detail/game_events | 库存原包／句柄绑定及本人不可变库存／人物／旅行事实编码；聊天依据提交时的收件人 |
| native_game_wire、native_item_wire、native_character_wire、native_combat_wire | 权威值投影成原S2C字节；JM磁盘物品不直接作网络物品 |
| administration、detail/host_administration | 类型化宿主管理命令及结果，校验实例代次和参与者；JSON／Win32管道均留在app/debug |

NativeRealmService是单个连接的生命周期编排，不是玩法容器。新增战斗／库存／任务逻辑放入server对应领域；包处理器只解码、提交领域命令和处理提交结果。不能把MPQ读取、存档编码、UI状态或每种技能实现放进GameInstance或这个编排对象。

发布阶段保持每个接收者独立的单位名册和原包顺序：NPC任务提示在同轮`publishMonsters`后直接复用该接收者的怪物成员集合；怪物动作编码只有需要隐藏未知目标时才复制完整投影；尸体装备只有实际待移除旧GUID时才准备本轮人物／公开装备保留集合。出口／NPC发布通过`GameHost::areaMetadata`同步只读借用区域元数据，出口只复制待筛选的小列表，不为这些查询复制整区静态物件规则；借用不跨宿主变更或实例退役。集合不跨发布轮次缓存，尸体归属8E仍独立于可见性发布。原版接收者更新列表参考D2MOO `GAME/Clients.cpp::CLIENTS_RefreshUnitsUpdateList`，这些投影优化不改变原兴趣资格或事件前后发布时点。

native_game_wire在本人新准入的0x59后发送原0xAA初始状态快照；monster_replication也为新进入视野的怪物／佣兵发送当前状态及数值列表。两者共用nativeStateSnapshot，即使无状态也包含0xFF终止；属性列表按当前ItemStatCost的Send Bits／Signed编码，容量不足明确拒绝。本人新准入不恢复临时属性列表，运行中变更走原A7／A8／A9；玩家光环及技能效果发布原数值列表，事件发送后同步观察缓存，避免后续空A7覆盖A9。目录的StateSnapshot已标实现，不表示运行中全状态重同步或所有他人状态组合已对齐。客户端对未收到／解码失败的状态仍保留未知，不为自研漏包默认零。原版依据与显示缺口见[人物属性](../gameplay/characters/ATTRIBUTES.md)和[技能描述](../gameplay/skills/DESCRIPTIONS.md)。

03包含幕号、地图种子、城镇关卡及本局稳定的物件控制初始种子；04不在静态入局包中预制，单连接loadPending只在本人的完整原近邻列表及本次人口／恢复就绪、房间／本人定位已发布后完成一次，失败或退局不会补发完成。客户端收到04先校验幕和本人指派，将其作为有序地图事件交给MPQ消费者；前序07／定位即使因分帧准备尚未消费，也不能提前开放本地地图输入。05／03使旧区域代次及本地完成状态失效，同幕旅行不增加03／04。两端调用链和原版边界见[传送点](../gameplay/world/OBJECTS.md#自研传送点)及[原版互通](NETWORK.md#tcpip原版互通参考与缺口)。

native_character_wire的四抗条目只发布安亚永久奖励，装备／状态／难度在共用客户端投影合成；佣兵原9E／9F／A0发布MonsterAI_SendMercStats对应的基础成长值及当前生命，装备和状态同样由原副本合成。不能把权威最终抗性或佣兵装备后总值再作为原基础列表发送。对应显示和验证边界归[人物属性](../gameplay/characters/ATTRIBUTES.md)及[佣兵](../gameplay/characters/HIRELINGS.md)。

野蛮人移动技能复用原16位置批次同步权威坐标，保留4C／4D技能动作；跳跃／旋风的动作通知也同步本人，避免普通本人预测在等待近战范围时不启动。原0D/action19按生命校正消费，不清除不可打断动作。具体范围、原版依据和缺口见[野蛮人](../gameplay/skills/BARBARIAN.md)。

内核现在提供GameCommand、command_dispatch及26个新领域State／Ports骨架，详见[内核子系统](SERVER_SYSTEMS.md)。submitGameplay统一将连接身份和当前来源区域／代次绑定到内部命令；01／03走跑及02／04出口靠近使用此入口，3A／3B已接人物成长事务，41复活及13本人尸体取回已接death；不新增私有包或客户端分支。

按原SCmd／D2PacketDef编码7A宠物归属（固定13字节），复用9D的monster ownerType公开装备及A8状态属性；73保留原pierce字节，仅按ClientSend发包。PlayerPets归属名册独立于可见单位，房间离开只发0A，不伪造归属删除；原服与自研客户端都消费相同原包。81可独立登记本人佣兵，7F非玩家分支投影本人宠物生命／区域，光标喂药使用61/body=0。头像及佣兵链的原版依据、当前范围与缺口见[佣兵](../gameplay/characters/HIRELINGS.md)。

通用十项复用原攻击／技能请求，书本／卷轴SrvDo113路由到库存与旅行。原3F现为implemented：固定8字节保留目标光标、来源GUID及技能ID，成功鉴定后取消光标；数量／物品及门户仍用原消息。它仅表示准备，不表示鉴定已成功，详见[通用技能](../gameplay/skills/GENERAL.md)。

原38的action0按NPC身份解释两个u32字段：加工为NPC／物品GUID，Warriv为NPC／目的地，Akara为NPC／parameter0。加工输出58(u32 NPC,u8 result,u8 reserved)，固定7字节，成功6／失败7沿SUnitNpc；不能由38父包状态推断所有服务已实现。任务前置与原位见[任务系统](../gameplay/quests/SYSTEM.md)，逐服务依据见[第一幕](../gameplay/quests/ACT1.md)。3C／51及23／7B保留真实充能源GUID，3E按204的16位layer和次数增量编码，不把充能写入普通21等级。装备触发输出原99单位／9A坐标包，依据SkillItem → SUnitMsg内部99／9A → SCmd投影，不冒用A3或普通施法确认；具体未实现ItemEffect／目标程序见[库存](INVENTORY.md)。没有私有ACK或客户端宿主分支。

## 消息覆盖与状态

B4覆盖原游戏端准入失败：6为票据／姓名／游戏资格错误，15为满员，16为客户端版本不符；无效存档14、未解锁难度17／18及模式19／20／21／23沿原系统错误码。Connected／LoggedOn失败进入Closed且清除票据；TCP/IP与PvPGN通过closeAfterWrites排空错误包，拒绝期间不接受后续输入，未入场角色不回写D2DBS。进入游戏后的玩法错误不伪装成入场错误。

本人资源／路径同步由提交后快照生成原18／95／96：药水预计恢复比例变化发18，生命／法力整数变化发95，耐力、移动命令确认及路径校正发96，携带当前位置减原tTargetCoord目标坐标的有符号字节偏移（D2MOO的PathGetFirstPoint名称并非第一个拐点）。只有整批字节接收成功才更新连接基线；普通本人走跑不再使用他人0F动作回包，死亡和移动技能仍保留各自动作。金币小额正增量用19，经验1A／1B为增量、1C为绝对值；属性按原255／65535边界选择1D／1E／1F。原版依据为本地PlrMsg／SCmd及既有1.13c经验位流补证，不能以D2X消费者认可替代原客户端兼容证据。

5F UpdatePosition由handlers_movement解码为MovementAction::Synchronize，经权威队列交给travel/position_sync；不取消正在使用的技能或既有跨区移动。沿本地D2MOO PlrMsg::Rcv0x5F_UpdatePlayerPos使用原整数距离（大轴加小轴一半）：死亡／正在使用技能和小于5的偏差忽略；sync_warped期间、距离小于15或大于45时，无单位移动目标则重新寻路。15–45校验当前／相邻活动房间、WALL|NOPLAYER|OBJECT的精确路径（最多77格）及普通玩家落点，失败发送原15回传本人权威位置；成功向旁观者同步位置，发布当前MPQ的sync_warped状态，不再向本人发15打断预测。跨区域先发布TravelFact以更新房间兴趣，保留仍可达的既有目的地。状态期限及单机频繁同步延长依据PlrMsg／Clients，状态和历史不写入D2S。交易期间不借同步移动角色或结束交易。

此处复用当前Grid精确寻路和已准备的双向步行边界；公共WallFollow入口与适配限制见[共用寻路](MAP.md#共用寻路)，不会以最近落点、未知区域或瓦片传送出口放行。自有客户端既有15消费者清除预测请求并记录位置断点，共用原版回包，不新增私有ACK；自有客户端按原版1.13c的位置容差主动发送5F，并保留本人当前显示位置；18／95／96不再触发本人强制位移或改写已提交目标。原版处理函数、容差及尚未复刻的房间门控见[本人位置回包](MAP.md#本人位置回包)。原版Join的出城失步及悬停问题见[怪物表现](../gameplay/monsters/PRESENTATION.md#原版加入自研宿主的待核实问题)，根因与完整原版行为仍待核对。

共享目录覆盖当前RealmSession发送及已交叉核对的11个预留请求，S2C覆盖现有1.13c长度表全部143项（含B4入局错误）。宿主实现状态独立于客户端消费；只确认身份／长度的消息为Reserved／stub，未知长度继续失败，不从旧头文件猜测。

| 方向 | 当前基础 |
| --- | --- |
| D2GS C2S | 具名入口覆盖共用客户端请求及TCP/IP查询／角色上传；完整ID、参数、阶段与stub见消息速查，未登记长度拒绝连接 |
| D2GS S2C | 覆盖1.13c共享身份；实现与预留以当前目录为准，输出拒绝stub；00／06／B2／B3已接TCP/IP加载／结束／列表／保存，AE仍拒绝分帧 |
| MCP C2S | 9种具名请求已接；游戏目录05／06、创建03与加入04支持多个房间及同局1–8人，资格按难度／专家状态／等级差／密码／容量复验 |
| MCP S2C | 9种请求对应响应已接；14排队仍为stub。05逐房间发送并终止，06按原16职业／等级槽及实际姓名编码；不存在的详情没有已核实错误包，保持超时 |
| 子命令 | 4F的18关闭仓库／19取金币／20存金币／23关闭方块／24合成已接，玩家交易2取消／3接受／4确认／7撤销确认／8报价金币已接；38的1普通商店／2赌博及0携带NPC／物品GUID的任务加工已接；0按NPC身份解释最后字段，Warriv旅行目的地40／1、Akara重置parameter0已接。子命令分别登记，不由父包状态推断全部支持 |

状态区分`implemented`、`admission-only`、`partial`和`stub`；4F父包为partial，子命令各自登记。9C／9D已有库存增量，97／23已有切组／选技回复，1F／21已有人物增量，28／5D已接五幕个人任务主流程，但不能据此宣称全部物品、技能或组队任务完成。两端复用network/protocol分帧，宿主目录不维护独立长度算法。编码参数见[消息速查](MESSAGES.md)，具体玩法资格归各专题。

0x0E已接传送点启动模式通知，首次中性点击不发0x63，ENDANIM按原版静默完成；0x63只用于mode1／2点击后打开菜单，使用原0x102＋112位历史。原0x49关闭／同区域／非法目的地及异步内容准备边界见[传送点](../gameplay/world/OBJECTS.md#自研传送点)；其他物件模式仍需按各自原程序核对。

库存GUID在宿主端通过inventoryInput绑定服务端ItemHandle；原包不增加revision或请求ID。请求在固定步复验来源、位置、活动武器组和交换目标，成功经InventoryFact生成原9C／9D／0A，切组另发97及23；同笔CharacterFact发原1F／21人物增量；可靠通知与可覆盖的PlayerSnapshot.command分开。一次队列写入包含整笔原字节包，接收成功才确认Outbox；失败终止连接并保留实例／存档租约。无通用成功ACK或错误fallback；详细规则／暂缓范围见[物品目录](../gameplay/items/README.md)。

原C2S 2A直接入方块、63背包自动入腰带与37最后购入物品鉴定已接；37不允许任意免费鉴定，2A不授予合成授权。D2X快捷入带同步发送63，调试入口提供cube-store／belt-auto；原版依据和未核实边界见[物品核对](../gameplay/items/EVIDENCE.md#客户端与服务端物品核对)。

库存移入Cursor的原9D/action5／8及相关移除回包保留事实before中的来源body、坐标和InvPage；新mode4不等于这些字段全部归零。装备与卸装x对应已映射活动武器组的原body，存储拿取保留来源S2C page。来源仅供原包移除位置，不改变权威Cursor或磁盘位置；自有客户端依mode消费，未靠私有ACK补偿。原版Join现场与原Host回包差异、修正版验证边界见[职业装备对照](../gameplay/items/EVIDENCE.md#职业装备与原版拒绝对照)。

具名stub返回NotImplemented，由宿主按包号统计，不修改角色或世界，不发伪造成功包。普通客户端继续原协议超时／无确认语义；开发者通过server-protocol查询明确状态，不把私有错误塞进原包。stub目前只保证注册、帧边界、连接阶段和分派，尚不承诺完整字段或玩法资格校验。未知包长拒绝连接；已识别但未知的4F／38子操作拒绝执行。

移动提交的Queued只表示进入权威FIFO；Applied／NoRoute等实际结果来自后续固定步，server-status.command给出最近实际结果。内部movementSequence单独触发原移动回复，不把其他领域的命令完成当成移动。即时拒绝移动会回当前权威位置；相同静止姿态不随每个tick重复广播。Scaffold领域在入队前返回NotImplemented。按包统计、最后一次分派结果、失败信息均有界保存，计数按当前宿主生命周期累计，诊断不输出握手票据或认证包。

普通攻击和技能释放入口共用一个字段解码器，仍保留每个原包的左右手、目标类型、持续与原地标志。目标按技能原表及领域资格校验，可含敌怪、友方玩家／单位、原物件或物品，技能取服务端当前武器组的选择；3C仅开放已准备的技能来源及原鉴定／回城物品技能，未经实现来源明确拒绝，不能信任客户端GUID替代操作者。具体支持范围见[近战链](SERVER_SYSTEMS.md#旧近战链与随机迁移)、[女巫](../gameplay/skills/SORCERESS.md)、[亚马逊](../gameplay/skills/AMAZON.md)及[通用技能](../gameplay/skills/GENERAL.md)。目录implemented表示具名入口已执行已支持的意图，不表示全部技能已实现。玩家41复活由death规划，不能由客户端本地恢复生命。

LifeFact／AttackFact／HitFact是内部值，不跨传输；EventOutbox.publishGroup原子预留批数及事实容量，扣血后不会漏掉本人生命或公开受击输出。怪物身份、移动、生命和死亡基线留在独立monster_replication，战斗不增加客户端分支或私有包。

## 世界与多人连接

目标接近在权威路径旁保留单位类型／GUID：他人玩家走跑用10，怪物目标路径用68（当前位置、目标、步数、pathType、速度），坐标路径仍用67。接收方尚未分配目标时退回坐标投影。怪物目标格挡走6A，无目标普通攻击及不可见目标攻击走6B坐标动作，6B包含目标坐标和当前位置，不是停止确认；普通停止保留6D。原有4C／4D技能动作与16移动技能位置同步不被普通走跑覆盖。

任务入场先按当前1.13c初始化表顺序发送5E的37个bNotIntro，顺序由公共quest/catalog唯一声明，自有客户端使用相同映射；再与40刷新从quests权威投影发送29公共标志、28私人记录、50附加进度与52完整状态数组（41个filter槽）。增量5D使用chain编号，不与52槽混用。公共CompletedBefore只由建局角色初始化，后加入者刷新不改写房间世界状态；各事件按任务规则冻结资格，对错过者投影CompletedNow／状态12并实际拒绝任务推进／奖励，保留个人已存及待提交的奖励资格。组队范围、古代人最终死亡／等级条件、都瑞尔与泰瑞尔阶段及地图邻接限制统一见[任务系统](../gameplay/quests/SYSTEM.md#资格准备与提交)。药剂、加工及旅行的任务增量也从quests取得完整房间上下文，避免覆盖公共标志。个人存档格式不变，房间公共标志与临时资格不写入D2S；全部任务与原版客户端组合仍未对齐。

尸体首次可见按59分配、死亡动作、74 PLAYERBODY关联投影；8E独立维护全房间尸体归属，后来加入也获得完整名册。区域卸载只移除可见单位，不注销归属；拾回／移除尸体才发送带真实所有者的8E取消。归属与可见性基线都在批次发送成功后提交。

光标原件耗尽／消耗或死亡转入尸体时，InventoryFact的原位置决定原42 CLEARCURSOR输出；它以所属玩家为目标，与按物品GUID的0A删除分开。42位于同笔增量最前，随后再投影删除及新光标原件，覆盖合堆、入书、镶嵌、光标喂药和死亡的公共事务路径。仅更新数量或正常移动／交换不清光标，失败请求没有42。

内存`connect`对应主客户端，`attach`可附加独立客户端。`listen`接受LAN MCP／D2GS，全部连接共用NativeRealmHost而各自保有RealmConnection；模拟只在共享调度入口推进，人数不影响时间速度。客户端仍通过RealmSession和TcpStream／MemoryTransport收发相同原包，只有连接组装不同。

GS接受TCP后发原AF00；收到68才凭hash／token寻找对应MCP入局会话，验证职业／姓名／签名／版本并一次消费票据。MCP按原客户端惯例关闭后，待入局会话保留15秒；握手和队列有界，输入错误或输出背压退役该连接。正常69先归一化交易并等待冻结任务奖励／死亡结算，保存成功后发送05／06及B0，TCP先排空确认再关闭。TCP/IP先回传B3，05／06结束原客户端游戏循环，B0为系统消息确认；D2X收到系统B0后持续消费至06，不提前关闭。退局等待客户端主动关闭，最多15秒后启动有界排空关闭；准入拒绝仍直接排空B4。断线仅保存／移除本人；失败存档保留实例和租约供宿主管理恢复，不暂停同局其他玩家。

13/type=5已接原UNIT_TILE旅行，原07／08／09／0A同步区域与出口，15位置包用于瓦片换区；自然边界沿移动连续过渡。5B／59／0D／0F／9D／0A／5C同步名册和区域可见玩家。公开9D只包含穿戴外观并隐藏属性列表；背包／Cursor／私人箱／人物成长增量仅发本人。15广播／私聊与26原回包、14头顶消息与76到期清除、5D(2,3,4)聊天屏蔽／敌意及75／8C关系投影已接；第一幕六项28／5D及石柱50、传送点63、本人门户51／60／82及物品技能数量22已接；Warriv／Meshif双向跨幕及第二幕六任务已接；第三至第五幕个人任务已接，玩家交易已接独立trade，队伍已接邀请／接受／离队及同幕任务目标资格，敌意按有向关系和原城镇／等级／冷却限制执行；普通近战怪物使用AC／67／69／6C／6D及0C投影，67沿原走路action=1／跑动action=23携带当前速度百分比，玩家攻击及技能使用带真实技能ID／等级的4C／4D，生命／法力属性1F只发本人，同区域传送用公开15。火弹／火球已由客户端施法动作派生视觉，不再重复发送73。

5A/type2／3广播真实入退局，color4、value0、param0、两个16字节姓名／账号槽；本地角色的账号槽为空。PvP死亡的type6使用受害者／击杀者姓名，value为原职业编号、param0；敌意变化为type7/Party3／4，冷却拒绝为type16及声音13。75关系按接收者→目标，8C保留from→to，不用反向关系替代；首次名册后补全当前关系对。D2X按同一原回包消费，伤害、状态和死亡继续复用原玩家资源／模式包；5A／75／8C的完整原版动态核对尚未完成。

人物命中／生命修正沿PlrMsg使用0D动作19和整数生命0..100，不发怪物0C、不把19当GH；怪物0C沿MonsterMsg使用旗标19、0..128原比例大于1时减一及原命中类型。组件池、SH格挡、HitClass准备见[怪物COMMON](../gameplay/monsters/COMMON.md)，原版Host的普通怪物消息观察见[怪物表现](../gameplay/monsters/PRESENTATION.md#原版加入自研宿主的待核实问题)。投影使用原协议，客户端不增加自研分支。

玩家兴趣粒度为本人区域及已准备的直接自然邻区；怪物进一步以本区RoomLayout邻室／邻区距离过滤，尚非完整原版房间兴趣。TCP/IP双方使用各自本机角色目录，独立无图形EXE则经PvPGN D2CS／D2DBS取得票据和角色锁，不自行提供BNCS账号。独立路径复用NativeRealmService的原游戏分派与输出，通过admitExternal／externalSave接入后端，不经过本地MCP选角或CharacterStore租约。部署／后端协议见[PvPGN服务端](../development/PVPGN_SERVER.md)，嵌入入口见[NETWORK](NETWORK.md#tcpip直连与自研宿主入口)；历史同机TCP证据不能认证新独立服务的多人／跨机器行为。

## 原版互通参考

以下保留原版1.13c客户端的断言／接收行为及原版Host输出。原版客户端连接D2X宿主的记录只能说明原消费者的反馈；原版服务端规则另由反向观察和本地原函数确认。全部B4拒绝码、跨区尸体、后来加入名册与任务特殊资格仍有缺口。

2026-10-11原版1.13c Join本机D2X Host先后触发D2Client RVA`0xAEE11`／line2474本人属性断言、RVA`0xAE8B2`／line4023物品动作断言及RVA`0x4C1E3`／line96任务初始化断言。当前入场先59创建玩家再0B指定本人；库存按动作选择9C／9D，背包、腰带和光标初始项不使用原版9D回调不接受的action4／14／18；NPC出现前发送5E。原1.13c D2Game RVA`0x10DC00`的37项表与D2MOO 1.10f顺序不同，slot41位于index7，不能直接照抄旧表。5F初始化本局门户访问位，后续区域访问位更新仍有缺口。

同次原版客户端点击阿卡拉触发D2Common line274断言，原因是27发满8条而原版要求count小于8；当前两端均限制最多7条。原版Host的27→29→带NPC身份28链另揭示交谈确认要求，2F只确认已有会话。原D2Game RVA0x2DCF7..0x2DD31确认05／06游戏结束包及系统B0发送，完整线上顺序另见[原版退出参考](NETWORK.md#tcpip原版互通参考与缺口)。

用户在修正后确认原版可交谈／领奖、查看任务页、保存退出及重入，但补充截图仍显示Important News／Urgent Matters代替正常服务菜单。当前27已移除普通闲谈，任务表menu1转换为回包menu0，并按血乌领奖后的任务序列开放凯恩介绍。完整原版菜单及扩展玩法仍待核对，不能由部分交互成功推断全部菜单一致。

同日由用户创建原版1.13c Host，D2X的Hero进入城镇后移动、消耗一瓶腰带药水、靠近阿卡拉交谈并确认对白，再保存退出。453条分帧包共6217字节，显示59→5E→28／29→0B→5F→属性→9D/06装备及9C/14腰带、9C/04背包；退局收到四段B3及B0。该次D2X在B0处停止接收，未知长度而无法分帧的原始TCP字节也未记录，因此不能称为完整退局交换；当前已改为等待06。原服48／65／7E等未消费消息仍是协议缺口。

### 原版 Host 与自研 Host 对照

2026-10-12使用当前MPQ、原版1.13c TCP Host及D2X接收端，在地图种子1496403436下观察隔离角色。ProtocolGuest为1级、5000金币、旧Den奖励已领，ShotSorceress为90级女巫且已学30项技能；这些是准备条件，不能当自然成长依据。原版房间由未完成第一幕任务的Hero创建。

原版基础交换1176帧／20007字节，扩展战斗2619帧／45326字节，装备出售653帧／16355字节。以下结论限于已收到的完整分帧包；未知长度导致不能分帧的TCP字节不在观察范围内。

| 路径 | 已取得的原版证据及修正 |
| --- | --- |
| NPC对白／任务介绍 | 旧Den已领奖角色只收Akara12、Kashya24首次介绍；首次确认后Kashya不提供81。新角色Akara先64任务、后12介绍，均为menu0。90级角色在创建者未救Cain的房间，Charsi只收36介绍，不自动提供146。任务序列按创建者与本局目标资格开放，27不附加普通闲谈，任务与介绍分别确认 |
| 介绍保存／本局任务位 | 原版介绍确认写52字节段的0x04 QuestIntroFlags，Akara／Kashya为0x0C；0x1C NpcIntroFlags独立保留，不补写bit0。当前D2Common RVA0x2D140清除slot0..41的旧PrimaryGoalDone／CompletedNow；旧Den重置资格入场为8002，不能继续发送2002 |
| 买卖／装备 | 药水30、普通腰带60；已装备短杖出售589、回购1134。成功出售后可能迟到9D/08卸装备；客户端不会再生成已售原件的Cursor。卖出复制新的NPC货架GUID，买入分配本人新GUID；购买回执flags4指向新购原件，可用空槽按原资格自动装备 |
| 库存／仓库／恢复 | 原版已观察药水消耗、背包与stash往返、金币存取、腰带购买自动装备、卸下／丢弃／拾取，以及保存后新进程重入。原版复验后金币4365、腰带及回购短杖保留，Cursor为空 |
| 部分修理 | 原版3/7耐久、报价12、随身5金币后为4/7且金币0。恢复按10位单位成本，只修耐久，不恢复充能／堆叠／破损；该样本不覆盖全部物品逐整数报价 |
| 战斗／技能 | 原版确认充能弹、火球、冰爆、近战、陨石、暴风雪、冰封球及Nova的命中／死亡；冰封装甲10、能量护盾30、Enchant16状态，传送位置、九头蛇351／352／353、武器组97和技能热键均有对应回包。接收端发送成功不能作为命中依据；具体技能条件及缺口见[女巫](../gameplay/skills/SORCERESS.md) |
| 正常退局 | 本轮原版交换均收四段B3、B0、05、06并回到选角，未将B0提前当成完整关闭。隔离角色新进程重入已核对金币／装备，保留旧用户存档 |

仍待核实的差异：原版NPC有行走，自研城镇NPC仍静止；同地图种子的野外怪物种类／数量／位置尚未逐项对应，战斗采集部分阶段人数也不同，不能据此确认同怪伤害、掉落概率或数值完全一致。该次原版FireBolt／FrostNova／Static／Inferno未取得充分有效目标命中依据，Static百分比仍缺同条件原版样本。原版还发送47／48／65／7E而D2X未消费，不能把已分帧猜作已完成玩法。后续区域5F访问位、全部职业／品质／充能／配方／佣兵／玩家交易组合及PvPGN后端仍缺完整核对。原版Join→D2X Host的NPC菜单和扩展玩法也未完成，反向观察不能替代它。

## 新增一个玩法的顺序

2026-10-12野外原版Host对照后的源码补齐：新进入视野的怪物与NPC沿AC→AA→当前动作发布，AA保留MPQ的alignment状态，MonStats Align枚举映射到原单位阵营，零值不虚构属性列表。AB按原0..128生命刻度向敌方观察者发布，变化超过4或归零时更新；GH／BL／死亡不重复发送软命中0C。普通67／68携带实际路径类型、步数、最大距离和统一速度贡献，末点不再取下一折点；Toward失败改走原WallFollow，原版和D2X消费同一原包。完整依据、实测日志与尚待原版复验的悬停问题见[怪物协议](../gameplay/monsters/PRESENTATION.md#原版加入自研宿主的待核实问题)。

第二幕新增原C2S0x44 StaffUpdate（17字节：player／object／item各u32、state与reserved各u16），连接绑定player，state2取消／3提交，reserved须零；0x31消息396路由已授权的Horazon对象滚动消息。S2C0x27保留原unitType2，0x58保留原结果／末字节状态，0x50任务号1只向有阅读资格的本人发送真墓偏移，0x53沿原10字节环境格式发送。日志、物品和旅行仍由原事实编码链发布，不附加私有请求号。具体入口及依据见[第二幕](../gameplay/quests/ACT2.md)。

1. 先核实1.13c身份、方向、长度和参数，再登记共享message_schema目录；不直接照搬D2MOO旧版本／自定义包。在对应handlers接管具名stub，解码完整字段，核实范围、类型、所有权与服务上下文。分派器统一finish，处理器仍须在副作用前验证字段；原规则核对MPQ／reference，不猜错误码和ACK。
2. 向server对应领域提交纯值命令，操作者取连接绑定。GUID、金币、报价、技能等级等客户端数据不视为权威；排队后再次核对失效的目标／区域／事务上下文。
3. 在权威固定步内校验并原子执行。失败不部分扣费、移物或改档；不能在网络回调直接写库存，也不能因接收重试重复结算。
4. 将结果和事件投影为已核实的原包，复用encodeServerPacket及共同长度校验，再更新宿主状态。S2C仅登记身份默认stub，不可输出；C2S分派引用具名函数，缺函数不能落入默认成功。客户端统一发送出口也校验C2S准确长度，TCP／内存相同。
5. 参数／长度只更新消息速查，分层只更新本页，玩法条件、原版依据与缺口更新所属专题；自研执行结果只在当次交付中说明。持久化语义变化须同步格式、校验条件和拒绝边界；原包不能附加私有请求号或GameHandle。

不预先为每个未来技能创造空类、万能命令字典或通用fallback。结构上的消息覆盖与玩法完成度分别维护；不能通过将目录改为implemented来替代领域实现。

## 管理与恢复

named pipe → app/debug/server_commands → AdminRequest → 宿主调度线程。管理入口是应用显式持有的能力，不来自客户端游戏连接；连接原服时拒绝所有宿主管理命令。当前宿主与pipe回调同线程，因此直接执行类型化请求，不制造一个立即同步等待的假队列；将来迁移宿主线程时在此端口增加有界请求／结果队列，不能让管道线程直接访问GameHost。

save、load、cancel-load、step、grant-experience／grant-gold、player-damage、refill-resources、travel及物品／怪物生成、monster-damage／monster-kill已接；管理travel准备真实地图但不授予任务或传送点资格。祭坛／佣兵／重置等其他管理意图仍保留明确NotImplemented入口，不能与正常原包已接服务混淆。grant-experience校验正数／绑定／入场，GameHost委托progression产生经验与升级事务，立即共用原事件输出；暂停仍可授予，重复来源及封顶拒绝。F11与pipe save调用同一保存接口；Ctrl+F11与load调用同一候选入场准备，再走原69退局、MCP选角、原D2GS重新入局。候选游戏保持暂停，重新取得的文件字节、角色名、难度必须匹配；不重新生成地图，也不悄悄换成别的角色。

保存失败保留当前实例和租约；重载准备失败保留旧实例；退出或取消释放候选实例。正常连接关闭保留已排队的B0，丢弃旧客户端输入；错误关闭丢弃部分入场输出。命令名、参数、返回状态和示例见[调试管道](../development/DEBUG_PIPE.md#嵌入宿主管理命令)。


原27为target GUID后source GUID，由inventory在固定步同时识别目标与消耗卷轴／书本；原50为本人GUID和金币数，由inventory／transactions成组扣钱包并安装地面金堆。原51解包技能／左右手／slot及owner，当前仅接受已有F1–F8与owner=UINT32_MAX；成功写CharacterRecord并发7B，D2S沿既有字段保存。其他槽／物品所有者技能仍未开放，不能用普通Attack回退。原13的门户GUID显式进入travel，普通物件缺身份不猜测特殊旅行。

## 女巫原包扩展

原4C／4D统一主动技能动作、Inferno转向和Hydra技能337；73只允许MPQ ClientSend弹体，静止弹体FirstX／Y为0；ClientSend是视野同步资格，并非强制创建广播。原服已核对的Blaze／FireWall／Meteor／Blizzard及其子火段常规创建由动作／状态重建，不重复发送73；本人普通动作省略4C／4D，其他可见客户端仍发送（原强制本人标志尚无执行入口）；A3编码ThunderStorm目标瞬时事件；A7／A9编码怪物／友方单位状态，11与2C编码原叠层／u16音效。67/action20使用原击退来源点、三格距离及pathType11。不存在自研专用客户端技能消息。具体执行和表现范围见[女巫技能](../gameplay/skills/SORCERESS.md)。

当前怪物投影沿原AC的五位champion／unique／superunique／minion／ghostly、固定hcIdx、词缀终止和nameSeed；没有新增自研阶级包。人物MonProp击退使用原0F/action20（KB），命中／生命仍为0D/action19，隐藏Kick由原技能动作呈现。状态值经原ItemStatCost ID及A7／A9投影，敌对弹体仍尊重ClientSend／NoMultiShot／NoUniqueMod／NextHit。具体实现及未认证边界见[精英](../gameplay/monsters/ELITES.md)及[原包表现](../gameplay/monsters/PRESENTATION.md)。

怪物0C／19与69/action6生命最高位为电强化发射资格；69 GH方向槽为生命字节，阶级只由AC传输。ClientSend为空的194／195不强发73，公开触发位由effects／monsters投影，视觉由共用原包回调驱动。依据与分层见[原包表现](../gameplay/monsters/PRESENTATION.md)及[怪物模块](MONSTERS.md)。
