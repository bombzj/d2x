# 剩余解耦工作

更新：2026-10-05。依据当前源码（含工作区改动）及[项目基线](../../BASELINE.md)。本页只维护剩余工作、入口和完成条件；模块分工见[架构](OVERVIEW.md)，接口与验证范围见[模块基线](../README.md#模块边界)。

会话 Impl、客户端基础投影、活角色组合、装备派生、公共单位能力、已实现技能的通用执行、区域缓存、任务／死亡奖励规则和 25 Hz 固定步已有入口，不再列为待迁移阶段。死灵法师与亚马逊各三页技能已有执行／被动入口；第一至第三幕佣兵也已接入技能执行，当前批次验证状态见[佣兵](../gameplay/characters/HIRELINGS.md)。

当前仍是单操控人物、单活区宿主。主要耦合留在表现层的完整会话查询、会话对库存／模拟器私有状态的写入，以及内容、任务和世界操作的回调组装。

## 下一步执行顺序

| 顺序 | 工作 | 完成条件 |
| --- | --- | --- |
| 1 | 场景投影、世界交互和表现资源入口 | 表现层只读所需视图并提交窄意图，移除对完整会话／库存／内容目录的查询 |
| 2 | 库存写入和跨服务事务 | NPC、任务、合成、掉落与恢复通过明确操作提交，收口 `InventoryService` 的外部私有写入 |
| 3 | 装备充能／触发来源 | 原规则核实后接入来源资格、实例版本、起手／释放复验和消费；复用现有技能执行 |
| 4 | 任务物件与宿主组装 | 规则接收事实并产出计划；宿主协调提交；模拟器不通过捕获整个会话的回调取得跨领域状态 |
| 5 | 单位与区域所有权 | 查询和操作显式绑定单位／区域，静态物件定义与活状态分开，保持每份权威状态唯一拥有 |

每批迁移一条完整调用链，并删除对应旧旁路。第 3 项依赖第 2 项的来源消费事务；第 4、5 项可按已具备端口的调用链推进。未实现技能、怪物 AI 和原版演出由各玩法专题维护，不混入解耦待办。联网优先规划[既有 D2GS 接入](MULTIPLAYER.md)，场景／资源边界随联网调用链迁移；本地库存事务、完整宿主和多玩家／多活区所有权改造不作为该路线前置。

## 1. 场景与表现边界

**缺口：** `SceneController`、`SceneView` 和 `SceneAssets` 仍借用 `GameSession`。攻击／施法、地面拾取、尸体回收、任务物品提交、目标 HUD、单位／物件／弹体显示、效果叠层、屋顶和光照仍有兼容查询。资源加载仍遍历完整内容／库存目录，`GameEvent` 仍混合内部事实与表现通知；`d2x_presentation` 仍私有链接会话。

**入口（相对 `src/`）：**

- `presentation/controller.cpp`、`scene_view.cpp`、`scene_assets.cpp`。
- `presentation/world/{world_renderer,projectile_view,object_hint}.cpp`、`loot_view.cpp`、`actors/*_assets.cpp`、`actors/state_overlay_view.cpp`、`hud/hud.cpp`、`npc/inventory_interactions.cpp`、`items/item_display_compat.cpp`。
- `contracts/`、`client/*_client.hpp`、`local_*_client.cpp`、`map_asset_source.hpp`、`gameplay/model/events.hpp`。

先迁移目标选择／攻击施法，再处理地面物品与物件交互、场景绘制及资源加载；每条链同时迁移命中、手势、意图、反馈与绘制。沿用现有本人角色／库存／NPC／地图投影；补充可见单位、物件、弹体、效果和目标提示值。表现资源由专门目录或只读资源源提供；客户端消息只携带允许显示的字段和必要的操作者／接收者信息。

**完成条件：** 对应链路不再包含或查询会话、模拟器、完整世界状态、库存服务或完整内容目录；保持原目标锁定、面板关闭与手势消费顺序。全部场景迁完后再移除表现目标对会话的依赖。当前地图资源借用无卸载，寿命约束见[地图基线](../modules/MAP.md)。

## 2. 库存写入与事务

**缺口：** `InventoryService` 仍将 `GameSessionImpl` 声明为 friend。NPC 出售／修理／鉴定、雇佣、任务奖励／物件、合成、掉落、尸体清理和恢复等入口直接改 `state_`、创建随机流或容器；部分失败路径靠会话备份库存再回滚。已有 `replaceItem` 和方块草稿可复用，但没有覆盖全部跨服务操作。

**入口：** `gameplay/items/inventory.hpp`、`operations.hpp`、`replacement.cpp`、`cube.cpp`、`corpse_inventory.cpp`；`gameplay/npc/{purchase,service,hireling_services}.cpp`；`gameplay/session/session_{cube,consumables,loot,quest_items,quest_rewards,act_two,later_act_objects,shrines,player_corpses,restore,tools}.cpp`。

按调用链补齐原子库存操作及限定恢复入口；钱包、任务进度和世界变更由应用服务协调。保留唯一物品位置、handle／revision、访问权限、装备需求与武器组规则。失败不留下半笔扣金／消耗或额外随机／ID 消费；明确各操作的提交与通知顺序。

**完成条件：** 对应宿主调用不再写库存私有字段，不自行复制库存实现事务；普通命令和恢复／调试的权限入口分开。全部旁路收口后移除 friend。已有死亡掉落的暗金占用／空位失败语义见[奖励基线](../modules/REWARDS.md)，若要改变须单独核实规则，不能随重构改动。

## 3. 装备技能来源

**缺口：** 现有装备授予与等级加成已经接入，来源携带物品 handle／revision；真实装备充能／触发及消费事务尚未实施。书本 `charges` 是卷轴数量，不能作为充能技能实现依据。佣兵天然技能／光环已有入口，装备触发和装备自带光环仍有消费者缺口。

**入口：** `gameplay/items/skill_sources.*`、`skills/{source,rank_sources,casting,release,reactions}.*`、`session/session_skill_sources.cpp`、`simulation/skill_world.cpp`、`npc/hireling_actions.cpp`；原表适配在 `content/items/` 与 `content/skills/`。

先查当前 MPQ 和本地参考，确定触发条件、来源资格、扣充能时点、失败／中断行为及保存映射。物品负责来源与消费，携带者负责施法；玩家、佣兵和其他已支持单位经显式适配接入现有 `SkillRuntime`，技能处理器不反查完整装备或角色。

**完成条件：** 起手、释放及后续求值能按操作者／来源实例复验；换装、物品删除、死亡、恢复和切区能正确失效或清理。规则未核实的来源继续暂缓。具体技能与装备效果的支持范围分别更新[技能](../modules/SKILL_RUNTIME.md)和[物品](../gameplay/items/SUPPORT.md)。

## 4. 任务物件与宿主

**缺口：** 五幕任务规则、NPC 接触、物品替换和死亡奖励已经拆出；复杂任务物件／演出仍由 `session_act_two.cpp`、`session_later_act_objects.cpp` 等协调。`session.cpp` 仍集中组装大量捕获 `this` 的模拟器回调，`Simulation` 保留 friend 和具体状态查询。

**入口：** `gameplay/quest/acts/`、`rewards/`；`gameplay/session/session.cpp`、`session_impl.hpp`、`session_{act_two,later_act_objects,quest_rewards,deaths,regions,tick,commands,restore}.cpp`；`gameplay/simulation/{simulation.hpp,skill_world.cpp,simulation_tick.cpp}`。

沿现有“准备事实 → 规则计划 → 权威提交”拆出剩余任务物件链。组装依赖由宿主显式提供，纯规则不创建其他服务，不读 MPQ；将捕获整份会话的查询回调换成必要的类型化输入或窄端口。保留现有技能／武器／世界端口，按实际消费者收窄 `session.hpp`、`simulation.hpp` 和 `model/state.hpp`。

**完成条件：** 对应规则不依赖完整会话／世界，模拟器只编排所需系统；恢复、创建／关闭世界和调试仍是独立宿主入口。保持 `session_tick.cpp`／`simulation_tick.cpp` 的现有先后顺序、25 Hz、`tick(0)`、死亡奖励顺序及随机消耗。跨服务失败提交与第 2 项统一收口。

## 5. 单位与区域所有权

**缺口：** `WorldState` 仍聚合一个 `player` 和一个活动 `area`；佣兵挂在玩家，随从在世界集合中。`AreaRepository` 已按移动语义保存休眠区，公共 `CombatUnit` 已与内部记录绑定分开；仍需收口按“当前玩家／当前区域”查询，以及 `WorldObject` 混合定义、外观、碰撞、任务计时与 NPC 活状态的职责。

**入口：** `gameplay/model/state.hpp`、`player/state.hpp`、`areas/{state,repository}.hpp`、`simulation/unit_records.*`、`monsters/`、`npc/hireling_control.cpp`；`world/{object,region,map,region_store}.*`、`gameplay/session/session_{regions,objects,exits}.cpp`。

为新操作显式传入操作者、单位 ID 和区域上下文；分离只读物件定义与运行态，继续复用公共移动／动作／资源能力。导航借用保持稳定所有者；单位增删、切区或恢复后按 ID 重新获取能力视图，不跨容器修改保留指针。中立单位身份、召唤主人、转换归属和死亡收益保持原规则。

**完成条件：** 每个单位、区域、物品和动态物件只有一个权威拥有者，休眠／激活／旅行有明确移交与失效边界；客户端观察范围与模拟激活分开。单机阶段保持单活区推进；多玩家注册、并行活区、共享资格和网络生命周期留待联机路线实施。

## 交付边界

遵守 [AGENTS.md](../../AGENTS.md)：默认只改源码／文档，不编写测试程序，不继续构建、运行检查或打包。每批更新对应已有基线，删除已完成待办；验证证据留所属模块，源码／包差异归[项目基线](../../BASELINE.md)。

保留 C++20／CMake 的 Windows／Linux 路径，按实际公开依赖拆头／目标，不复制权威状态或再建聚合大头。内容参数从 MPQ 导入；持久字段保持原 D2S v96，语义变化同步规则指纹和[存档文档](../modules/SAVES.md)，不静默迁移旧档。
