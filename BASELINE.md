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

当前工作：传送点与回城卷轴。第一幕九个传送点已生成／识别；新角色包括营地在内全部未开启，实际走近点击才解锁，播放原 nu/on/op 动画，普通传送只允许已开启目标。调试 travel／F2 目录独立，不自动解锁。背包回城卷轴已接双向蓝门，返程关闭并支持保存恢复。当前存档 v10、规则 v30，旧档不迁移；最后提交 `b7cb503`，本轮传送改动未提交。原模板放置仍为项目适配，洞口视觉问题和完整怪物属性等既有缺口保留；验证见核心基线。
