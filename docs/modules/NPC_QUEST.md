# NPC 与任务投影

NPC、任务进度、资格、服务和奖励由所连接的服务端拥有；客户端只投影本人已知状态及原对白。LocalNpcClient／LocalQuestClient 和本地任务协调／奖励执行器已删除。

自研宿主已接普通NPC服务与邪恶洞穴；其余任务仍待迁入。公共资格／报价／旗标解释与服务端交互／奖励事务的提取方案见[参考设计](../architecture/REFERENCE_DESIGN.md#9-npc怪物任务与物品怎样拆)。没有因共享公式而恢复客户端任务执行器。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [quest_projection.cpp](../../src/client/quest_projection.cpp) | QuestProjectionInput + 当前 MPQ → 唯一任务标题、说明、状态及原图槽 |
| [quest.hpp](../../src/contracts/quest.hpp)、[quest_client.hpp](../../src/client/quest_client.hpp) | 当前难度本人任务值视图，不传可写旗标／完整任务簿 |
| [npc.hpp](../../src/contracts/npc.hpp)、[npc_client.hpp](../../src/client/npc_client.hpp) | 对白／菜单／服务及窄语义意图 |
| [remote_ui_clients.cpp](../../src/client/remote_ui_clients.cpp) | 原任务、NPC提示、对白、货架适配公共端口 |
| [remote_control.cpp](../../src/client/remote_control.cpp) | 原 GUID 靠近／交谈、对白确认、关闭与跨幕旅行 |
| [quest_controller.cpp](../../src/presentation/hud/quest_controller.cpp)、[npc_controller.cpp](../../src/presentation/npc/npc_controller.cpp) | 共用任务／NPC面板手势与生命周期 |
| [quest/catalog.hpp](../../src/gameplay/quest/catalog.hpp) | 内部 ID、幕／日志位置、原任务号、图像槽及 D2S 槽映射值 |

## 原数据与 UI

每局 ProtocolReady 后一次0x40请求日志。0x28本人私有48项任务字与0x29本局公共任务字分开；0x52全量、0x5D增量和0x50原进度消费各自编号语义。27项日志从本人状态选 MPQ文字；缺洞窟剩余数显示 `?`，不从地图种子、公共字或旧执行器推断。

已完成条目仍能查看说明。首次未知→已知建立动画基线；之后已知状态转换触发完成动画／newquestlog提示，不重播首次收到的旧完成记录。NPC0x8A提示使用原 npcalert／高度，不能作为服务资格。

NPC消息提交0x31，仅原0x27消息的menu=0自动显示，多段按未确认集合推进，不重复确认；menu=2任务评论保留在Talk话题中，普通再次点击直接显示服务菜单。首次介绍和任务自动对白仍由原服消息及PlrIntro旗标决定，不增加本地“已问候”状态。TBL首行数字是 a1npc SPEED 元数据，对白去除该行；通用字符串查询保留原文。交谈关闭、单位移除、换幕和死亡清理 UI 与未发送意图，网络继续推进。

## 未完成范围

NPC服务／Talk菜单与发起／接收交易邀请共用[OriginalMenu](../../src/presentation/graphics/original_menu.hpp)的当前MPQ boxpieces、font16测宽／居中文字、Sky PL2金色标题和蓝色悬停、行命中区域。依据用户提供的Warriv、Akara及等待交易原版截图统一样式；NPC资格与命令仍由原服务副本及控制器处理。菜单锚点及完整状态未宣称逐像素认证；本批构建／运行证据见[联网交付](NETWORK.md#当前批交付)。

商店报价、赌博及普通经济已接，完整原服逐整数认证仍有限；雇佣／复活／装备、重置／灌注／打孔／署名／插杖等特殊服务尚未形成完整原协议闭环；没有生产者的旧插杖面板已删除。真墓符号、部分后续幕说明、五幕剧情／奖励／旅行资格及多人共享规则未认证。

既有NPC陈旧位置靠近修正已随后续Release入包，具体截图超时未复现，见[联网模块](NETWORK.md#本批源码修正)。原任务身份与核对入口见[任务系统](../gameplay/quests/SYSTEM.md)，NPC操作见[交互](../gameplay/npc/INTERACTIONS.md)，服务范围见[交易](../gameplay/npc/TRADE.md)。

当前自研普通商店、赌博、回购、批量及充能维修的执行／原包／限制统一见[库存模块](INVENTORY.md#已接入的执行路径)。任务灌注／打孔／署名仍须独立资格与领奖事务，不能把商店服务完成算作任务完成。
