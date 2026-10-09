# 库存与物品模块入口

更新：2026-10-09。本页只维护代码入口、状态归属及领域协作。规则、分类和证据统一见[物品目录](../gameplay/items/README.md)。原服／自研宿主共用原1.13c物品包、RealmSession／RemoteInventory／RemoteUiClients及公共UI。

## 内容与领域

| 入口 | 职责 |
| --- | --- |
| [item_content.cpp](../../src/hosting/item_content.cpp)与content/items | 当前MPQ基底、属性、品质、词缀、实际生成与不可变装备规则 |
| [loot_content.cpp](../../src/hosting/loot_content.cpp) | 捕获来源与TC／品质，准备冻结产物 |
| [merchant_content.cpp](../../src/hosting/merchant_content.cpp) | NPC货架、赌博与实际报价准备 |
| [crafting_content.cpp](../../src/hosting/crafting_content.cpp) | 镶嵌／CubeMain／任务加工，绑定材料与请求结果 |
| [inventory/system.cpp](../../src/server/systems/inventory/system.cpp)及planning／placement／equipment／quantities／storage／consumption／identification／ground／weapon_cost／replenishment | 库存规划、来源／权限／位置复验、装备／数量／消耗及请求状态 |
| [items/system.cpp](../../src/server/systems/items/system.cpp) | 地面库存、落点／身份、寿命／落地代次和GUID恢复时钟 |
| [loot/system.cpp](../../src/server/systems/loot/system.cpp) | 来源去重、有界准备、唯一物品记录与掉落安装协调 |
| [merchant/system.cpp](../../src/server/systems/merchant/system.cpp) | 实例NPC货架／个人赌博、请求／交谈版本、成交与周期 |
| [crafting/system.cpp](../../src/server/systems/crafting/system.cpp) | 材料／输出准备与加工复验，提交调用事务 |
| [attributes](../../src/server/systems/attributes)／[transactions](../../src/server/systems/transactions) | 总值／装备资格，人物／库存／世界成组提交 |
| [gameplay/items](../../src/gameplay/items)／[gameplay/loot](../../src/gameplay/loot) | 纯模型与规则计算，不持全局可写库存 |
| [native_item_wire.cpp](../../src/hosting/native_item_wire.cpp)／hosting/protocol/inventory_requests | 原位流输出与C2S绑定，运行版本不加入私有协议 |
| [persistence/d2s_items.cpp](../../src/persistence/d2s_items.cpp)及d2s_inventory／d2s_stats | 原v96编码与准入校验，不属于客户端 |

PlayerStore唯一持有人物持久库存及存档中的佣兵物品；items持不属于角色的地面值，merchant持货架。伙伴装备由hosting准备规则，活动实体归monsters，归属／控制关系归companions，不另建一份人物库存。attributes读取提交后的值，transactions统一提交；hosting只准备规则与实际掷值。技能拥有释放／扣费计划，effects持装备事件与恢复效果；任务和travel拥有奖励／门户资格。GameInstance不恢复万能库存／商店会话。

## 客户端入口

| 入口 | 职责 |
| --- | --- |
| [remote_inventory.cpp](../../src/client/remote_inventory.cpp) | 原物品／货架／容器只读副本，组合请求与回包等待 |
| [remote_ui_clients.cpp](../../src/client/remote_ui_clients.cpp) | 视图投影与公共UI意图适配 |
| [realm_session.cpp](../../src/network/realm_session.cpp) | 同一原请求发送，连接／游戏生命周期 |
| [content/items/item_display.cpp](../../src/content/items/item_display.cpp)及item_appearance／item_descriptions／item_pricing | 已知原值的图形、说明、原字段报价 |
| [presentation/inventory](../../src/presentation/inventory)／SoundCatalog／SceneAudio | 原图面板、手势与音效，不修改权威库存 |

物品GUID／revision、交互与区域代次、地面generation属于不同层的条件；具体语义见[MODEL](../gameplay/items/MODEL.md)和[COMMON](../gameplay/items/COMMON.md)。原包成功条件、图形和拒绝边界见[PRESENTATION](../gameplay/items/PRESENTATION.md)，参数仅维护在[调试管道](../development/DEBUG_PIPE.md#联网物品操作)。

## 修改归属

修改规则更新所属物品专题；充能／触发更新[物品技能](../gameplay/skills/ITEM_SKILLS.md)，任务阶段／奖励更新任务页，原D2S与准入更新[SAVES](SAVES.md)。固定步、命令与可靠事件结构归[SERVER_SYSTEMS](SERVER_SYSTEMS.md)，不复制第二份当前范围或包日志。

未知规则／位流、缺实际掷值或未准备来源显式拒绝／暂缓；定义、stub、生成、原包显示和实际执行分开登记。有限运行与失败样本统一见[EVIDENCE](../gameplay/items/EVIDENCE.md)，不以旧包认证当前新增源码。
