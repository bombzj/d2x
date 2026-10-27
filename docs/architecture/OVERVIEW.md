# 代码结构与依赖

PD2专项内容：[客户端接入](../pd2/CLIENT_INTEGRATION.md)。本页保留原版／公共规则及必要的版本边界。

本页维护当前模块分工、状态所有权、依赖方向和跨层生命周期；源码入口以CMake为准。客户端和独立服务端分别构建／打包，实现范围见[基线](../../BASELINE.md)，原版依据和缺口见对应专题，功能顺序见[总计划](MULTIPLAYER.md)。目标设计不等于已实现架构。

## Agent速览

**一套客户端，两种权威来源。** Single Player／自研TCP/IP使用自研宿主，原服使用D2GS；入局共用D2GS请求与同一客户端副本。TCP/IP按原版直连游戏端口并交换本机角色字节，选角复用内部内存MCP；账号Realm路径继续使用远端MCP。单机是内存字节传输，断线不切换成本地权威。

| 大模块（相对src/） | 放什么／不能放什么 |
| --- | --- |
| `app`／`server_main` | 程序组合、平台输入、启停与调试；不实现另一套玩法 |
| `presentation`／`client` | UI、动画、声音、只读副本与请求；不裁决命中、库存、奖励或保存 |
| `network` | 原协议、连接、字节传输与客户端会话；不决定游戏规则 |
| `hosting` | 内容准备、协议适配、角色存储、实例调度；调用内核，不在会话里结算玩法 |
| `server` | 权威领域状态、命令、25Hz执行、事务和可靠事实；不读取MPQ／文件／socket／设备／GPU／Win32 |
| `gameplay`／`core` | 显式输入的规则计算、纯值和基础工具；不依赖会话、UI或隐式全局世界 |
| `world`／`resources`／`content` | 导航、原地图生成、MPQ读取与定义；内核仅使用导航及已准备纯值，不调用内容加载 |
| `persistence` | 原D2S编解码与文件保存工具，由hosting使用；TCP/IP本地租约及回传校验也在hosting，network／表现不读写角色存档 |

**服务端三层边界：** hosting把原包转为绑定玩家的类型化命令，并从MPQ准备规则；server校验资格、执行领域操作、提交事实；hosting把事实编码成原回包，并持有保存／传输边界。`GameInstance`和`GameSystems`只组装，`runtime/simulation`只安排执行顺序，不成为万能游戏会话。

**状态各有唯一主人：**

| 所有者 | 状态或协作职责 |
| --- | --- |
| PlayerStore | 角色持久值与玩家状态 |
| AreaStore／world | 分别持区域／碰撞／房间活动投影，以及各幕房间引用／纯值物化状态 |
| monsters／companions | 分别持活动及休眠非玩家实体，以及伙伴控制关系 |
| social | 队伍关系 |
| GameSystems.frameEvents／EventOutbox | 分别持玩法定时器与可靠输出；输出队列不是玩法时钟 |
| actions／skills | actions持施放记录、玩家恢复动作及玩家／怪物待释放队列；skills解释技能程序并推进释放 |
| missiles／effects／reactions | 分别持弹体、持续效果、物品触发队列 |
| combat／death | 分别负责伤害结算，以及死亡与普通奖励协作 |
| maps | PD2场景阶段及特殊死亡游标，不替代世界、人口或掉落领域 |
| transactions | 跨领域人物／物品变更的提交边界，不另持一套人物真值 |

索引、客户端副本和诊断都不是第二份权威。领域接口及原生帧事件入口见[内核子系统](../modules/SERVER_SYSTEMS.md)，迁移阶段与尚未闭合的休眠／全局顺序见[总计划2.3节](MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)，本页不维护逐程序迁移清单。

修改时先找状态所属领域，通过窄Ports协作；不把GameSystems、网络会话或内容加载器传给玩法。共享算法不共享运行态，协议值不等于磁盘值。混合出生通过EntityIds::Publication共享一次ID范围发布，AreaStore复验同一房间登记令牌；ActorPublication可携带带版本的退役集合，与新实体／人物事务共同验证。异步内容等待使用PendingContent，由isDeferred统一交给调度暂缓；准备队列的写入接口必须显式可变。新内容沿“MPQ准备 → 领域资格／执行 → 事务／事实 → 原包投影”接入，未实现路径明确拒绝。

快速定位：[领域目录与提交约定](../modules/SERVER_SYSTEMS.md)、[数据流](DATA_FLOW.md)、[原包入口](../modules/SERVER_PROTOCOL.md)、[存档边界](../modules/SAVES.md)。编码及头文件依赖见[编码规范](../development/CODING.md)，验证授权和方法见[测试指南](../development/TESTING.md)。下文保留详细目录和链接关系，不另维护第二份架构清单。

后续战斗重构先对照[服务端战斗复刻目标架构](SERVER_COMBAT_RECONSTRUCTION.md)：重点为精确数值、属性来源、动作帧事件、技能／飞弹程序和提交顺序，再修复既有内容并补怪物／职业缺口。该文中的建议类型、目录和调度契约尚未全部实施，不能与本页当前架构混淆。

## 模块分工

路径相对src/；客户端可组装嵌入宿主，独立控制台进程也可复用同一宿主与内核。两种组合均保持库与状态边界。

| 模块 | 职责 |
| --- | --- |
| core、resources、content | 跨平台值工具，MPQ／原图／表读取，准备只读定义 |
| world/native_map、generated_area、room_terrain | 原生布局及纯值房间输入准备；宿主按实际引用物化，客户端按原包重建 |
| network/byte_transport、tcp_stream、memory_transport | IByteTransport字节流；TCP与有锁有界内存FIFO；连接代次隔离旧字节 |
| network/realm_session、protocol | 唯一MCP／D2GS编码、解码、握手、worker、客户端稳定快照；原服另有账号认证 |
| client/remote_* | 两种服务端共用的世界／库存副本、地图、请求、预测与I*Client投影 |
| content/character/combat_display、content/skills/description_*、skill_description | 原包已观察装备／状态及MPQ准备的只读数值、描述程序；复用gameplay纯计算，不持有权威运行态，未知输入不默认成功 |
| presentation | 唯一RealmFrontend、SceneController、SceneView、RemoteScene及原资源／面板／动画／声音 |
| server/player_identity、game_commands、game_messages | 分别持内部身份、命令和快照；基础运行契约不包含完整快照，不是网络协议或客户端契约 |
| server/game_instance、area_store、player_store、movement | 组合根、区域／玩家唯一所有权、入场值组装与移动执行；无MPQ／GPU／文件／socket |
| server/runtime | 类型化命令、穷尽分派、系统组合／窄依赖、固定步顺序、只读规则和有界事件出口 |
| server/systems/* | 独立领域State／Ports／操作；已执行范围与保留Scaffold分开，详见[内核子系统](../modules/SERVER_SYSTEMS.md) |
| hosting/game_host | 多实例槽位、代次、绑定、25Hz固定步、暂停与内部快照 |
| hosting/game_content、character_creation、area_preparation | MPQ输入准备、初始角色／物品；区域蓝图工作线程独占MPQ句柄及缓存，按票据交回纯值；不负责客户端绘制或玩法提交 |
| hosting/embedded_realm | 内存端点、selector、分帧与断开／调度组装，不实现具体玩法 |
| server_main、hosting/pvpgn_server | 独立控制台组合根；D2CS房间／票据、D2DBS角色锁／保存、游戏监听与停服，不实现另一套玩法 |
| network/protocol/pvpgn | D2CS／D2DBS八字节头有界分帧，与客户端MCP／D2GS分帧分离 |
| hosting/detail | 与传输无关的多连接服务组合、MCP角色／游戏、D2GS生命周期和管理接口 |
| hosting/protocol | C2S／S2C目录、阶段／长度检查、按领域具名分派与显式stub；扩展约定见[服务端协议](../modules/SERVER_PROTOCOL.md) |
| hosting/native_game_wire、native_item_wire | 原入局／状态／移动／物品包编码；JM磁盘位流不作网络包 |
| hosting/administration、app/debug/server_commands | 类型化宿主管理与外围JSON适配；不增加私有游戏消息 |
| hosting/character_store、character_directory | 服务端目录名册、稳定选择、角色租约、替换检测、原子保存与删除；目录值不暴露给UI |
| gameplay/character/persistent_character | 纯角色持久值；不带路径、表现、网络或整局运行态 |
| persistence | 沿用master支持范围的D2S v96编码／校验；CharacterSaveData为上述值的别名 |
| app/frontend | 连接选择、Single Player自动建房、窗口／输入／宿主调度与开发管理；不读写D2S，不另设世界循环 |
| asset_tool | 独立MPQ／地图／存档只读诊断 |

## 依赖方向

```mermaid
flowchart TD
    app[d2x] --> presentation
    app --> remote_client
    app --> character_host
    dedicated["d2x_pvpgn / d2x_server.exe"] --> character_host
    dedicated --> d2gs_protocol
    dedicated --> persistence
    presentation --> remote_scene
    presentation --> client
    remote_scene --> remote_client
    remote_scene --> world
    remote_client --> network
    remote_client --> d2gs_protocol
    character_host --> host
    character_host --> game_content
    character_host --> network
    character_host --> d2gs_protocol
    character_host --> persistence
    host --> server
    server --> gameplay
    server --> navigation
    game_content --> world
    game_content --> server
    game_content --> persistence
    persistence --> content
    world --> content
    content --> resources
```

客户端和表现库不链接host、server或persistence；客户端EXE经嵌入宿主链接存储库，独立EXE经同一character_host连接权威内核。server／host不链接content、resources、raylib或network；独立目标不链接presentation／raylib，但项目配置仍包含其他客户端目标，尚无仅服务端依赖配置开关。C++20／CMake保留Windows／Linux路径，独立进程目前仅Windows构建，Linux未认证。目标、包目录及部署统一见[PvPGN服务端](../development/PVPGN_SERVER.md)。

## 生命周期

原服与自研均由FrameInput → SceneController → RemoteUiClients／RemoteControl／Combat／Inventory → RealmSession发送原包。回包进入同一RemoteWorld／RemoteTown／RemoteScene，再由公共UI、动画和声音消费。单机仅在组装入口建立内存字节连接；移除了LocalGame／GameClients／GameScene及其CMake目标。

单机嵌入宿主由应用线程泵入字节并调用GameHost.advance；TCP/IP Host启用独立宿主线程，按自身单调时钟收包、推进和输出，不由窗口帧驱动。EmbeddedRealm公开接口由同一调用线程使用，LAN期间经有界队列同步取得宿主操作结果／只读值，内存字节传输沿用有锁队列；性能观察在调用方pump／shutdown交付。宿主拥有独立MPQ句柄和可变缓存，保留同一挂载配置／语言／松散文件映射，不继承UI加载回调。关闭先保存再停止线程，切回单机先join，首个同步pump不重复累计后台时间。内核命令继续校验实例／区域代次和内部序号；独立GS多房间线程见下段及[线程方案](HOST_RUNTIME_RECONSTRUCTION.md)。

最小化时应用继续事件、协议、存档回传和地图CPU消费，跳过场景创建、纹理上传与绘制／交换缓冲。单机世界仍按暂停策略停步，退局待结算不受普通失焦暂停阻塞；LAN世界不因房主窗口状态暂停。恢复清理旧输入并重建场景表现。原版混合客户端及Linux上的线程／最小化行为仍待验证。

独立GS主线程处理TCP／D2CS及路由；固定gameThreads游戏线程分别独占NativeRealmHost、资源句柄、缓存和连接投影，按房间数分配新局且不迁移，同局保持串行25Hz。专用PvpgnStorage线程负责文件与DBS串行事务，游戏线程以externalCheckpoint提交并等待异步确认，不等待网络或磁盘。admitExternal接受已校验票据与角色值，不创建本地CharacterStore租约。连接ID不复用，保存完成另匹配请求序号；故障／关闭及容量限制见[部署页](../development/PVPGN_SERVER.md)。多房间实际吞吐、完整后端故障组合及Linux行为仍未认证。

嵌入宿主在原MCP选角时取得整局角色租约，创建游戏前完成内容和网络入局数据准备；失败不覆盖原档。正常离局先停止接收玩法请求，等待已接受操作结算并完成保存，再确认退出和回收绑定；保存失败不能伪造成功回执，须保留可恢复状态。具体等待条件、TCP/IP房主／客人差异、包序列及检查点／重载操作统一见[存档](../modules/SAVES.md#已迁移的角色与存档入口)，不能将本地租约保证套用于独立DBS后端。

地图、规则、存储和原包适配不得继续聚集到GameInstance。后续在既有server/systems骨架内逐领域实现，runtime/simulation只维护顺序，不承载玩法；旧GameSession及其全能执行器不恢复。参考仓库、MPQ、mvp、旧包和用户存档保留。
