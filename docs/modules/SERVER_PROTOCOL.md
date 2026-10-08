# 自研服务端协议与管理入口

更新：2026-10-08。本文维护源码接口与扩展约定；功能顺序见[总计划](../architecture/MULTIPLAYER.md)。此前行走包有有限冒烟，见[基线](../../BASELINE.md#当前运行包与有限冒烟)；本批已完成Windows Release构建、打包及有限冒烟，具体范围与限制见基线，消息目录不代表全部玩法已实现。

## 所有权

客户端只有既有RealmSession／Remote*链。TCP和内存连接均传原1.13c MCP／D2GS字节；GameHost、PersistentCharacter和内部投影不交给客户端。此处不实现BNCS账号服务；原服账号路径保留，独立账号宿主见总计划P10。

| 源码入口（相对src/hosting） | 职责 |
| --- | --- |
| embedded_realm | 组装多个内存端点与LAN监听，集中泵送共享宿主；close只退主连接，shutdown保存全部参与者后停服 |
| detail/native_realm_service | 每个参与者独立协议阶段、票据、角色名册版本／文件租约及入场准备，不拥有共享实例调度器 |
| detail/native_realm_host | 共享只读MPQ、GameHost、房间／地图目录；集中内容准备、事件路由和一次固定步推进 |
| detail/realm_connection、lan_realm | selector／拆包状态、TCP连接与原68票据的绑定、断线保存和超时；TCP实现留network/tcp_listener |
| detail/world_replication、player_replication、monster_replication | 原房间／物件／瓦片出口增删、跨区位置及多人名册／位置／穿戴外观；私有数据不广播 |
| detail/realm_protocol、realm_characters、realm_games | MCP分派与具名处理器；账号／角色、游戏票据与领域执行分开 |
| detail/game_protocol | Connected → LoggedOn → Entered → Closed；验证票据、版本、阶段，处理原握手／心跳／保存退出 |
| protocol/message_catalog、client_messages.inc、server_messages.inc | 原包身份、长度、所属领域、阶段、实现状态；目录供拆包、分派、输出和诊断共同使用 |
| protocol/client_stream | 有界增量分帧；块可拆开或合并消息；已消费偏移在追加时整理，避免逐包搬移整个缓冲 |
| protocol/gameplay_dispatch、handlers_* | 移动、战斗、库存、交互、成长、社交具名入口；只得到宿主、已认证玩家绑定、坐标原点和序号 |
| protocol/inventory_requests、detail/game_events | 库存原包／句柄绑定及本人不可变库存／人物／旅行事实编码；聊天依据提交时的收件人 |
| native_game_wire、native_item_wire、native_character_wire、native_combat_wire | 权威值投影成原S2C字节；JM磁盘物品不直接作网络物品 |
| administration、detail/host_administration | 类型化宿主管理命令及结果，校验实例代次和参与者；JSON／Win32管道均留在app/debug |

NativeRealmService是单个连接的生命周期编排，不是玩法容器。新增战斗／库存／任务逻辑放入server对应领域；包处理器只解码、提交领域命令和处理提交结果。不能把MPQ读取、存档编码、UI状态或每种技能实现放进GameInstance或这个编排对象。

内核现在提供GameCommand、command_dispatch及26个新领域State／Ports骨架，详见[内核子系统](SERVER_SYSTEMS.md)。submitGameplay统一将连接身份和当前来源区域／代次绑定到内部命令；01／03走跑及02／04出口靠近使用此入口，3A／3B已接人物成长事务，41复活及13本人尸体取回已接death；不新增私有包或客户端分支。

## 消息覆盖与状态

目录逐项来自当前RealmSession的实际发送代码，以及RealmSession、RemoteWorld、RemoteSocial的实际消费入口。不是将旧版本全部包长复制后默默接受未知包。

| 方向 | 当前基础 |
| --- | --- |
| D2GS C2S | 60种均登记具名入口；已接移动／生命周期、普通攻击及已实现技能、基本库存／拾取／丢弃／使用、NPC普通服务、人物成长、请求任务／复活、传送点及本人门户。新增27单件鉴定、50丢金币、51绑定F1–F8。仓库须真实交互授权；22分堆、28镶嵌及其余未接范围明确stub或拒绝，详见目录及各领域 |
| D2GS S2C | 90种客户端已消费消息具有ServerMessage身份和输出目录。encodeServerPacket提供统一具名编码入口；已有编码接入，其余禁止生成空包假装实现 |
| MCP C2S | 9种具名请求已接；游戏目录05／06、创建03与加入04支持多个房间及同局1–8人，资格按难度／专家状态／等级差／密码／容量复验 |
| MCP S2C | 9种请求对应响应已接；14排队仍为stub。05逐房间发送并终止，06按原16职业／等级槽及实际姓名编码；不存在的详情没有已核实错误包，保持超时 |
| 子命令 | 4F的10种动作中，18关闭仓库／19取金币／20存金币已接，交易及方块仍stub；38的3种服务中，1普通商店已接，0 NPC旅行／2赌博仍stub。子命令分别登记，不由父包状态推断全部支持 |

状态区分`implemented`、`admission-only`和`stub`。9C／9D已有库存位置增量，97／23已有切组／选技回复；9C另投影地面金币／物品，implemented不表示全部物品来源或施法已实现。1F属性及21有效技能已有实时人物增量，0A覆盖耗尽物品、可见玩家／装备／怪物移除；28及5D已接邪恶洞穴增量；29及其余任务初始化不表示其他任务执行已恢复。S2C长度统一调用现有lod113c_packet_size；不维护第二份回包长度表。3A属性分配按当前客户端的原1字节ID＋2字节打包参数拆帧。

库存GUID在宿主端通过inventoryInput绑定服务端ItemHandle；原包不增加revision或请求ID。请求在固定步复验来源、位置、活动武器组和交换目标，成功经InventoryFact生成原9C／9D／0A，切组另发97及23；同笔CharacterFact发原1F／21人物增量；可靠通知与可覆盖的PlayerSnapshot.command分开。一次队列写入包含整笔原字节包，接收成功才确认Outbox；失败终止连接并保留实例／存档租约。无通用成功ACK或错误fallback；详细规则／暂缓范围见[库存](INVENTORY.md#自研服务端库存切片)。

具名stub返回NotImplemented，由宿主按包号统计，不修改角色或世界，不发伪造成功包。普通客户端继续原协议超时／无确认语义；开发者通过server-protocol查询明确状态，不把私有错误塞进原包。stub目前只保证注册、帧边界、连接阶段和分派，尚不承诺完整字段或玩法资格校验。未知包长拒绝连接；已识别但未知的4F／38子操作拒绝执行。

移动提交的Queued只表示进入权威FIFO；Applied／NoRoute等实际结果来自后续固定步，server-status.command给出最近实际结果。内部movementSequence单独触发原移动回复，不把其他领域的命令完成当成移动。即时拒绝移动会回当前权威位置；相同静止姿态不随每个tick重复广播。Scaffold领域在入队前返回NotImplemented。按包统计、最后一次分派结果、失败信息均有界保存，计数按当前宿主生命周期累计，诊断不输出握手票据或认证包。

普通攻击和技能释放入口共用一个字段解码器，仍保留每个原包的左右手、目标类型、持续与原地标志。目标只允许当前切片中的怪物，技能取服务端当前武器组的选择；3C仅开放已准备的技能来源及回城物品技能，未经实现来源明确拒绝，不能信任客户端GUID替代操作者。具体支持范围见[普通近战](SERVER_SYSTEMS.md#普通近战切片)及[女巫技能](SERVER_SYSTEMS.md#女巫主动技能案例)。目录implemented表示具名入口已执行已支持的意图，不表示全部技能已实现。玩家41复活由death规划，不能由客户端本地恢复生命。

LifeFact／AttackFact／HitFact是内部值，不跨传输；EventOutbox.publishGroup原子预留批数及事实容量，扣血后不会漏掉本人生命或公开受击输出。怪物身份、移动、生命和死亡基线留在独立monster_replication，战斗不增加客户端分支或私有包。

## 世界与多人连接

内存`connect`对应主客户端，`attach`可附加独立客户端。`listen`接受LAN MCP／D2GS，全部连接共用NativeRealmHost而各自保有RealmConnection；模拟只在共享调度入口推进，人数不影响时间速度。客户端仍通过RealmSession和TcpStream／MemoryTransport收发相同原包，只有连接组装不同。

GS接受TCP后发原AF00；收到68才凭hash／token寻找对应MCP入局会话，验证职业／姓名／签名／版本并一次消费票据。MCP按原客户端惯例关闭后，待入局会话保留15秒；握手和队列有界，输入错误或输出背压退役该连接。正常69保存成功才发送B0，TCP先排空确认再关闭。断线仅保存／移除本人；失败存档保留实例和租约供宿主管理恢复，不暂停同局其他玩家。

13/type=5已接原UNIT_TILE旅行，原07／08／09／0A同步区域与出口，15位置包用于瓦片换区；自然边界沿移动连续过渡。5B／59／0D／0F／9D／0A／5C同步名册和区域可见玩家。公开9D只包含穿戴外观并隐藏属性列表；背包／Cursor／私人箱／人物成长增量仅发本人。15聊天请求及26同局广播已接；邪恶洞穴28／5D、传送点63、本人门户51／60／82及物品技能数量22已接；队伍、敌意、交易、其余任务及NPC旅行仍未实现；普通近战怪物使用AC／67／69／6C／6D及0C投影，67沿原走路action=1／跑动action=23携带当前速度百分比，玩家攻击及技能使用带真实技能ID／等级的4C／4D，生命／法力属性1F只发本人，同区域传送用公开15。火弹／火球已由客户端施法动作派生视觉，不再重复发送73。

玩家兴趣粒度为本人区域及已准备的直接自然邻区；怪物进一步以本区RoomLayout邻室／邻区距离过滤，尚非完整原版房间兴趣。当前没有BNCS账号与独立无图形宿主发行；LAN使用宿主共享角色目录，详见[联网入口](NETWORK.md#局域网自研宿主入口)。历史包有同机TCP双进程有限证据，本轮六项未复验多人／跨机器路径。

## 新增一个玩法的顺序

1. 在对应handlers文件接管已有具名stub；解码完整字段并finish，核实原版本、范围、目标类型、所有权和服务上下文。原规则先核对当前MPQ／本地reference，不猜错误码和ACK。
2. 向server对应领域提交纯值命令，操作者取连接绑定。GUID、金币、报价、技能等级等客户端数据不视为权威；排队后再次核对失效的目标／区域／事务上下文。
3. 在权威固定步内校验并原子执行。失败不部分扣费、移物或改档；不能在网络回调直接写库存，也不能因接收重试重复结算。
4. 将结果和事件投影为已核实的原包，复用encodeServerPacket和共同长度校验，再更新目录状态。新增包在目录登记后，分派会引用它的具名函数，缺函数不会落入通用成功或默认stub。
5. 更新对应领域与保存文档。持久化语义变化须同步格式／规则指纹和明确拒绝边界；原包不能附加私有请求号、GameHandle或规则指纹。

不预先为每个未来技能创造空类、万能命令字典或通用fallback。结构上的消息覆盖与玩法完成度分别维护；不能通过将目录改为implemented来替代领域实现。

## 管理与恢复

named pipe → app/debug/server_commands → AdminRequest → 宿主调度线程。管理入口是应用显式持有的能力，不来自客户端游戏连接；连接原服时拒绝所有宿主管理命令。当前宿主与pipe回调同线程，因此直接执行类型化请求，不制造一个立即同步等待的假队列；将来迁移宿主线程时在此端口增加有界请求／结果队列，不能让管道线程直接访问GameHost。

save、load、cancel-load、step、grant-experience／grant-gold、player-damage、refill-resources及物品／怪物生成、monster-damage／monster-kill已接；旅行／祭坛／佣兵／重置等保留类型化参数和明确NotImplemented入口。grant-experience校验正数／绑定／入场，GameHost委托progression产生经验与升级事务，立即共用原事件输出；暂停仍可授予，重复来源及封顶拒绝。F11与pipe save调用同一保存接口；Ctrl+F11与load调用同一候选入场准备，再走原69退局、MCP选角、原D2GS重新入局。候选游戏保持暂停，重新取得的文件字节、角色名、难度必须匹配；不重新生成地图，也不悄悄换成别的角色。

保存失败保留当前实例和租约；重载准备失败保留旧实例；退出或取消释放候选实例。正常连接关闭保留已排队的B0，丢弃旧客户端输入；错误关闭丢弃部分入场输出。命令名、参数、返回状态和示例见[调试管道](../development/DEBUG_PIPE.md#嵌入宿主管理命令)。


原27为target GUID后source GUID，由inventory在固定步同时识别目标与消耗卷轴／书本；原50为本人GUID和金币数，由inventory／transactions成组扣钱包并安装地面金堆。原51解包技能／左右手／slot及owner，当前仅接受已有F1–F8与owner=UINT32_MAX；成功写CharacterRecord并发7B，D2S沿既有字段保存。其他槽／物品所有者技能仍未开放，不能用普通Attack回退。原13的门户GUID显式进入travel，普通物件缺身份不猜测特殊旅行。
