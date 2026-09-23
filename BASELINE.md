# 项目基线

更新：2026-09-23。供维护者和协作 agent 从当前代码继续工作。

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
- 界面与保存：[经典 HUD](docs/CLASSIC_HUD.md)、[存档](docs/SAVES.md)。
- 工程：[构建与分发](docs/BUILD_AND_SHARE.md)、[MPQ 清单](docs/MPQ_RESOURCES.md)、[第三方许可](docs/THIRD_PARTY.md)。

当前工作：用户报告的洞口、移动方向、手部外观与 NPC 问题。已修寻路越点回头、16 方向索引与 COF 图层顺序，手部外观响应穿脱／读档；血腥荒地按原边界预设补弗拉维。怪物行走接真实 MonStats.Velocity，但生命、伤害、攻击间隔等仍是 MVP。洞口缺块尚未定位，不宣称已修复，不修改隐藏出口标记或原资源。当前存档格式 v8、规则 v27，旧规则档不迁移；最后提交 `a2fc351`，本轮修改未提交。验证与限制见核心基线；[Windows 调试管道](docs/DEBUG_PIPE.md) 可查询对象、出口并穿脱装备。
