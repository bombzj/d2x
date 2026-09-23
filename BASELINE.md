# 项目基线

更新：2026-09-24。供维护者和协作 agent 从当前代码继续工作。

资源与规则数据以运行时挂载的原 MPQ 为准；`resources` 解码、`content` 类型化适配，玩法只接收只读定义。不要把抽取后的表当作另一个需维护的数据源，也不要在玩法或界面写死物品名、数值、概率与资源路径。原 MPQ 未记载的引擎规则单独实现并注明来源；未核实的规则暂缓。既有模块仍有历史硬编码，按 [实施清单](docs/ITEM_COMPLETION.md) 逐项清理。

| 文档 | 内容 |
| --- | --- |
| [能力与缺口](docs/baseline/STATUS.md) | 全项目已实现内容、限制、后续工作 |
| [模块与接口](docs/baseline/ARCHITECTURE.md) | 依赖方向、状态所有权、扩展入口 |
| [数据与生命周期](docs/baseline/DATA.md) | MPQ、地图、怪物、物品、存档 |
| [开发与交接](docs/baseline/DEVELOPMENT.md) | 构建、资源、协作约定、当前检查状态 |
| [Agent 约定](AGENTS.md) | 开始修改前必读 |

专题细节：

- 世界：[地图](docs/ACT1_MAPS.md)、[怪物生成](docs/MONSTER_POPULATION.md)。
- 物品：[原表](docs/ITEM_DATA.md)、[实例与事务](docs/ITEM_MODEL.md)、[包裹](docs/INVENTORY_UI.md)、[腰带](docs/BELT_AND_CONSUMABLES.md)、[储物箱](docs/STORAGE.md)。
- 本轮物品补全顺序与当前进度：[物品与掉落补全](docs/ITEM_COMPLETION.md)。
- NPC：[对话、服务与原路径移动](docs/NPC_COMPLETION.md)。
- NPC 交易：[购买实施顺序与限制](docs/NPC_TRADE.md)。
- 界面与保存：[经典 HUD](docs/CLASSIC_HUD.md)、[存档](docs/SAVES.md)。
- 成长与战斗属性：[角色属性实施计划](docs/CHARACTER_ATTRIBUTES.md)。
- 工程：[构建与分发](docs/BUILD_AND_SHARE.md)、[MPQ 清单](docs/MPQ_RESOURCES.md)、[第三方许可](docs/THIRD_PARTY.md)。

当前工作：角色与战斗属性首批实现包括运行时 MPQ 全职业 `CharStats`／`Experience` 成长、共用加点、调试切换、直接装备词缀、普通近战命中和原 `invchar` 左面板。玩家击杀怪物按 MPQ `MonStats`／`MonLvl`／`Experience` 及已核实的引擎等级差规则结算经验；资料片私人箱按 MPQ `Big Bank Page 1` 使用 6×8 格和原 `tradestash.dc6`。存档 v18、规则 v45，旧档不迁移。Windows 构建、击杀升级、储物箱截图和存读档冒烟已完成；键鼠和完整战斗仍待验收。运行时直接读取原始 MPQ。Buff、技能规则、非直接属性函数及完整怪物属性仍待后续阶段。
