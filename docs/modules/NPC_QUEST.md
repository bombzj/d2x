# NPC 与任务接口

NPC、任务进度、资格、服务和奖励由所连接的服务端拥有；原服与自研宿主共用客户端，客户端只投影本人已知状态及原对白。LocalNpcClient／LocalQuestClient与客户端本地任务执行器已删除；保留的纯规则由服务端调用。

任务领域、原消息编号和保存边界统一见[任务系统](../gameplay/quests/SYSTEM.md)，六项执行及运行证据见[第一幕](../gameplay/quests/ACT1.md)。共享资格／报价／旗标解释的依据见[参考设计](../architecture/REFERENCE_DESIGN.md#9-npc怪物任务与物品怎样拆)。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [quest_projection.cpp](../../src/client/quest_projection.cpp) | QuestProjectionInput与当前MPQ生成唯一任务标题、说明、状态及原图槽 |
| [quest.hpp](../../src/contracts/quest.hpp)、[quest_client.hpp](../../src/client/quest_client.hpp) | 当前难度本人任务值视图，不传可写旗标／完整任务簿 |
| [npc.hpp](../../src/contracts/npc.hpp)、[npc_client.hpp](../../src/client/npc_client.hpp) | 对白／菜单／服务及窄语义意图 |
| [remote_ui_clients.cpp](../../src/client/remote_ui_clients.cpp) | 原任务、NPC提示、对白、货架适配公共端口 |
| [remote_control.cpp](../../src/client/remote_control.cpp) | 原GUID靠近／交谈、对白确认、关闭与跨幕旅行 |
| [quest_controller.cpp](../../src/presentation/hud/quest_controller.cpp)、[npc_controller.cpp](../../src/presentation/npc/npc_controller.cpp) | 共用任务／NPC面板手势与生命周期 |
| [quest/catalog.hpp](../../src/gameplay/quest/catalog.hpp) | 内部ID、幕／日志位置、原任务号、图像槽及D2S槽映射 |

## 客户端生命周期

每局ProtocolReady后一次0x40请求日志；27项日志使用本人状态选择MPQ文字，缺洞穴剩余数显示`?`。已完成条目仍可查看说明。首次未知→已知建立动画基线，之后已知状态转换触发完成动画／newquestlog提示，不重播首次收到的旧完成记录。

交谈关闭、单位移除、换幕和死亡清理UI与未发送意图，网络继续推进。对白menu／确认、原菜单图形和NPC提示统一见[NPC交互](../gameplay/npc/INTERACTIONS.md)，服务请求与范围见[交易](../gameplay/npc/TRADE.md)。客户端不根据NPC提示、公共任务字或地图种子补造个人资格。

## 服务端交互边界

NPC交谈绑定人物身份、区域代次和交谈revision，持续复验距离／视线；关闭／重开同一NPC使旧准备失效。merchant把报价和货架绑定同一交谈，成交复验商品及人物／库存版本。治疗由effects准备补满、清毒／冻结与MPQ curable状态，随对白事务提交；新增治疗分支未单独认证。

已接受的原NPC旅行保留交谈身份，因为原客户端在0x38之后关闭对话；新交谈、移动、死亡和换区仍使旅行失效。旅行执行属于travel／world，任务只提交资格与幕完成记录，见[服务端系统](SERVER_SYSTEMS.md#第一幕任务与旅行)。

普通商店、赌博、回购、充能维修及任务加工的执行与限制见[库存模块](INVENTORY.md#已接入的执行路径)。第一幕授予灌注、免费重置、Cain服务及罗格奖励；其他幕任务生产者、完整雇佣／复活／装备和插杖等特殊服务仍待接入。没有生产者的旧插杖面板已删除。五幕完整剧情、原服逐整数经济与多人共享均未完整认证。
