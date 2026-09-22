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

当前工作：按最新指令转入怪物掉落，沿用逐项实现、构建及现有入口验证的方式，不编写测试脚本／用例。已接入 LoD 单人 TC 递归选择、NoDrop、正负 Picks、品质修正继承、自动类别和根 TC 等级升级，现有资源工具可查询选择路径；品质、数量及实例生成尚未接通，游戏内怪物掉落仍关闭。装备剩余工作暂停，保留普通装备已实现内容及交互验收缺口，不宣称完整。当前存档 v7、规则 v24，本阶段未改变持久语义。源码未提交，原资源保持不变。
