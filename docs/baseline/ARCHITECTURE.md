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

- Travel 为开发目录／管道自由传送；WaypointTravel 为游戏传送，GameSession 校验两端激活与源点距离。SceneView.waypointSource 区分专用菜单与 F2 目录，首次交互发 WaypointActivated 而非直接开菜单。WorldState.waypoints 保存激活时间，表现层从 Objects.FrameDelta 和原 nu/on/op 计算动画阶段。
- 回城卷轴 UseItem 在 session_consumables 中规划端点、消耗库存并替换 TownPortalState；UseTownPortal 走近后切区，营地返回关闭。蓝门仅作为状态派生图像，不混入静态 Region.objects；快照校验端点和原营地标记。原野外传送点固定 LvlSub 片段由 outdoor 和 map_assembly 负责。

- `SceneAssets::loadHeroEquipment` 按原手部物品组件与动作类别构造角色动画并缓存，空组件用 nil 明确跳过；穿脱与读档重建表现，GPU 状态不入存档。`Graphics::composite` 将 COF 顺序方向映射到 DCC 方向，缺角色武器图不替换为默认斧。怪物实际位移及面向缓存在 SceneView，不改变战斗状态编码。
- `region.cpp` 在 DS1 对象后补原规则要求的 Navi 中立单位，位置来自血腥荒地边界预设中心；新增对象改变实体分配，因此同步规则指纹。`GameSession` 通过类型化回调向 Simulation 提供真实怪物基础行走速度，玩法仍不读取 MPQ。
- `app/debug_pipe.*` 为 Windows 本机当前用户 ACL 的非阻塞传输，`debug_commands.*` 在主线程解析 JSON、只读查询或提交 GameCommand；不把 Win32 带入玩法。`DebugKill` 复用正常死亡链，命令入口限制活着的可见激活目标；默认不开启。脚本 `scripts/Send-D2XCommand.ps1` 为通用调用入口，详见 [调试协议](../DEBUG_PIPE.md)。
- 金币地面实例仍由 InventoryService 拥有，GameSession 在正常拾取距离／通路检查后 consume 并增加 PlayerState.gold，余额按原实例保留；UI 只读显示钱包。普通容器拒绝金币，快照同时校验钱包和地面归属。
- `content/monster_loot.*` 将真实怪物身份及原表解析为死亡 TC 入口、等级与暂缓原因，供 `session_loot.cpp` 和 `d2x_assets <MPQ> loot-entry <monster> <rank> <difficulty> <level-ID> [superunique-ID]` 共用。会话在已结算 ID 检查后解析并输出诊断，仍由 LootSystem 记录死亡，不直接创建普通物品替代缺失品质。
- `gameplay/loot/treasure.cpp` 执行类型化 TC 的单人递归选择，不依赖 MPQ 或 GPU；`content/lod_data.cpp` 导入原表并生成自动类别。`d2x_assets <MPQ> treasure <TC-name> [seed] [monster-level]` 仅查询选择路径。`gameplay/loot/quality.*` 执行品质请求，`content/item_quality.*` 适配原 ItemRatio 并在 TC 叶子调用品质规则、规划支持的消耗品批次；会话提交随机状态与死亡 ID，再调用库存创建支持的普通实例。LootDeferred 只通知暂缓原因，不持久化为待补发请求。
- 现有资源工具 `quality <code> <item-level> <MF> [seed] [unique set rare magic modifiers]` 输出品质请求与各次分母／掷骰；`loot-plan <TC> <item-level> <seed> [upgrade-level]` 调用游戏共用规划器，显示候选／暂缓原因，不创建物品或会话。二者不代替实际击杀拾取验收。
- `gameplay/model`：共享命令、事件、状态及规则类型；不执行会话协调。
- `content/equipment_data.*` 将原 ItemTypes／物品字段转换为只读装备规则；`gameplay/items/equipment_rules.*` 定义部位和类型查询，`equipment.cpp` 规划基础穿脱与左右手冲突。InventoryService 保持实例唯一归属；GameSession 提供可信角色需求，不接受 UI 指定属性。
- `EquipItem` 提交实例版本及目标部位，空部位表示卸下，可指定背包／私人箱／地面目的地；穿戴时不能同时指定目的地。通用 Move／Transfer 不允许直接修改装备容器。腰带仍走 EquipBelt 原子缩容，二者共用需求校验；会话额外验证地面目的地碰撞与通路。表现层按原面板坐标显示部位，按真实库存加载原图，动态角色外观尚未接入。
- `gameplay/items/equipment_stats.*` 从普通装备实例派生基础武器伤害、防御、格挡及角色等级；`durability.cpp` 负责非堆叠装备损耗和版本事件。GameSession 在成功库存操作后刷新 Simulation 的只读派生缓存，并以回调协调战斗损耗；Simulation 不读取 MPQ 或设备输入。普通怪物准确率由会话提供类型化值，缺核实数据则不执行新增命中分支。
- `d2x_assets <MPQ> save-info <d2xsave>` 只读显示装备部位、耐久、防御和随机状态；只解码，不代替 GameSession 的完整存档校验。
- `gameplay/simulation`：固定步调度与模拟生命周期；`combat`、`player`、`monsters`、`consumables`、`loot` 分别持有对应实现。
- `gameplay/skills`：`casting.cpp` 统一施放门禁、扣费、冷却和事件；注册表绑定校验与效果，六种即时效果各有独立实现，数值在本目录 `definitions.cpp`。持续效果仍由角色／战斗更新执行；当前固定技能 ID 与状态结构不是完整原版技能系统。
- `world/maze`：`room_graph.hpp` 持有房间连接与特殊房放置，`generation.cpp` 编排增长、主题替换及配方输出，`cave.cpp`／`crypt.cpp` 提供家族配置；`barracks.cpp` 管兵营入口、楼梯／铁匠房及外侧回廊坐标连接，`resources.cpp` 按家族检查并枚举原资源。`world/maze.hpp` 保留公共入口。静态家族配置不消费随机数；不得随意调整生成各阶段或变体选择的随机数调用顺序。

- 新地图家族：新增 `world` 生成器，返回 `MapRecipe`，接 `region_catalog`；不修改 DS1 解码器来硬塞布局。
- `world/cow_level.*`：独立牛场轮廓、四类二级边界、专属预设及隔离资源清单；不构造任务传送门。
- `world/outdoor_substitution.*`：消费原 DS1 分组，按 BordType 扫描宏格，区分空地、外部空白与受保护连接；目前限 GridSize=1 的二级边界，不等于通用 LvlSub 主题执行器。
- `world/outdoor_river.*`：原河岸变体表与桥位选择；`outdoor_layout` 提供已核实方向组合对应的河流标志。`world/outdoor_paths.*`：宏格路径适配、道路占用、原栅格化及地板转换表；不读取设备或修改玩法状态。
- `world/outdoor_cliffs.*`：将接触区间拆成边界段，选择无连接的悬崖段；`outdoor` 负责悬崖原预设及洞口扫描、营地固定槽位过渡，随后执行受保护的二级边界替换。
- `d2x_assets <MPQ> substitutions <Type>` 查询原模板分组及匹配／替换宏格编码；打包从牛场和野外资源清单收集模板及全变体，纯河水模板不作为可行走独立区域初始化。
- `maze/jail.cpp` 按层提供上下楼梯、传送点及首领房配置；`maze/catacombs.cpp` 初始化固定入口及相邻房，再交共享增长和主题替换。资源家族类型及变体数量由 maze 查询统一提供给报告、缺资源检查及打包。
- 新怪物：`gameplay/monsters/monster_spawn.*` 注册实际实现，再接 AI 和 `SceneAssets`；死亡保留原始身份。
- 怪物动画由 `SceneAssets::monsterAnimations` 按 MonsterKind 和动作索引；定义携带原 token／武器类别，组件按已核实 MonStats2 组合选择。合成结果记录组件完整性，缺动作或组件时明确拒绝加载，不回退到另一种怪物图片。普通类型仍复用公共近战，专属 AI 后续独立接入。
- 新物品效果：从 `content` 导入定义，在玩法／事务服务执行；HUD 仅展示结果。
- 新 UI：布局、绘制、命中分开；技能图标经 `Skills → SkillDesc → DC6`。
- 修改持久状态：同步 `state`、`save_codec`、`session_snapshot` 与规则指纹。
