# 数据流与生命周期

更新：2026-10-10。本文维护客户端、嵌入宿主与独立PvPGN游戏服务沿原协议交换状态的数据流。当前包与未交付差异见[基线](../../BASELINE.md)，实施顺序见[总计划](MULTIPLAYER.md)，库边界见[架构](OVERVIEW.md)，独立服务的有限互通证据与限制见[部署页](../development/PVPGN_SERVER.md#有限冒烟)。

| 数据 | 流向 | 所有权与限制 |
| --- | --- | --- |
| 连接 | app → RealmSession → TcpStream或MemoryTransport | 内存跨线程只传原字节；账号认证或预认证Realm在连接入口选择 |
| 只读内容 | Archives → sharedClassicData → 客户端／NativeRealmHost | 同一挂载集合仅解析一次，单机与LAN Host在同进程共享；LAN Join不初始化未使用的本地宿主。独立进程分别加载自身所需MPQ，世界状态、地图副本、存档及GPU对象独立；解压缓存边界见[MPQ](../resources/MPQ.md#查询与提取) |
| 角色列表／建删选角 | RealmFrontend ↔ RealmSession原MCP ↔ 原Realm或EmbeddedRealm → CharacterStore | UI只有OnlineCharacter；服务端映射名称到目录身份及版本，客户端不能传路径 |
| 初始角色 | MPQ CharStats／Items → character_creation → D2S | 真实职业属性、初始物品与原来源技能，不自造参数 |
| 入局 | 原MCP建房／票据 → 原GS握手 → nativeGameAdmission | 准备碰撞和完整可编码状态后才接受；旧连接及不匹配票据拒绝 |
| 独立服务入局 | D2CS建房／票据 → D2DBS读取并锁定角色 → admitExternal → 原GS握手 | 账号／选角仍属PvPGN；核对姓名、职业、模式、进度、charinfo身份，不接受客户端上传D2S |
| 地图 | LOADACT／0x07 → RemoteTown → NativeMapGenerator | 与宿主generateArea使用同一生成器和房间顺序；两侧不共享可变Map |
| 移动 | SceneController → RemoteControl → 原01／03 → 原协议适配 → GameHost → GameInstance FIFO | 服务端25Hz寻路／碰撞；内部序号和绑定由宿主产生 |
| 内核命令 | 原包具名处理器 → GameCommand → command_dispatch → 领域System／Ports | 来源区域及代次在入队和执行时核对；新增领域Scaffold返回NotImplemented |
| 子系统固定步 | GameInstance → runtime/simulation → 各领域step | 同一25Hz，记录明确的未实现状态；已有移动、技能／战斗、物品／成长、效果／死亡、物件／任务／旅行等执行，具体顺序见[内核子系统](../modules/SERVER_SYSTEMS.md) |
| 领域输出 | 事务／投影 → EventOutbox → GameHost.pendingEvents → hosting原包编码 | 查询不消费、成功接入可靠发送后确认；只编码已支持领域的原包，未接领域仍为stub；不直接发送C++事件 |
| 状态 | 内核内部投影 → 原0D／0F等 → RemoteWorld → RemoteScene／SceneView | 客户端预测和显示不回写权威；无自研快照旁路 |
| 物品 | PersistentCharacter → 原9D位流 → RemoteInventory → InventoryView | 网络位流与D2S JM记录不同；客户端解码、面板和手势共用原服链 |
| 技能／任务 | 原属性／技能／任务字 + MPQ → 公共人物／任务投影 | 保存值保留；已支持技能、洞穴奖励和库存事务由独立领域执行，其余范围见对应专题 |
| 存档 | 服务端租约 → 导出PersistentCharacter → persistence校验 → 原子替换／.bak | 全部写入由宿主发起；存档不含整局AI、路径、弹体等运行态 |
| 独立服务存档 | 领域导出 → 原D2S编码／恢复副本 → D2DBS charsave与charinfo确认 | 正常角色库在DBS，本地仅恢复副本；两文件无后端原子事务，不自动回写恢复文件 |
| 退局 | 客户端原69 → 宿主保存成功 → 原B0 → MCP重新列角 | 失败保留实例及租约，客户端不会得到成功确认；可修复后重试 |
| 单机暂停 | app窗口／菜单策略 → GameHost.pause | 清路径和未执行移动，恢复不补暂停时间；不暂停原服 |
| 开发保存／重载 | F11／Ctrl+F11或pipe save／load → AdminRequest → 宿主 → 原协议重新入局 | 同一类型化管理接口；重载准备并保留候选实例，校验重新选角的版本后复用 |
| 开发单步／诊断 | pipe step／server-status／server-protocol／server-systems → 宿主管理 | step仅推进已暂停的权威实例；系统诊断显示目录／阶段／stub，不用于客户端世界同步，不给原包追加字段 |
| 探索／偏好 | 公共AutomapExploration与client-settings.json | 探索当前为本局，偏好独立保存；不把客户端偏好当D2S |

内存队列有锁和容量限制；MCP／D2GS消费不能假定一次send就是一个完整包。重连清旧字节、递增代次；内核玩家绑定从服务端票据取得。GameHost内部快照只供协议适配读取，可靠服务响应不能由覆盖式邮箱取代。

嵌入宿主当前由窗口帧调度，固定步单次最多补8步；未来共享房间需独立于窗口的宿主线程。客户端始终使用同一个网络worker、地图副本、输入控制、动画、声音和绘制循环。

独立服务端由server_main控制台循环驱动同一NativeRealmHost／GameHost，不创建窗口。停止请求先禁止新入局，等待可保存状态，再保存、排队解锁及离局通知；主动退局确认只在保存成功后发送。D2CS控制、DBS超时和恢复限制见[PvPGN服务端](../development/PVPGN_SERVER.md)，不沿用本地文件租约的故障恢复承诺。
