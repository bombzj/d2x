# 项目基线

更新：2026-09-23。供维护者和协作 agent 从当前代码继续工作。

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
- 工程：[构建与分发](docs/BUILD_AND_SHARE.md)、[MPQ 清单](docs/MPQ_RESOURCES.md)、[第三方许可](docs/THIRD_PARTY.md)。

当前工作：物品与掉落补全已接入并完成基础冒烟（见 [实施清单](docs/ITEM_COMPLETION.md)）；NPC 对话、治疗、付费鉴定与原 DS1 路径移动已接入；商人原表货架、购买事务、交互菜单和 MPQ 商店面板已接入源码，购买界面已构建并截取 Akara 商店画面，购买操作仍待验收。NPC 聊天现改为顶部小字幕框与附近菜单，传送点选择改为 MPQ 原边框／背景／图标布局；NPC 字幕和商店已与英文原版截图对照并完成本地画面复核；传送点菜单仍待画面验收。详见 [NPC 实施顺序](docs/NPC_COMPLETION.md)、[NPC 交易](docs/NPC_TRADE.md)与 [经典 HUD](docs/CLASSIC_HUD.md)。传送点与回城卷轴阶段状态：第一幕九个传送点已生成／识别；新角色包括营地在内全部未开启，实际走近点击才解锁，按原 NU／OP（启动）／ON（开启）模式和 Objects 动画字段播放，普通传送只允许已开启目标。普通场景物件也按 FrameCnt／FrameDelta／CycleAnim／Start 播放，非循环的箱子、尸体等停在末帧。调试 travel／F2 目录独立，不自动解锁。背包回城卷轴已接双向蓝门，OP 段只播一次后进入 ON 循环，使用透明混合；返程关闭并支持保存恢复。当前存档 v14、规则 v38，旧档不迁移。此前 NPC 对话阶段的 Windows Release 构建、完整 `assets/mpq2` 的新游戏与读档短帧冒烟通过；EXE 直接读取该目录。装备外观与所有物品交互仍待完整视觉和操作验收。原模板放置、洞口视觉与完整怪物属性等既有缺口保留；验证见核心基线。
