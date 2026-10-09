# 原协议消息速查

更新：2026-10-10。本文唯一维护消息编码、参数、分帧与预留目录；连接与副本见[NETWORK](NETWORK.md)，服务端分派与扩展见[SERVER_PROTOCOL](SERVER_PROTOCOL.md)，运行包见[基线](../../BASELINE.md)。这是源码契约，不是逐包运行认证。

## 编码约定

- 固定 LoD 1.13c；Single Player、LAN与原服客户端使用相同原包，不增加私有ACK、revision、请求号或宿主类型分支。
- ID为十六进制，长度为十进制且包含ID。字段表不重复ID；偏移从ID=0起。u8/u16/u32均小端；有符号值保留补码，不能直接序列化C++结构体或依赖平台对齐。
- `xy`表示u16 x,y；`unit`表示u8 type,u32 guid，仅用于明确标为unit的S2C。C2S目标通常为u32 type,guid。GUID须由连接绑定与领域复验，不是持久句柄。
- `zstr(n)`为最多n字节后接NUL；`char[n]`为固定数组。聊天当前只开放可打印ASCII／language0，其他编码暂缓。
- 位流低位先行。物品、状态、NPC组件位宽取当前MPQ ItemStatCost／MonStats2等表；不把参考数值写死或把D2S的JM数据当网络物品。
- 依据为当前1.13c D2Net长度表、当前编解码器、本地D2MOO D2PacketDef／D2Net Server／PlrMsg／SCmd；新增C2S预留布局与本地diablo2-protocol的1.13定义交叉核对。版本与许可见[来源](../resources/THIRD_PARTY.md)。D2MOO旧66–6D登录编号、3A疑点及2B自定义包不能直接移入1.13c。

## 唯一来源

| 入口 | 维护内容 |
| --- | --- |
| [message_schema.hpp](../../src/network/protocol/message_schema.hpp) | 两方向身份、分帧种类、长度偏移／最低包长、编译期完整性约束 |
| [C2S目录](../../src/network/protocol/client_messages.inc) | 73个已核对身份和长度，双方共同分帧／发送校验 |
| [S2C目录](../../src/network/protocol/server_messages.inc) | 142个身份，含仅保留ID的Reserved项 |
| [原长度表](../../src/network/protocol/lod113c_lengths.inc) | 唯一1.13c S2C长度表，0未定义、-1变长 |
| [分帧实现](../../src/network/protocol/lod113c.cpp) | 两方向有界拆帧，不知道长度则拒绝，不尝试扫描下一个ID |
| [宿主目录](../../src/hosting/protocol/message_catalog.cpp) | 阶段、领域、实现状态；不决定客户端是否消费 |

编译期检查ID唯一、S2C完整对应长度表、宿主名称／长度一致、C2S登记完整及未知分帧不得标为已实现。每个C2S链接具名处理器，分派统一检查无剩余字段。目录不是玩法完成清单。

`server-protocol`给出profile、ID、名称、领域、支持状态和计数；C2S另有phase／framing，S2C另有bytes／framing／frameSupported，有长度字段时给lengthOffset／minimumBytes。bytes=0表示非固定长度，不是空包。unknownGameRequests只列到达服务分派层的未知请求；流分帧提前拒绝的错误由连接失败诊断记录。使用见[DEBUG_PIPE](../development/DEBUG_PIPE.md)，不输出原始认证载荷。

## 状态

| 状态 | 含义 |
| --- | --- |
| implemented | 宿主已接具名处理或输出，不保证所有技能／参数／目标都支持 |
| partial | 父包有已实现和未实现子操作，须查子目录；当前C2S 4F |
| admission-only | 仅入场投影，不代表持续同步 |
| stub | C2S只读完整字段后NotImplemented，无世界写入；S2C禁止编码输出，不产生空ACK |
| ReservedXX | 只确认S2C身份／长度，参数语义未核实；宿主Reserved／stub |
| framing=unsupported | 身份已预留但无法分帧，拒绝连接，不是可以跳过的stub |

68仅Connected；69／6B／6D为LoggedOn或Entered，其中6B另拒绝重复入场；其余C2S仅Entered。Closed拒绝全部。操作者始终来自认证连接，包内玩家GUID不能替代连接身份。领域在固定步再验区域、目标、库存、资源。

## 传输封装

| 通道 | 格式 | 边界 |
| --- | --- | --- |
| SID | FF,u8 id,u16 total,body | total含4字节头；账号服务不由自研宿主实现 |
| MCP | u16 total,u8 id,body | total含3字节头；TCP selector在连接层，不是MCP消息 |
| D2GS C2S | u8 id,body | 无通用长度头，固定表或15聊天程序拆半包／粘包 |
| D2GS S2C协商 | AF,u8 mode | mode0原始逻辑包，mode1 Huffman，其他拒绝 |
| D2GS S2C压缩 | 一／两字节总长头＋Huffman数据 | 首字节<F0为一字节总长，否则((first & 0F)<<8)\|second；含头，可含多逻辑包，逻辑包可跨压缩块 |

输入各有256 KiB上限，单次Huffman输出64 KiB。客户端统一sent出口在TCP／内存发送前校验C2S长度；宿主sendGame及事件输出沿validateServerPacket校验身份、支持状态和准确长度。队列失败不记发送成功，非幂等请求不自动重发。

## C2S 清单

未写stub的项沿宿主现有处理器执行，玩法限制归对应专题。除生命周期外均要求Entered。

| ID | 名称／用途 | 字节 | ID后参数顺序 |
| --- | --- | --- | --- |
| 01／03 | WalkPoint／RunPoint | 5 | xy |
| 02／04 | WalkUnit／RunUnit | 9 | u32 type,guid |
| 05／08 | 左手坐标／持续 | 5 | xy |
| 06／07／09／0A | 左手单位／原地／持续／持续原地 | 9 | u32 type,guid |
| 0C／0F | 右手坐标／持续 | 5 | xy |
| 0D／0E／10／11 | 右手单位／原地／持续／持续原地 | 9 | u32 type,guid |
| 12 | StopSkill | 1 | 无；不是通用取消ACK |
| 13 | InteractUnit | 9 | u32 type,guid |
| 14／15 | OverheadChat／Chat | 变长 | u8 type,language; zstr(255) text; zstr(15) receiver; u8 extraLength; bytes[extraLength] |
| 16 | PickUpItem | 13 | u32 type,item,cursor |
| 17／19／24 | DropItem／TakeItem／TakeBeltItem | 5 | u32 item |
| 18 | PlaceItem | 17 | u32 item,x,y,page；page0背包／3方块／4仓库 |
| 1A／1B／1D／1E | 装备／间接装备／交换／双手 | 9 | u32 item,bodySlot |
| 1C | UnequipItem | 3 | u16 bodySlot |
| 1F | SwapItem | 17 | u32 cursorItem,targetItem,x,y |
| 20 | UseItem | 13 | u32 item,x,y |
| 21／28／29 | StackItem／SocketItem／LoadBook | 9 | u32 source,target |
| 23 | PlaceBeltItem | 9 | u32 item,cell；cell0–15 |
| 25 | SwapBeltItem | 9 | u32 cursorItem,targetItem |
| 26 | UseBeltItem | 13 | u32 item,useOnMerc,reserved |
| 27 | IdentifyItem | 9 | u32 targetItem,sourceItem |
| 2A | ItemToCube，stub | 9 | u32 item,cube |
| 2F／30 | InitializeNpc／CloseNpc | 9 | u32 type,npc |
| 31 | NpcMessage | 9 | u32 npcOrObject; u16 stringId,reserved |
| 32 | BuyItem | 17 | u32 npc,item; u16 transaction,mode; u32 quote；transaction2赌博，mode高位批量 |
| 33 | SellItem | 17 | u32 npc,item; u16 mode,reserved; u32 quote |
| 34 | IdentifyAll | 5 | u32 npc |
| 35 | RepairItems | 17 | u32 npc,item; u16 reserved,reserved; u32 all；全修item/all均FFFFFFFF |
| 36 | HireMercenary，第一幕已接 | 9 | u32 npc,name；name为原TBL姓名索引 |
| 37 | IdentifyGamble，stub | 5 | u32 item |
| 38 | NpcService | 13 | u32 action,npc,parameter；parameter按NPC解释为物品／目的地／0 |
| 3A | SpendAttribute | 3 | u16 packed；低8位属性0力量1精力2敏捷3体力，高8位count-1 |
| 3B | LearnSkill | 3 | u16 skill |
| 3C | SelectSkill | 9 | u32 selected,owner；selected低16位skill、bit31左手，其他位0；普通owner=FFFFFFFF |
| 40／41 | RequestQuests／Resurrect | 1 | 无 |
| 44 | StaffUpdate | 17 | u32 player,object,item; u16 state,reserved；state2取消／3提交 |
| 49 | Waypoint | 9 | u32 waypoint,destination；0关闭 |
| 4F | UiAction，partial | 7 | u16 action,high,low；金额=high<<16\|low，不是直接u32小端读取 |
| 50 | DropGold | 9 | u32 player,amount |
| 51 | BindHotkey | 9 | u32 packed,owner；skill低15位、bit15左手、slot高16位 |
| 58 | QuestCompleted，stub | 3 | u16 quest |
| 59 | MoveNpc，stub | 17 | u32 type,guid,x,y |
| 5D | PlayerRelation，stub | 7 | u8 relation,toggle; u32 player |
| 5E | PartyAction，stub | 6 | u8 action; u32 player |
| 5F | UpdatePosition，stub | 5 | xy，不能信任为权威坐标 |
| 60 | SwitchWeapons | 1 | 无 |
| 61 | MercenaryItem，partial | 3 | u16 bodySlot；空光标取装备，有光标按真实物品执行装备或支持的喂药 |
| 62 | ResurrectMercenary，第一幕已接 | 5 | u32 npc |
| 63 | InventoryToBelt，stub | 5 | u32 item |
| 68 | Logon | 37 | u32 hash; u16 token; u8 class; u32 version,sigA,sigB; u8 locale; char[16] name |
| 69 | SaveAndLeave | 1 | 无，保存成功才发B0 |
| 6B | EnterEnvironment | 1 | 无 |
| 6D | Ping | 13 | u32 elapsed,latency,wardenResponse |

68 version=13，签名DWORD依次ED5DCC50／91A519B6；认证票据和登录原始载荷不进入日志。14头顶／15广播和私聊使用type1、language0、extraLength0；广播receiver空，最大261字节，具名私聊最大276字节。分帧识别双字符串／扩展，暂不授权其他文本编码或非零扩展；私聊接收2／发送6及找不到／屏蔽5A结果，头顶5与76清除，见[NETWORK](NETWORK.md)。

4F子命令：2取消交易、3接受、4同意、7重置、8报价金币均stub；18关闭仓库、19取金币、20存金币、23关闭方块、24合成已接。38子命令：0旅行／奖励加工、1商店、2赌博已接；未知子操作拒绝，不从父包推断全部NPC服务支持。

未列C2S继续按未知格式拒绝；旧14聊天／22分堆／2B自定义／旧存档上传等不因d2moo存在符号就自动接受。未来先核实1.13c身份、长度和参数，再登记具名stub或正式处理器。

## S2C 分帧

固定包长由原长度表唯一提供。以下长度字段均为逻辑包总长，不是仅body；minimum仅为分帧最低值，消费者另验字段。

| ID | 规则 | minimum |
| --- | --- | --- |
| 16 | u16@1 | 13 |
| 26 | offset10起zstr(15)发送者、zstr(255)消息 | 12 |
| 3E | u8@1 | 2 |
| 5B | u16@1 | 34 |
| 94 | 6+3*u8@1 | 6 |
| 9C／9D | u8@2 | 3 |
| A6 | u16@2 | 4 |
| A8 | u8@6 | 8 |
| AA | u8@6 | 7 |
| AC | u8@12 | 13 |
| AF | 固定协商2字节，mode仅0／1 | 2 |
| AE／B3 | 已登记但分帧不支持，拒绝连接 | 未核实 |

## S2C 常用参数

下表列主要投影和消费入口；宿主准确输出状态由server-protocol提供。合法但未消费的消息按ID计unconsumed，不伪造副本。复杂位流不在此复制第二套MPQ定义。

| ID | 用途／字节 | ID后格式或参数入口 |
| --- | --- | --- |
| 01 | GameFlags／8 | u8 difficulty; u16 flags,reserved; u8 expansion,ladder |
| 02／04／05／06／B0 | 登录准备／加载完成／卸幕／退出／退局确认，各1 | 无 |
| 03 | LoadAct／12 | u8 act; u32 seed; u16 townArea; u32 secondarySeed |
| 07／08 | 房间进入／离开，各6 | u16 tileX,tileY; u8 level |
| 09 | AssignWarp／11 | unit; u8 warp; xy |
| 0A／0B | 移除单位／本人身份，各6 | unit |
| 0C | Hit／9 | unit; u8 flags,hitClass,lifeAndTrigger |
| 0D | UnitIdle／13 | unit; u8 action; xy; u8 hitClass,life |
| 0E | UnitMode／12 | unit; u8 changes,flags; u32 mode |
| 0F／10 | 坐标／单位移动，各16 | unit后动作／目标／当前坐标，见[世界消费者](../../src/client/remote_world.cpp) |
| 11／15 | Overlay／Reposition，8／11 | 11为unit,u16 overlay；15为unit,xy,u8 flag |
| 16／18／95／96 | 位置／资源位流，变长／15／13／9 | [世界消费者](../../src/client/remote_world.cpp) |
| 19／1A／1B／1C | 金币／经验，2／2／3／5 | u8／u8／u16／u32；1A/1B增量，1C绝对经验 |
| 1D／1E／1F／20 | 属性，3／4／6／10 | 前三种u8 stat后u8/u16/u32 value；20含单位字段，见世界消费者 |
| 21 | SkillRank／12 | u8 type,flags; u32 guid; u16 skill; u8 base,bonus,reserved |
| 22／23 | 物品技能次数／选技，12／13 | 23为unit,u8 left,u16 skill,u32 owner；22见[事件编码](../../src/hosting/detail/game_events.cpp) |
| 26 | Chat／变长 | 10字节固定头后发送者和文本双NUL，见[社交消费者](../../src/client/remote_social.cpp) |
| 27 | NpcMessages／40 | unit; u8 count,reserved; 8组(u8 menu,reserved,u16 stringId) |
| 28／29 | 私有／公共任务，103／97 | 28为u8 type,u32 player,u8 reserved后96字节；29直接96字节 |
| 2A／2C | 商店结果／音效，15／8 | 2A见[物品编码](../../src/hosting/native_item_wire.cpp)；2C为unit,u16 sound |
| 3E／3F／42 | 物品属性／光标／技能，变长／8／6 | 3F为u8 targetMode,u32 source,u16 skill；其余见世界消费者 |
| 4C／99 | 单位施法／替代单位施法，各16 | unit; u16 skill; u8 rank; unit target; u16 reserved |
| 4D／9A | 坐标施法／替代坐标施法，各17 | unit; u32 skill; u8 rank; xy; u16 reserved |
| 50／52／5D | 任务载荷／状态／更新，15／42／6 | 50按任务号解释，5D原链编号，见[任务编码](../../src/hosting/native_quest_wire.cpp) |
| 51／53 | 物件／环境，14／10 | 51为unit,u16 class,xy,u8 state,mode；53见任务编码 |
| 58 | NpcServiceResult／7 | u32 npc; u8 result,reserved |
| 59 | AssignPlayer／26 | u32 player; u8 class; char[16] name; xy |
| 5B／5C | 名册／离局，变长／5 | 5B前缀u16 total,u32 player,u8 class,char[16] name,u16 level；尾部见[名册编码](../../src/hosting/native_game_wire.cpp)；5C为u32 player |
| 60／63／82 | 门状态／传送点／主人，7／21／29 | 63为u32 waypoint,u16 marker=0102,112位历史；其余见事件编码 |
| 67／68／69／6A／6B／6C／6D | NPC移动／动作，16／21／12／12／16／16／10 | u32 npc前缀；[战斗编码](../../src/hosting/native_combat_wire.cpp)保留不同动作格式，6D为u32 npc,xy,u8 life |
| 73 | Missile／32 | 身份、位置、方向、主人、pierce见事件编码；仅MPQ ClientSend |
| 74／75／77／78／79 | 尸体／队伍／UI／交易方／金币，10／13／2／21／6 | 世界／社交消费者；不代表宿主交易已实现 |
| 7A／7B | 宠物归属／热键，13／8 | 7A原owner/pet布局见事件编码；7B为u8 slot,u16 packedSkill,u32 owner |
| 7F／8B／8C／8D／90 | 盟友位置／关系／队伍／生命，10／6／11／7／13 | 社交消费者，未核实含义保留原值 |
| 8A／8E／8F | 任务提示／尸体归属／Pong，6／10／33 | 世界消费者，8F由会话维护心跳 |
| 94／97 | BaseSkills／WeaponSet，变长／1 | 94为u8 count,u32 player,count组(u16 skill,u8 rank)；97无body |
| 9C／9D | 世界／所属物品，变长 | action、总长、原位流，9D含owner，见物品编码和当前MPQ |
| A3 | SkillEvent／24 | 原瞬时技能事件，见事件编码 |
| A7／A9 | 开／关状态，各7 | unit; u8 state |
| A8／AA | 状态属性／快照，变长 | unit,u8 total；A8再u8 state；剩余位流按MPQ |
| AC | AssignNpc／变长 | u32 npc; u16 class; xy; u8 life,total；组件／rank／词缀／owner等位流 |
| AF | Compression／2 | u8 mode |

## S2C 身份预留

以下用ReservedXX名称且宿主禁止输出，不编造未核实参数。固定长度只说明能分帧，不说明客户端消费。A6有分帧；AE具名Warden与B3分帧仍拒绝。

| ID | 总字节 |
| --- | --- |
| 00／4F／6E／6F／70／71／72 | 1 |
| 61／89 | 2 |
| 54／A4 | 3 |
| 5F／7E | 5 |
| 76／7C／92 | 6 |
| 4E／62／65／66／98／9B／9E／A1／AB | 7 |
| 93／9F／A2／A5 | 8 |
| A0 | 10 |
| 47／48 | 11 |
| 40／45 | 13 |
| 13／57 | 14 |
| 14／7D | 18 |
| 81 | 20 |
| 12／91 | 26 |
| 5E | 38 |
| 5A | 40 |
| B2 | 53 |
| 24／25 | 90 |
| A6／AE／B3 | 变长；AE／B3分帧拒绝 |

## MCP 与 SID

## 第一幕佣兵扩展

C2S36雇佣、62复活已接第一幕；61由stub转partial，含罗格装备及支持药水，38/action3为名单请求。S2C4E固定7字节(u16 name,u32 seed)，4F固定1字节清空名单；81固定20字节(u8 petType,u16 class,u32 owner,unit,seed,name)，9B固定7字节(u16 name,u32 cost)，name=FFFF清除死亡报价。9E/9F/A0为u8 stat,u32 unit后u8/u16/u32绝对值；A1/A2相同前缀后u8/u16增量，未知基线不凭空累加。上述身份不再Reserved，宿主81／9B／A0已输出，9E／9F／A1／A2仍仅消费预留。原清单中的相应stub／Reserved数量是重构时快照，以当前代码目录和本节为准。功能、限制与依据见[佣兵](../gameplay/characters/HIRELINGS.md)。

MCP请求：01启动、02建角、03建房、04加入、05列表、06详情、07选角、0A删角、19角色列表；对应响应已接，14排队由客户端消费、自研输出stub。字段由[客户端会话](../../src/network/realm_session.cpp)、[realm_protocol](../../src/hosting/detail/realm_protocol.cpp)、[realm_characters](../../src/hosting/detail/realm_characters.cpp)及[realm_games](../../src/hosting/detail/realm_games.cpp)维护；不能用D2GS长度表拆MCP。

SID当前流程为50／51、3D、3A、40、3E、0A、0C、0F及25心跳。自研宿主不实现BNCS；新增D2GS预留不代表账号协议完成。密码、key、票据和原始认证载荷不输出到消息诊断，功能边界见[NETWORK](NETWORK.md)。