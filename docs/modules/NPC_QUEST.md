# NPC与任务客户端接口

本页维护客户端端口和修改入口。任务规则／资格／事务／协议归[任务系统](../gameplay/quests/SYSTEM.md)，各幕执行与证据由其专题维护；对白／提示／菜单行为归[NPC交互](../gameplay/npc/INTERACTIONS.md)，经济／加工归[NPC交易](../gameplay/npc/TRADE.md)。

## 接口与修改入口

| 入口 | 职责 |
| --- | --- |
| [quest_projection.cpp](../../src/client/quest_projection.cpp) | QuestProjectionInput结合当前MPQ生成本人任务标题、说明、状态和原图槽 |
| [quest.hpp](../../src/contracts/quest.hpp)、[quest_client.hpp](../../src/client/quest_client.hpp) | 当前难度本人任务值视图；不暴露可写旗标或完整存档 |
| [npc.hpp](../../src/contracts/npc.hpp)、[npc_client.hpp](../../src/client/npc_client.hpp) | 对白／菜单／服务视图与窄语义意图 |
| [remote_ui_clients.cpp](../../src/client/remote_ui_clients.cpp) | 原任务、NPC提示、对白和货架适配公共端口 |
| [remote_control.cpp](../../src/client/remote_control.cpp) | 按原GUID靠近／交谈、确认／关闭与旅行请求 |
| [quest_controller.cpp](../../src/presentation/hud/quest_controller.cpp)、[npc_controller.cpp](../../src/presentation/npc/npc_controller.cpp) | 共用面板手势与生命周期 |
| [quest/catalog.hpp](../../src/gameplay/quest/catalog.hpp) | 内部ID、幕／面板位置、原任务号、图像槽及D2S槽映射 |

原服和自研宿主共用以上消费者，不构造LocalNpcClient／LocalQuestClient或本地任务执行器。客户端不根据NPC提示、公共任务字、地图种子或发送成功补造个人资格；未知值的显示与完成动画规则由任务系统页维护。

## 服务端协作入口

交谈租约、距离／视线与治疗归npc／effects，商品与报价归merchant，加工归crafting，任务资格归quests，区域／门户／跨幕旅行归world／travel。实际所有权与提交顺序见[内核系统](SERVER_SYSTEMS.md#任务与旅行)；本页不复制任务奖励或原D2S编码规则。

[npc::System::service](../../src/server/systems/npc/system.cpp)一次验证认证玩家、区域代次、NPC显隐／可交互、任务可见性、距离／视线及匹配交谈，返回只读NPC与Conversation借用。商店和任务加工在请求、内容准备、安装阶段分别调用并核对交谈revision；不得把借用指针存进异步准备或当永久权限。NPC仅提供交谈资格，维修／赌博／加工材料／任务奖励仍由所属领域复验。普通方块和镶嵌不要求NPC交谈，不与NPC加工混用。crafting的serviceNpc统一从类型化意图提取NPC身份。

客户端的原对象滚动消息、插杖面板及NPC旅行同样用于D2GS，修复依据与运行范围见[第二幕](../gameplay/quests/ACT2.md)。完整佣兵服务、多人任务与原服逐整数经济仍有缺口，分别见[佣兵](../gameplay/characters/HIRELINGS.md)、[任务系统](../gameplay/quests/SYSTEM.md)和[物品经济](../gameplay/items/ECONOMY.md)。
