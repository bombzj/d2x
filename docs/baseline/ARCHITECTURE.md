# 模块与接口

| 目录／目标 | 所有权与职责 | 主要入口 |
| --- | --- | --- |
| `core` | ID、字节、坐标、指纹 | `id.hpp`、`math.hpp` |
| `resources` | 原文件解码；不解释玩法 | `Archives`、`decodeDs1/Dt1/Dcc/Dc6/Cof` |
| `content` | 原表 → 类型化只读定义 | `ClassicData`、`WorldCatalog`、`MonsterCatalog` |
| `world/navigation.*`、`room_activation.cpp` | 寻路、只读房间空间索引 | `Grid`、`RoomLayout` |
| `world` | 地图计划、拼接、图层、共享 DT1、出口 | `planWorld`、`MapRecipe`、`Map`、`Region` |
| `world/population.*` | 内容和地图 → 生成指令；不分配 ID | `planPopulation`、`PopulationPlan` |
| `gameplay` | 25 Hz 战斗状态和规则；不依赖 MPQ／raylib | `Simulation`、`WorldState`、`GameCommand` |
| `gameplay/items` | 物品／容器唯一状态及事务 | `InventoryService` |
| `gameplay/session` | 区域生命周期、出口、交互、死亡与存档协调 | `GameSession` |
| `persistence` | 值快照、编码、版本、文件替换和备份 | `SessionSnapshot`、`encodeSave/decodeSave` |
| `presentation` | GPU、音效、只读绘制、屏幕命中 | `SceneAssets`、`SceneView`、`SceneController` |
| `app` | 参数、输入、窗口、固定步调度 | `application.cpp`、`options.cpp` |

调用链：`输入 → Controller → GameCommand → GameSession → Simulation/Inventory → 状态与事件 → View`。

依赖约束：

- `d2x_gameplay → d2x_navigation`；玩法不链接 StormLib 或 raylib。
- `d2x_world → content/navigation`；生成器只产出资源配方，不创建 GPU 对象。
- `d2x_population → content/gameplay`；怪物计划与实体创建分离。
- `GameSession` 持有区域、模拟、物品服务和掉落状态。`Simulation` 借用稳定区域网格与房间索引。
- 当前区与非当前区状态只能存在一份；UI 不直接修改角色、物品或怪物。
- 图形按区域共享缓存；跨野外边界绘制相邻区域，碰撞与区域状态仍由会话切换。

扩展入口：

- `gameplay/model`：共享命令、事件、状态及规则类型；不执行会话协调。
- `gameplay/simulation`：固定步调度与模拟生命周期；`combat`、`player`、`monsters`、`consumables`、`loot` 分别持有对应实现。
- `gameplay/skills`：`casting.cpp` 统一施放门禁、扣费、冷却和事件；注册表绑定校验与效果，六种即时效果各有独立实现，数值在本目录 `definitions.cpp`。持续效果仍由角色／战斗更新执行；当前固定技能 ID 与状态结构不是完整原版技能系统。
- `world/maze`：`room_graph.hpp` 持有房间连接与特殊房放置，`generation.cpp` 编排增长、主题替换及配方输出，`cave.cpp`／`crypt.cpp` 提供已实现家族规则，`resources.cpp` 负责资源枚举与收集。`world/maze.hpp` 保留公共入口。家族配置不消费随机数；不得随意调整生成各阶段或变体选择的随机数调用顺序。

- 新地图家族：新增 `world` 生成器，返回 `MapRecipe`，接 `region_catalog`；不修改 DS1 解码器来硬塞布局。
- 新怪物：`gameplay/monsters/monster_spawn.*` 注册实际实现，再接 AI 和 `SceneAssets`；死亡保留原始身份。
- 新物品效果：从 `content` 导入定义，在玩法／事务服务执行；HUD 仅展示结果。
- 新 UI：布局、绘制、命中分开；技能图标经 `Skills → SkillDesc → DC6`。
- 修改持久状态：同步 `state`、`save_codec`、`session_snapshot` 与规则指纹。
