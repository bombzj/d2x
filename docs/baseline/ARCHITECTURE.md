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
| `gameplay/combat/unit.*` | 公共单位视图、阵营／主人关系、伤害过滤、减伤和死亡 | `CombatUnit`、`CombatIdentity`、`DamageRequest` |
| `gameplay/skills/summoning.cpp` | 尸体消耗、宠物创建、上限、跟随与旅行；复用公共战斗 | `summonFromCorpse`、`updateCompanions` |
| `gameplay/character` | 角色已分配点、类型化成长与派生资源／战斗快照；不读 MPQ | `deriveCharacterAttributes` |
| `gameplay/items` | 物品／容器唯一状态及事务 | `InventoryService` |
| `gameplay/npc` | NPC 路径移动、凯恩鉴定与商店购买 | `advanceNpcPaths`、`planCainIdentification`、`planVendorStock`、`buyVendorItem` |
| `gameplay/quest` | 第一幕六项任务的分文件阶段规则与每难度记录 | `QuestRecord`、各任务 `Advance` |
| `gameplay/session` | 区域生命周期、出口、交互、死亡与存档协调 | `GameSession` |
| `persistence` | 角色值数据、原 D2S 编解码、文件替换和备份 | `CharacterSaveData`、`encodeSave/decodeSave` |
| `presentation` | GPU、音效、只读绘制、屏幕命中 | `SceneAssets`、`SceneView`、`SceneController` |
| `app` | 参数、输入、窗口、固定步调度 | `application.cpp`、`options.cpp` |

调用链：`输入 → Controller → GameCommand → GameSession → Simulation/Inventory → 状态与事件 → View`。

## 目录与头文件边界

- `content/{items,monsters,skills,character,npc,world}` 按原表所属领域组织；根层保留 `classic_data`、`lod_data` 和公共字符串表入口。
- `presentation/{inventory,hud,npc,actors,world,graphics,audio}` 按表现功能组织；同功能的绘制、控制与资源实现相邻，根层保留场景和总控制器。
- `world/outdoor` 收拢野外生成，`world/maze` 保留迷宫生成；地图、区域、导航和人口计划入口仍在 `world` 根层。
- `app/debug` 收拢本机调试传输与命令；应用入口、输入、角色前端和崩溃记录仍在 `app` 根层。目录分组不新增 CMake 库，既有目标和依赖方向不变。
- `GameSession` 独占 `Simulation`，会话头仅前置声明模拟类；完整定义由访问模拟的实现显式包含。构造、析构及模拟访问入口在实现文件，新增模拟私有函数不再经会话头传播到表现层。
- 控制器头前置声明会话与场景，库存布局和场景资源头前置声明会话；依赖完整对象的调用放在实现文件，消费者不依赖偶然的传递包含。
- `model/state.hpp`、`model/commands.hpp`、`skills/spec.hpp`、`classic_data.hpp` 和场景值成员仍是公共编译依赖。改变共享值类型仍会引起相关消费者重编，尚未将全部内容目录或场景缓存改为不透明实现，也未测量增量构建时间。
- 2026-10-01 本次目录／头文件调整仅做源码与差异审阅，未构建、运行检查、测试或打包；旧运行包不包含本次调整。

依赖约束：

- `d2x_gameplay → d2x_navigation`；玩法不链接 StormLib 或 raylib。
- 原 MPQ 是内容数据的唯一运行时来源；`content` 在加载时解析原 TXT 并提供类型化只读定义。提取文件仅供人工核对，不随源码维护或提交。算法常量与 MPQ 内容字段应分开记录。
- `content/character/character_attributes.*` 从 `CharStats.txt` 导入各职业起点与增长，`content/character/character_progression.*` 从 `Experience.txt` 导入各职业阈值；职业代码与原人物图形 token 在适配层匹配，不存放成长数值。`content/items/equipment_modifiers.*` 将装备实例的原 Properties 直接属性解译为 MPQ 无关的 `CharacterModifiers`。会话按需求闭包重算角色／装备快照；UI 只读显示，普通命中公式在 `gameplay/combat/accuracy.*`。
- `d2x_world → content/navigation`；生成器只产出资源配方，不创建 GPU 对象。
- `d2x_population → content/gameplay`；怪物计划与实体创建分离。
- `GameSession` 持有区域、模拟、物品服务和掉落状态。`Simulation` 借用稳定区域网格与房间索引。
- 待施法与持续引导是角色状态：`PlayerState.pendingCast/channel` 拥有技能参数、目标和计时，`Simulation` 仅推进，内部开始／推进／停止／释放函数显式接收角色引用。角色销毁或替换不留下会话级施法槽；引导显示从同一份状态派生，不参与 D2S 编码。
- 战斗通过 `combat/unit.*` 的实体 ID、CombatUnit 访问视图、阵营／所有权关系与公共伤害入口执行。玩家、佣兵、怪物共享选敌、碰撞过滤、减伤、持续伤害和击杀归属；派生属性／装备快照属于 PlayerState。规则和扩展边界见 [战斗阵营](../COMBAT_FACTIONS.md)。当前仍为单操控角色世界，命令、库存、区域激活和网络同步尚未实现多人；未来注册其他玩家时必须提供其实体状态、属性和可信命令来源，退出／加载不能沿用本地 restore 的全局重置。
- 当前区与非当前区状态只能存在一份；UI 不直接修改角色、物品或怪物。
- 图形按区域共享缓存；跨野外边界绘制相邻区域，碰撞与区域状态仍由会话切换。
- 步行边界由 `world/exits.cpp` 根据两侧原碰撞及连通区域生成 `LevelExit.passages`；会话按角色和点击终点选择可达通道，并保持世界坐标终点。`Grid::reachableFrom` 共用于预设开口识别和通道筛选；`Grid::segment` 的逐格碰撞与 A*／路径平滑共用网格。通道和待过界选择属于地图／会话运行状态，不进入 D2S。已构建且用户确认行走修复可用，不能据此视为完整跨区寻路或多人区域流式加载已实现。
- 应用持有窗口／原档案，会话作用域持有角色、世界、视图、管道与渲染目标。Esc 无面板时打开暂停的游戏菜单，选择 Save and Exit Game、关闭窗口或调试退出前保存有归属的角色；成功后作用域销毁。菜单的 Options 暂不执行，Return 或 Esc 收起菜单并隔离鼠标手势。启动地图、输入积累和存档路径不跨角色继承，保存失败不会销毁当前会话。整局 `snapshot()` 无现有测试／联机调用，已删除；角色存档经 `characterSave()`，不校验活世界 AI，读档在 `session_restore.cpp` 预备空白新局。

扩展入口：

- Travel 为开发目录／管道自由传送；WaypointTravel 为游戏传送，GameSession 校验两端激活与源点距离。SceneView.waypointSource 区分专用菜单与 Ctrl+F2 目录，首次交互发 WaypointActivated 而非直接开菜单。WorldState.waypoints 保存激活时间，表现层按 Objects 的 NU／OP（Operating）／ON（Opened）顺序及 FrameCnt／FrameDelta／CycleAnim／Start 计算动画阶段；普通物件共用这套只读播放规则，非循环段钳在末帧。
- `presentation/hud/hud/waypoint_view.cpp` 拼接 MPQ 菜单美术，依据 `WorldCatalog` 的 `Levels.Waypoint` 排列目的地，点击只提交 `WaypointTravel`。NPC 字幕和紧凑菜单分别在 `npc_dialogue_view.cpp`、`npc_menu_view.cpp`；正文仍来自 `NpcDialogueCatalog`，表现层不创建 NPC 服务状态。
- 回城卷轴 UseItem 在 session_consumables 中规划端点、消耗库存并替换 TownPortalState；UseTownPortal 走近后切区，营地返回关闭。蓝门仅作为本局状态派生图像，不混入静态 Region.objects；表现层按 Objects 的 OP 一次段、ON 循环段和 COF 透明绘制播放，读档新局不恢复蓝门。原野外传送点固定 LvlSub 片段由 outdoor 和 map_assembly 负责。

- `content/items/item_appearance.*` 从原物品表与 `ArmType.txt` 适配头盔、胸甲六图层及手部组件；`presentation/actors/hero_assets.cpp` 按当前穿戴与动作类别合成并缓存角色动画。穿脱与读档重建表现，GPU 状态不入存档；死亡模式使用原基础身体资源。`Graphics::composite` 将 COF 顺序方向映射到 DCC 方向；缺部位 token 时提示并保留该部位基础图层，整套动作图不完整时提示并回退基础身体。怪物实际位移及面向缓存在 SceneView，不改变战斗状态编码。
- `region.cpp` 在 DS1 对象后补原规则要求的 Navi 中立单位，位置来自血腥荒地边界预设中心；新增对象改变实体分配，因此同步规则指纹。`GameSession` 通过类型化回调向 Simulation 提供真实怪物基础行走速度，玩法仍不读取 MPQ。
- `app/debug/debug_pipe.*` 为 Windows 本机当前用户 ACL 的非阻塞传输，`debug_commands.*` 在主线程解析 JSON、只读查询或提交 GameCommand；不把 Win32 带入玩法。`DebugKill` 复用正常死亡链，命令入口限制活着的可见激活目标；默认不开启。脚本 `scripts/Send-D2XCommand.ps1` 为通用调用入口，详见 [调试协议](../DEBUG_PIPE.md)。
- 金币地面实例仍由 InventoryService 拥有，GameSession 在正常拾取距离／通路检查后 consume 并增加 PlayerState.gold，余额按原实例保留；UI 只读显示钱包。普通容器拒绝金币，角色存档检查钱包，不携带或校验地面实例。
- `content/monsters/monster_loot.*` 将真实怪物身份及原表解析为死亡 TC 入口、等级与暂缓原因，供 `session_loot.cpp` 和 `d2x_assets <MPQ> loot-entry <monster> <rank> <difficulty> <level-ID> [superunique-ID]` 共用。会话在已结算 ID 检查后解析并输出诊断，仍由 LootSystem 记录死亡，不直接创建普通物品替代缺失品质。
- `gameplay/loot/treasure.cpp` 执行类型化 TC 的单人递归选择，不依赖 MPQ 或 GPU；`content/lod_data.cpp` 导入原表并生成自动类别。`d2x_assets <MPQ> treasure <TC-name> [seed] [monster-level]` 仅查询选择路径。`gameplay/loot/quality.*` 执行品质请求，`content/items/item_quality.*` 适配原 ItemRatio 并在 TC 叶子调用品质规则、规划支持的消耗品批次；会话提交随机状态与死亡 ID，再调用库存创建支持的普通实例。LootDeferred 只通知暂缓原因，不持久化为待补发请求。
- 现有资源工具 `quality <code> <item-level> <MF> [seed] [unique set rare magic modifiers]` 输出品质请求与各次分母／掷骰；`loot-plan <TC> <item-level> <seed> [upgrade-level]` 调用游戏共用规划器，显示候选／暂缓原因，不创建物品或会话。二者不代替实际击杀拾取验收。
- `gameplay/model`：共享命令、事件、状态及规则类型；不执行会话协调。
- `content/items/equipment_data.*` 将原 ItemTypes／物品字段转换为只读装备规则；`gameplay/items/equipment_rules.*` 定义部位和类型查询，`equipment.cpp` 规划基础穿脱与左右手冲突。InventoryService 保持实例唯一归属；GameSession 提供可信角色需求，不接受 UI 指定属性。
- `EquipItem` 提交实例版本及目标部位，空部位表示卸下，可指定背包／私人箱／地面目的地；穿戴时不能同时指定目的地。通用 Move／Transfer 不允许直接修改装备容器。腰带仍走 EquipBelt 原子缩容，二者共用需求校验；会话额外验证地面目的地碰撞与通路。表现层按原面板坐标显示部位，按真实库存加载原图，并从装备状态重建人物外观。
- `gameplay/items/equipment_stats.*` 从普通装备实例派生基础武器伤害、防御、格挡及角色等级；`durability.cpp` 负责非堆叠装备损耗和版本事件。GameSession 在成功库存操作后刷新 Simulation 的只读派生缓存，并以回调协调战斗损耗；Simulation 不读取 MPQ 或设备输入。普通怪物准确率由会话提供类型化值，缺核实数据则不执行新增命中分支。
- `d2x_assets <MPQ> save-info <file.d2s>` 只读解码原版 v96 角色与物品，不代替 GameSession 的角色装备恢复检查。不打印本来就不在 D2S 中的世界时间和战斗随机数。
- `gameplay/simulation`：固定步调度与模拟生命周期；`combat`、`player`、`monsters`、`consumables`、`loot` 分别持有对应实现。
- `content/skills/skill_data.*` 从运行时 MPQ 原表建立七职业技能目录、通用动作、初始物品技能、位置、前置、等级与页签；`content/skills/sorceress_data.*` 导入女巫优先技能的 MPQ 法力／伤害／协同／弹体定义与 Levels 传送许可；`content/items/item_projectiles.*` 把 Weapons／Missiles 原表转为只读弹体定义。`presentation/hud/hud/skill_tree_view.cpp` 和原 DC6 绘制技能树。会话持有原技能 ID 对应的等级和点数；UI 仅选择技能并提交命令。
- `gameplay/combat/physical_projectiles.cpp` 接普通远程伤害与弹体，`InventoryService::consumeEquipped` 为会话提供可信的箭袋／投掷堆叠消耗；玩法层不读取 MPQ 或图形资源。
- `UseSkill → GameSession::useSkill → resolveSkill → Simulation::beginSkillCast` 是原 ID 驱动的技能入口；`gameplay/skills/spec.hpp` 为类型化定义，`resolve.cpp` 计算等级／协同，`casting.cpp` 推进出手和引导。原演示定义、直接枚举施法、旋风／跳跃特例已删除；未实现技能明确拒绝，不代替为普通攻击。已接入四项被动，其余仅记录等级。
- `content/skills/state_data.*` 导入原 States；`gameplay/effects/state.*` 独立管理目标单位的状态容器、来源／原状态／实例身份、25 Hz 生命周期、重施与组互斥、数值和反应。冰封装甲经 `SkillCastSpec.appliedEffect` 接入，移除反应与实例同寿命。`effects/reactions.cpp` 实施已核实的近战冻结事件；完整光环、诅咒、周期伤害、其他单位的属性消费者仍待逐项接入，详见 [技能](../SKILLS.md#状态效果系统)。
- `world/maze`：`room_graph.hpp` 持有房间连接与特殊房放置，`generation.cpp` 编排增长、主题替换及配方输出，`cave.cpp`／`crypt.cpp` 提供家族配置；`barracks.cpp` 管兵营入口、楼梯／铁匠房及外侧回廊坐标连接，`resources.cpp` 按家族检查并枚举原资源。`world/maze.hpp` 保留公共入口。静态家族配置不消费随机数；不得随意调整生成各阶段或变体选择的随机数调用顺序。

- 新地图家族：新增 `world` 生成器，返回 `MapRecipe`，接 `region_catalog`；不修改 DS1 解码器来硬塞布局。
- `world/cow_level.*`：独立牛场轮廓、四类二级边界、专属预设及隔离资源清单；不构造任务传送门。
- `world/outdoor/outdoor_substitution.*`：消费原 DS1 分组，按 BordType 扫描宏格，区分空地、外部空白与受保护连接；目前限 GridSize=1 的二级边界，不等于通用 LvlSub 主题执行器。
- `world/outdoor/outdoor_shrines.*`：根据 `Levels.SubShrine` 指向的原 LvlSub 记录，把类型 5 固定分组及对象放到第一幕野外可用宏格；不持有 MPQ 或玩法状态。
- `world/outdoor/outdoor_river.*`：原河岸变体表与桥位选择；`outdoor_layout` 提供已核实方向组合对应的河流标志。`world/outdoor/outdoor_paths.*`：宏格路径适配、道路占用、原栅格化及地板转换表；不读取设备或修改玩法状态。
- `world/outdoor/outdoor_cliffs.*`：将接触区间拆成边界段，选择无连接的悬崖段；`outdoor` 负责悬崖原预设及洞口扫描、营地固定槽位过渡，随后执行受保护的二级边界替换。
- `d2x_assets <MPQ> substitutions <Type>` 查询原模板分组及匹配／替换宏格编码；打包从牛场和野外资源清单收集模板及全变体，纯河水模板不作为可行走独立区域初始化。
- `maze/jail.cpp` 按层提供上下楼梯、传送点及首领房配置；`maze/catacombs.cpp` 初始化固定入口及相邻房，再交共享增长和主题替换。资源家族类型及变体数量由 maze 查询统一提供给报告、缺资源检查及打包。
- 新怪物：`gameplay/monsters/monster_spawn.*` 注册实际实现，再接 AI 和 `SceneAssets`；死亡保留原始身份。
- 怪物动画由 `SceneAssets::monsterAnimations` 按 MonsterKind 和动作索引；定义携带原 token／武器类别，组件按已核实 MonStats2 组合选择。合成结果记录组件完整性，缺动作或组件时明确拒绝加载，不回退到另一种怪物图片。普通类型仍复用公共近战，专属 AI 后续独立接入。
- 新物品效果：从 `content` 导入定义，在玩法／事务服务执行；HUD 仅展示结果。
- 新 UI：布局、绘制、命中分开；技能图标经 `Skills → SkillDesc → DC6`。
- 修改持久状态：核对原 D2S v96 字段，在 `persistence/d2s_*` 映射并同步 `state`、`character_save` 与支持文档。不升级格式、不写私有字段或规则指纹；未支持数据明确拒绝。
