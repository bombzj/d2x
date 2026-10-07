# 自研服务端协议与管理入口

更新：2026-10-08。本文维护源码接口与扩展约定；功能实施顺序仍以[总计划](../architecture/MULTIPLAYER.md)为准。Windows Release已构建打包，原MCP选角／入局、D2GS走跑与代表性stub有有限冒烟；证据及覆盖限制见[基线](../../BASELINE.md#当前运行包与有限冒烟)，消息目录不代表全部处理已实现。

## 所有权

客户端只有既有RealmSession／Remote*链。TCP和内存连接均传原1.13c MCP／D2GS字节；GameHost、PersistentCharacter和内部投影不交给客户端。此处不实现BNCS账号服务；原服账号路径保留，独立账号宿主属于总计划第三阶段。

| 源码入口（相对src/hosting） | 职责 |
| --- | --- |
| embedded_realm | 组装内存端点，处理selector、拆包、连接代次、断开和调度；不执行具体玩法 |
| detail/native_realm_service | 单参与者角色租约、入场准备及服务组合；不依赖MemoryChannel或客户端视图，可由后续TCP监听驱动 |
| detail/realm_protocol、realm_characters、realm_games | MCP分派与具名处理器；账号／角色、游戏票据与领域执行分开 |
| detail/game_protocol | Connected → LoggedOn → Entered → Closed；验证票据、版本、阶段，处理原握手／心跳／保存退出 |
| protocol/message_catalog、client_messages.inc、server_messages.inc | 原包身份、长度、所属领域、阶段、实现状态；目录供拆包、分派、输出和诊断共同使用 |
| protocol/client_stream | 有界增量分帧；块可拆开或合并消息；已消费偏移在追加时整理，避免逐包搬移整个缓冲 |
| protocol/gameplay_dispatch、handlers_* | 移动、战斗、库存、交互、成长、社交具名入口；只得到宿主、已认证玩家绑定、坐标原点和序号 |
| native_game_wire、native_item_wire | 权威值投影成原S2C字节；JM磁盘物品不直接作网络物品 |
| administration、detail/host_administration | 类型化宿主管理命令及结果，校验实例代次和参与者；JSON／Win32管道均留在app/debug |

NativeRealmService是单玩家宿主的生命周期编排，不是玩法容器。新增战斗／库存／任务逻辑放入server对应领域；包处理器只解码、提交领域命令和处理提交结果。不能把MPQ读取、存档编码、UI状态或每种技能实现放进GameInstance或这个编排对象。

内核现在提供GameCommand、command_dispatch及26个新领域State／Ports骨架，详见[内核子系统](SERVER_SYSTEMS.md)。submitGameplay统一将连接身份和当前来源区域／代次绑定到内部命令；01／03移动使用此入口，3A／3B／41作为成长／复活接线例子，领域未实现时仍返回stub，不新增原包或客户端分支。

## 消息覆盖与状态

目录逐项来自当前RealmSession的实际发送代码，以及RealmSession、RemoteWorld、RemoteSocial的实际消费入口。不是将旧版本全部包长复制后默默接受未知包。

| 方向 | 当前基础 |
| --- | --- |
| D2GS C2S | 60种消息均登记长度、阶段和具名处理入口。01／03坐标走跑及68／69／6B／6D生命周期已接；其余为显式stub |
| D2GS S2C | 90种客户端已消费消息具有ServerMessage身份和输出目录。encodeServerPacket提供统一具名编码入口；已有编码接入，其余禁止生成空包假装实现 |
| MCP C2S | 9种请求均有入口；01／02／03／04／05／07／0A／19已接单人范围，06 GameInfo为stub |
| MCP S2C | 上述9种响应及14建房排队已登记；06、14尚无编码实现。05只返回私有单人宿主的空公共房间列表 |
| 子命令 | 4F的10种交易／仓库／金币／方块动作，38的3种旅行／商店／赌博服务，分别解码并进入具名stub；不把整个4F算成一个未来杂项系统 |

状态严格区分`implemented`、`admission-only`和`stub`。例如原9D物品、1F属性、28／29任务初始化已有编码，不表示库存事务、人物总属性或任务奖励已执行。S2C长度统一调用客户端现有lod113c_packet_size；不维护第二份回包长度表。3A属性分配按现有客户端的原1字节ID＋2字节打包参数拆帧。

具名stub返回NotImplemented，由宿主按包号统计，不修改角色或世界，不发伪造成功包。普通客户端继续原协议超时／无确认语义；开发者通过server-protocol查询明确状态，不把私有错误塞进原包。stub目前只保证注册、帧边界、连接阶段和分派，尚不承诺完整字段或玩法资格校验。未知包长拒绝连接；已识别但未知的4F／38子操作拒绝执行。

移动提交的Queued只表示进入权威FIFO；Applied／NoRoute等实际结果来自后续固定步，server-status.command给出最近实际结果。内部movementSequence单独触发原移动回复，不把其他领域的命令完成当成移动。即时拒绝移动会回当前权威位置；相同静止姿态不随每个tick重复广播。Scaffold领域在入队前返回NotImplemented。按包统计、最后一次分派结果、失败信息均有界保存，计数按当前宿主生命周期累计，诊断不输出握手票据或认证包。

## 新增一个玩法的顺序

1. 在对应handlers文件接管已有具名stub；解码完整字段并finish，核实原版本、范围、目标类型、所有权和服务上下文。原规则先核对当前MPQ／本地reference，不猜错误码和ACK。
2. 向server对应领域提交纯值命令，操作者取连接绑定。GUID、金币、报价、技能等级等客户端数据不视为权威；排队后再次核对失效的目标／区域／事务上下文。
3. 在权威固定步内校验并原子执行。失败不部分扣费、移物或改档；不能在网络回调直接写库存，也不能因接收重试重复结算。
4. 将结果和事件投影为已核实的原包，复用encodeServerPacket和共同长度校验，再更新目录状态。新增包在目录登记后，分派会引用它的具名函数，缺函数不会落入通用成功或默认stub。
5. 更新对应领域与保存文档。持久化语义变化须同步格式／规则指纹和明确拒绝边界；原包不能附加私有请求号、GameHandle或规则指纹。

不预先为每个未来技能创造空类、万能命令字典或通用fallback。结构上的消息覆盖与玩法完成度分别维护；不能通过将目录改为implemented来替代领域实现。

## 管理与恢复

named pipe → app/debug/server_commands → AdminRequest → 宿主调度线程。管理入口是应用显式持有的能力，不来自客户端游戏连接；连接原服时拒绝所有宿主管理命令。当前宿主与pipe回调同线程，因此直接执行类型化请求，不制造一个立即同步等待的假队列；将来迁移宿主线程时在此端口增加有界请求／结果队列，不能让管道线程直接访问GameHost。

save、load、cancel-load、step已接；金币／经验／物品／怪物／旅行／祭坛／佣兵／重置等保留类型化参数和明确NotImplemented入口。F11与pipe save调用同一保存接口；Ctrl+F11与load调用同一候选入场准备，再走原69退局、MCP选角、原D2GS重新入局。候选游戏保持暂停，重新取得的文件字节、角色名、难度必须匹配；不重新生成地图，也不悄悄换成别的角色。

保存失败保留当前实例和租约；重载准备失败保留旧实例；退出或取消释放候选实例。正常连接关闭保留已排队的B0，丢弃旧客户端输入；错误关闭丢弃部分入场输出。命令名、参数、返回状态和示例见[调试管道](../development/DEBUG_PIPE.md#嵌入宿主管理命令)。
