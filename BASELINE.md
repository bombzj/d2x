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

当前工作：怪物掉落。已接死亡入口、ItemRatio 品质请求、普通消耗品／箭袋数量、金币原数量与 TC 倍率、钱包拾取；未知实例仍整批暂缓。按最新授权增加 [Windows 调试管道](docs/DEBUG_PIPE.md) 与 PowerShell 通用客户端，实际击杀、箭袋拾取、金币入钱包、重复击杀拒绝和保存恢复已验证。当前存档 v8、规则 v26，旧档明确拒绝；任务／随从入口、完整品质实例、投掷武器及金币经济仍有缺口。最后提交为 `dfa9029`，新增修改未提交，原资源保持不变；不添加专用测试程序。
