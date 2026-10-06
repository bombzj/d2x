# 库存、装备与技能来源基线

2026-10-07 当前源码已删除其余 Local 客户端及旧本地表现／地图资源旁路，解除表现库对 GameSession 的链接；统一端口与保留范围见[客户端](CLIENT.md)。下方本地适配与验证记录仅是历史事实。最终依赖清理与本轮构建／包／有限冒烟统一见[联网模块](NETWORK.md#最终依赖清理)。

更新：2026-10-06。对应 [技术改造方案](../architecture/REFACTOR_PLAN.md) P1 库存 UI 切片及 P3 装备／来源基础切片；历史离线`InventoryService`曾负责权威事务，现已删除，联机权威为D2GS，完整P3尚未完成。第四项已随第五项通过Windows Release及代表性佣兵装备路径，完整规则回归尚未覆盖；联网物品与城镇服务已Release、打包及有限原服冒烟。

当前物品复核新增源码修正：`ItemDefinition.questTag/questCarryConflicts` 由内容层从原 quest 身份及 D2MOO 原生互斥形式准备；`checkCarryLimit` 共用于创建／拾取／移动／转移／交换／恢复，carry1 包括私人箱，任务检查还含 Cursor 及该角色尸体。carry1 元数据直接覆盖完整 enabled 暗金原行，不依赖 lvl≤99 的生成目录；带独立孔数的堆叠禁止合并。玩家药剂按显式职业代码应用纯玩法倍率，佣兵继续非玩家倍率。运行指纹增加 `inventory-carry-rules-v2-native-quest-pairs`、`potion-class-rules-v1-native-restoration`、`socket-rules-v1-native-children-runewords`；D2S 字段仍为 v96，非法重复／互斥任务物品明确拒绝，无静默删物品或迁移。镶嵌已完成 Windows Release 与有限实机冒烟，未打包；SocketItem、有序 socketedItems、需求、符文之语独立属性及原生 D2S 共用物品边界，孔相关配方只绑定当前原表，细项见 [物品支持](../gameplay/items/SUPPORT.md)、[数据](../gameplay/items/DATA.md)、[药剂](../gameplay/items/BELT_AND_CONSUMABLES.md)。

第八项将 `ItemGeneration`／`ItemAffixInstance` 移到 `items/generation.hpp`；`items/state.hpp` 继续复用同一值定义，掉落计划不再包含完整库存状态。库存仍唯一拥有实际物品与位置，未改物品生成／保存字段；该批已随收尾构建／有限冒烟，见[奖励基线](REWARDS.md)。

## 联网物品消费

2026-10-06源码接入[RemoteInventory](../../src/client/remote_inventory.hpp)与[原服值契约](../../src/contracts/online_items.hpp)。0x9C／0x9D及数量／耐久增量驱动物品副本，MPQ提供布局／位流元数据，command提交原拾取、背包／腰带／装备、使用／堆叠／装书／鉴定／镶嵌和武器组请求。联机权威为D2GS，未调用本节离线InventoryService，不写本地D2S或重算角色装备效果。

另接服务器确认的箱子／方块格、存取／丢金币、合成、普通货架买卖、维修及凯恩鉴定，共27种command。此前通过Windows Release与城镇有限原服冒烟及新进程持久保存回归；现已入包，本轮追加原服掉落拾取／药水使用和拾取物品保存重入，其他组合未重复认证；损坏装备维修、凯恩资格与全部消费组合未实测。库存／货架复用单机面板、原图和物品提示格式；原服精确价格、孔内完整显示、赌博／多买及玩家交易／佣兵装备仍待接。原协议与结果见[联网模块](NETWORK.md#原服物品与请求)，参数见[command](../development/DEBUG_PIPE.md#联网物品操作)。

## 任务物品替换（第七项）

`InventoryService::replaceItem` 在私有库存／ID 草稿中复验来源 handle、单件数量和访问，再消耗、创建并准备有序通知；成功一次移交库存、创建随机流和 ID 游标，失败不改原件。调用方可传已解析物品生成结果及准备好的随机流，库存不反查角色／任务；当前接入卷轴翻译与恰西灌注。成功仍按原移除→创建顺序发布，发布发生在完整事务提交之后；草稿仅用于低频替换，不用于每帧装备计算或通用多领域事务。

本轮 Windows Release 及原树皮卷轴拾取／翻译／重复交谈简单冒烟通过，实际灌注及失败注入未验收，见[NPC／任务基线](NPC_QUEST.md#npc任务与奖励协调第七项)。不改变 D2S 或成功随机／ID 消费顺序，不是全库存／钱包／合成事务重写。

## 装备与技能来源（第四项）

| 入口 | 当前职责 |
| --- | --- |
| [`handle.hpp`](../../src/gameplay/items/handle.hpp)、[`errors.hpp`](../../src/gameplay/items/errors.hpp) | 独立物品 ID／revision 与库存错误值；原值和错误顺序不变，来源头不需要完整物品实例／事务头 |
| [`equipment_loadout.*`](../../src/gameplay/items/equipment_loadout.hpp) | 短期只读借用：各槽位、背包物品及其定义，提供需求／有效装备查询；不持有库存或角色状态 |
| [`equipment_inventory.*`](../../src/gameplay/items/equipment_inventory.cpp) | 从库存和显式容器建立借用，绑定需求属性解析；唯一包含库存服务的装备读取适配 |
| [`equipment_requirements.*`](../../src/gameplay/items/equipment_requirements.cpp) | 校验鉴定、类型、职业、属性和等级；属性解析保持在前置拒绝之后；事务仍先核验 ItemHandle 版本 |
| [`equipment_contributions.*`](../../src/gameplay/items/equipment_contributions.cpp)、[`equipment_combat.*`](../../src/gameplay/items/equipment_combat.cpp) | 护符、有效装备、需求闭包、套装去重、角色属性及战斗贡献；只接收类型化数据和属性解析函数，不读取 MPQ／内容目录 |
| [`equipment_stats.*`](../../src/gameplay/items/equipment_stats.cpp) | 从借用装备及显式使用者／加成计算防御、格挡、武器伤害和外观；不包含 InventoryService |
| [`content/items/equipment_modifiers.*`](../../src/content/items/equipment_modifiers.cpp)、[`equipment_set.hpp`](../../src/gameplay/items/equipment_set.hpp) | 内容加载后一次准备套装件数／门槛／指令索引；运行时按规则请求解析物品／套装属性。旧 content/items/equipment_combat 实现已迁到玩法并删除 |
| [`items/skill_sources.*`](../../src/gameplay/items/skill_sources.cpp) | 按显式装备使用者筛选当前武器组的既有授予技能，返回物品 handle、技能 ID 和等级；不查询玩家或怪物 |
| [`skills/rank_sources.*`](../../src/gameplay/skills/rank_sources.cpp) | 合成调用方提供的学习等级、合格授予和各类加成，复用原等级算术；不读取角色、怪物、库存或内容目录 |
| [`session_character_stats.cpp`](../../src/gameplay/session/session_character_stats.cpp)、[`session_skill_sources.cpp`](../../src/gameplay/session/session_skill_sources.cpp) | 宿主准备角色属性／等级上下文，连接上述规则与原技能来源服务；库存事务与施法执行不再各自实现装备贡献／等级规则 |

权威状态仍只有 `InventoryState` 一份。`EquipmentLoadout` 的物品／定义指针和解析函数只在一次同步权威调用内使用；库存提交、恢复、容器替换后重新建立，不缓存或传给客户端。装备预览借用原规划草稿；读档、角色刷新与佣兵派生均复用同一规则。派生装备值仍为可重算结果，不是第二份物品状态。模拟器、会话显示值及角色提示只包含 `combat/weapon_values.hpp`，不再借装备派生入口包含库存／装备规则头。

保留原背包护符、当前武器组、损坏／数量／需求排除、槽位和闭包迭代顺序、套装件数与去重、替换佣兵旧装备排除，以及被动／Warmth／资源刷新时机。未新增属性、掉落概率或随机消费；D2S v96、字段映射、原生物品掷值、保存语义与规则指纹不变。

已支持的来源是原 `grantedSkill` 授予与已有技能词缀加成。`ItemSkillGrant` 携带当前 handle／revision 作为来源事实，不是施法授权或充能消费事务；书本 `charges` 仍是原卷轴数量语义。真实充能／触发技能、消费时机、来源复验、多所有者上下文和会话 `friend` 的方块／NPC／恢复写入收口继续待实施，不能称完整 P3 完成。

第四项已随第五项通过 Windows Release。简单冒烟通过真实 UI 购买／出售及佣兵头盔装备、卸到 Cursor、放回背包，防御 51→56→51；使用原派生规则和库存事务，见[NPC／任务基线](NPC_QUEST.md#商店与佣兵-ui第五项)。未重做套装闭包、全部需求／技能来源、全职业和保存往返；下方仍为此前库存 UI 批次证据。未新增测试程序、打包或提交 Git。

## 职责与入口

当前源码新增封闭 `ContainerKind::Corpse`：12个装备位加一个死亡鼠标物品位，物品位置仍是唯一权威归属。`corpse_inventory.cpp` 独立完成死亡脱装草稿和回收，复用装备资格／组合与普通收集规则；普通 UI 移动、消费和转移拒绝访问，`InventoryView` 不投影尸体内部物品。尸体不产生当前装备／护符贡献，也不计入非任务 carry1；任务物品重复／互斥拾取检查会计入该角色尸体。原存档校验显式传入允许的尸体容器集合；死亡批次已通过 Windows Release 构建及初始法杖尸体回收冒烟，复杂库存组合及当前限带修正未验收，完整规则见 [玩家死亡](../gameplay/characters/PLAYER_DEATH.md)。

| 入口 | 职责 |
| --- | --- |
| [`items/intents.hpp`](../../src/gameplay/items/intents.hpp) | 独立库存意图集合；移动、交换、合并、拆分、装备、使用、金币等沿用原命令值类型 |
| [`contracts/inventory.hpp`](../../src/contracts/inventory.hpp) | `InventoryView` 持有本人容器、物品显示值、布局、钱包及版本；不保存 `PlayerState` 或完整物品实例，不借用服务指针 |
| [`IInventoryClient`](../../src/client/inventory_client.hpp) | 读取投影、预览意图、提交意图及查询腰带空位；接口不接受任意 `GameCommand` |
| [`client/inventory_view.cpp`](../../src/client/inventory_view.cpp) | 客户端值查询、占格命中与装备位置查询；无会话或世界状态依赖 |
| [`LocalInventoryClient`](../../src/client/local_inventory_client.cpp) | 绑定当前本地角色，映射本人容器及当前获准储物容器；预览／提交转回原会话校验和事务 |
| [`content/items/item_display.cpp`](../../src/content/items/item_display.cpp) | 将原物品与显式角色显示上下文转换为名称／提示行；无会话、GPU 或活角色引用 |
| [`presentation/inventory/`](../../src/presentation/inventory) | 包裹、腰带、仓库、方块的显示、命中和手势；消费投影并通过客户端接口提交 |
| [`presentation/items/item_display_compat.cpp`](../../src/presentation/items/item_display_compat.cpp) | 地面／世界旧物品入口复用同一名称和提示计算，暂保留会话上下文适配；商店已改用客户端单项提示 |
| [`presentation/npc/inventory_interactions.cpp`](../../src/presentation/npc/inventory_interactions.cpp) | 任务物品世界命中仍为兼容入口；灌注／结束 NPC 会话改经 `INpcClient`，与库存手势实现分开 |

`d2x_client_api → core`，`d2x_client → client_api`；客户端查询实现不链接会话。`d2x_local_client → client`，私有依赖 `session`；应用负责组装。`presentation` 改为链接 `client`，但其他面板及 NPC／地面交互仍需 `session`。

库存契约复用 `items/operations.hpp`、`state.hpp` 和装备枚举；这些基础值头还可在后续 P3 中进一步拆小。Ninja 实际编译依赖确认 `client/inventory_view.cpp` 与 `presentation/inventory/inventory_panel.cpp` 不包含会话公共／私有头、`simulation.hpp`、`model/state.hpp` 或 `classic_data.hpp`。总场景头仍有资源／事件等其他共享类型，尚未实现全部 UI 的编译隔离。根层未编入 CMake 的旧 `presentation/inventory_controller.cpp` 已删除，实际入口只有库存子目录版本。

## 数据与操作约束

- 权威物品仍只有 `InventoryState` 一份；客户端投影是值副本，手势不修改它。只投影本人库存及当前储物上下文，不遍历其他玩家库存或地面物品。
- 保留原 ID／revision、唯一位置、预览拒绝及最终提交时的版本／权限／容量复核。`InventoryApplied`／`InventoryRejected` 仍由原事件链反馈；没有另写移动、交易或配方规则。
- 提示计算显式接收等级、力量、敏捷、最大耐久及相关任务提示值；库存 UI 不读取原属性、原存档标志、随机流或 `PlayerState`。图像键只用于现有资源缓存，不是已定稿的网络字段。
- MPQ 布局、物品属性、Books 配对和原配方继续由现有内容层读取；D2S 继续使用 v96；镶嵌语义及运行规则指纹见 [存档](SAVES.md)。

## 当前限制

本节离线预览／读取为同步本地接口；远端另由RemoteInventory和RealmSession维护版本、请求编号／回执、代次及断线清理，尚未绑定库存UI。本地适配按权威版本缓存，相同渲染帧不重新生成库存，仍未测量大库存性能。版本／寿命见[角色基线](CHARACTER.md)。第五项商店报价／出售走INpcClient，佣兵面板与手势读库存／佣兵投影，装备提交InventoryIntent。地面拾取和任务物品世界命中仍有兼容调用，不能称为完整库存领域分离。

NPC 购买／出售／修理等已有命令转发 `INpcClient` 后继续沿用原库存事务；复杂货架和报价查询仍未迁移，入口及一次购买的实际覆盖见 [NPC／任务基线](NPC_QUEST.md)。下方库存批次原有验证范围保持。

已有 `ui-input` 调试入口支持单帧或最多 32 个连续帧，以及按住／松开、Shift／Ctrl 和数字输入；批次全部校验后入队，每个真实应用帧处理一次，仍经过现有控制器。此入口仅用于本机诊断，不是游戏网络协议。

## 构建与冒烟

Windows Release 游戏与资源工具构建通过，修正一处重复定义并清理重复包含。通过既有调试管道注入真实控制器输入，普通女巫／第一幕城镇／整局 seed 210 的观察如下。

| 操作 | 观察结果 |
| --- | --- |
| 包裹拖放与交换 | 回城卷轴 `(0,0) → (2,0)`，revision `1 → 2`；再与鉴定卷轴交换，双方位置和版本正确 |
| 无效目标／取消 | 拖到包裹面板内网格外再右键取消，原位置、版本和物品数不变 |
| 装备／武器组 | 初始法杖右键卸入包裹再穿回原右手槽；武器组 `0 → 1 → 0`，空组伤害显示 `1–2`，恢复原组为 `1–5` |
| 腰带／使用 | Shift 将药水移入包裹再放回腰带；右键饮用消耗一瓶，其他实例保留 |
| 仓库／方块 | 两侧 Shift 双向转移成功，仍为同一 ID；方块由原调试入口提供、正式拾取，再由库存右键打开 |
| 金币 | UI 存入／取出 1234；数字输入再存入 500，钱包 734／仓库 500 |
| 地面／Cursor | 库存拖到世界丢弃；真实标签点击拾取进 Cursor，D2S 保存／恢复仍为 Cursor，独立加载后可放回包裹 |
| 最终保存恢复 | 七件物品：法杖装备、三瓶腰带药水、方块在包裹、回城卷轴在方块、鉴定卷轴在仓库；位置、数量及钱包／仓库金额恢复正确 |

原法杖名称、耐久、伤害及附加技能提示，仓库、方块和 Cursor 截图已查看。最终构建后的独立两帧加载实例退出 0、stderr 空。该冒烟未覆盖商店交易、所有堆叠／拆分／鉴定组合、方块配方、多人或 Linux 运行。

证据：`artifacts/refactor-inventory-build.log`、`artifacts/refactor-inventory-20261003/` 内 JSON、依赖记录、临时 D2S、日志及截图，均不纳入源码。未新增测试脚本、用例或专用程序，未读写用户角色档，未更新旧运行包。

方块事务与 Crafted：typed 原配方匹配完整材料，输出和随机状态在库存草稿成功后一次提交；useitem 保留宿主身份，usetype／新物品另分配身份。Crafted 固定属性和词缀只掷一次并保存原生统计值；运行指纹为 `cube-rules-v2-native-crafted-and-txt-indices`，磁盘仍原 v96，规则与有限验证见 [方块](../gameplay/items/CUBE_AND_GOLD.md)。
