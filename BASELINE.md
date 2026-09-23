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
- 成长与战斗属性：[角色属性实施计划](docs/CHARACTER_ATTRIBUTES.md)、[七职业技能树](docs/SKILLS.md)。
- 工程：[构建与分发](docs/BUILD_AND_SHARE.md)、[MPQ 清单](docs/MPQ_RESOURCES.md)、[第三方许可](docs/THIRD_PARTY.md)。

当前工作：七职业技能树和出生通用动作已接入源码，运行时从原 MPQ 读取节点、页签、图标、门槛与 `CharStats` 的初始技能。普通近战、弓／弩与投掷的基础攻击已接。女巫传送、火弹、火球、冰霜新星、静电力场接入原技能／弹体表的法力、等级伤害、协同与资源。F1–F8 通过悬停左右技能菜单图标绑定，按键切换对应鼠标技能并随存档保存；存档 v22、规则 v49，旧档不迁移。Windows Release 构建、七职业三页技能树截图、女巫技能菜单、快捷键存读档及完整 MPQ 分发目录已冒烟；远程战斗和女巫技能效果仍待逐项实机验收。运行时直接读取原始 MPQ；Buff、非直接装备属性与完整怪物规则仍待后续阶段。
