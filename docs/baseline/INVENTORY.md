# 库存客户端边界基线

更新：2026-10-03。对应 [技术改造方案](../TECHNICAL_REFACTOR_PLAN.md) P1 的库存 UI 切片；原 `InventoryService` 继续负责权威事务，P3 多所有者领域改造尚未完成。

## 职责与入口

| 入口 | 职责 |
| --- | --- |
| [`items/intents.hpp`](../../src/gameplay/items/intents.hpp) | 独立库存意图集合；移动、交换、合并、拆分、装备、使用、金币等沿用原命令值类型 |
| [`contracts/inventory.hpp`](../../src/contracts/inventory.hpp) | `InventoryView` 持有本人容器、物品显示值、布局、钱包及版本；不保存 `PlayerState` 或完整物品实例，不借用服务指针 |
| [`IInventoryClient`](../../src/client/inventory_client.hpp) | 读取投影、预览意图、提交意图及查询腰带空位；接口不接受任意 `GameCommand` |
| [`client/inventory_view.cpp`](../../src/client/inventory_view.cpp) | 客户端值查询、占格命中与装备位置查询；无会话或世界状态依赖 |
| [`LocalInventoryClient`](../../src/client/local_inventory_client.cpp) | 绑定当前本地角色，映射本人容器及当前获准储物容器；预览／提交转回原会话校验和事务 |
| [`content/items/item_display.cpp`](../../src/content/items/item_display.cpp) | 将原物品与显式角色显示上下文转换为名称／提示行；无会话、GPU 或活角色引用 |
| [`presentation/inventory/`](../../src/presentation/inventory/) | 包裹、腰带、仓库、方块的显示、命中和手势；消费投影并通过客户端接口提交 |
| [`presentation/items/item_display_compat.cpp`](../../src/presentation/items/item_display_compat.cpp) | 地面／NPC 等旧物品入口复用同一名称和提示计算，暂保留会话上下文适配 |
| [`presentation/npc/inventory_interactions.cpp`](../../src/presentation/npc/inventory_interactions.cpp) | 任务物品世界命中仍为兼容入口；灌注／结束 NPC 会话改经 `INpcClient`，与库存手势实现分开 |

`d2x_client_api → core`，`d2x_client → client_api`；客户端查询实现不链接会话。`d2x_local_client → client`，私有依赖 `session`；应用负责组装。`presentation` 改为链接 `client`，但其他面板及 NPC／地面交互仍需 `session`。

库存契约复用 `items/operations.hpp`、`state.hpp` 和装备枚举；这些基础值头还可在后续 P3 中进一步拆小。Ninja 实际编译依赖确认 `client/inventory_view.cpp` 与 `presentation/inventory/inventory_panel.cpp` 不包含会话公共／私有头、`simulation.hpp`、`model/state.hpp` 或 `classic_data.hpp`。总场景头仍有资源／事件等其他共享类型，尚未实现全部 UI 的编译隔离。根层未编入 CMake 的旧 `presentation/inventory_controller.cpp` 已删除，实际入口只有库存子目录版本。

## 数据与操作约束

- 权威物品仍只有 `InventoryState` 一份；客户端投影是值副本，手势不修改它。只投影本人库存及当前储物上下文，不遍历其他玩家库存或地面物品。
- 保留原 ID／revision、唯一位置、预览拒绝及最终提交时的版本／权限／容量复核。`InventoryApplied`／`InventoryRejected` 仍由原事件链反馈；没有另写移动、交易或配方规则。
- 提示计算显式接收等级、力量、敏捷、最大耐久及相关任务提示值；库存 UI 不读取原属性、原存档标志、随机流或 `PlayerState`。图像键只用于现有资源缓存，不是已定稿的网络字段。
- MPQ 布局、物品属性、Books 配对和原配方继续由现有内容层读取；D2S v96、保存语义和规则指纹不变。

## 当前限制

预览和读取是同步本地接口；远端的缓存、异步操作回执、请求编号及断线处理尚未实现。本地适配按权威更新版本缓存，场景仅在版本变化时复制投影；相同渲染帧重复读取不重新生成库存，仍未测量大库存性能。版本／寿命约束见 [角色基线](CHARACTER.md)。NPC 商店报价／出售、地面拾取、佣兵面板及任务提交保留兼容调用，不能把本切片称为完整库存领域／客户端服务端分离。

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
