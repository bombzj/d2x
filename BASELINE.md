# 项目当前基线

更新：2026-10-04。实现以当前源码及对应模块为准；本页仅维护全项目范围、源码／包差异和交接入口。完整索引见 [文档目录](docs/README.md)，修改前阅读 [AGENTS.md](AGENTS.md)。

## 当前范围

| 领域 | 当前事实 | 具体边界 |
| --- | --- | --- |
| 架构 | 九项基础解耦切片已完成；客户端值契约、本地适配、角色组合、公共单位／技能、库存／装备、区域、任务／奖励及固定步已有独立入口 | [架构](docs/architecture/OVERVIEW.md)、[后续改造](docs/architecture/REFACTOR_PLAN.md) |
| 世界 | 五幕城镇与已接入的地图生成／按需加载，房间激活怪物、自动地图可见格揭示及边车持久化 | [地图](docs/gameplay/world/MAPS.md)、[怪物](docs/gameplay/world/MONSTERS.md)、[自动地图](docs/gameplay/world/AUTOMAP.md) |
| 角色／技能 | 七职业成长、学习与原图技能树；已有通用执行、被动、光环及部分职业行为，死灵法师十项诅咒均有入口 | [角色模块](docs/modules/CHARACTER.md)、[公共技能](docs/gameplay/skills/COMMON.md)、[职业技能目录](docs/README.md#玩法规则) |
| 物品／NPC | 唯一库存事务、两组武器、装备派生、掉落品质、腰带／箱子／方块、NPC 对话与已有交易／雇佣服务 | [物品支持](docs/gameplay/items/SUPPORT.md)、[NPC](docs/gameplay/npc/INTERACTIONS.md) |
| 任务／保存 | 五幕 27 项日志任务的主要流程与各幕规则模块；原 D2S v96，不写项目私有版本或规则指纹，旧内部档不迁移 | [任务系统](docs/gameplay/quests/SYSTEM.md)、[存档](docs/modules/SAVES.md) |
| 联机 | 多玩家所有权、并行活区、网络后端及 D2GS 兼容均未实施；当前先继续单机解耦 | [联机设计](docs/architecture/MULTIPLAYER.md) |

加载原资源、显示技能树或任务入口可用，不等于完整原版行为已还原。具体未实现项只在所属模块／专题维护。

## 源码与运行包

| 批次 | 源码事实与验证范围 | `dist/current` |
| --- | --- | --- |
| 九项基础重构、自动地图、五幕任务、死灵法师诅咒与启动音频修复 | 已有 Windows Release 与有限冒烟记录；各模块保留实际覆盖及未覆盖项 | 2026-10-04 既有包包含这些批次 |
| 玩家死亡与装备尸体 | Windows Release 构建通过；有限冒烟覆盖死亡提示、Esc 满资源回城、尸体 D2S 同进程／新进程回城恢复及点击取回初始法杖；宠物死亡时序和复杂库存组合未验收 | 已包含，见 [死亡规则](docs/gameplay/characters/PLAYER_DEATH.md) |
| 传送点与开发快捷键清理 | Windows Release 构建通过；实机查看原菜单、默认第一点／读档开启及关闭按钮，打开时祭坛计时继续下降；修复白色字体变换导致标题透明和调试重置字段遗留引用 | 已包含，见 [HUD](docs/gameplay/ui/CLASSIC_HUD.md)、[地图模块](docs/modules/MAP.md) |
| 文档整理 | 目录按职责归类，合并重复架构／状态／旧计划，技能公共规则与职业说明分开，修正过期保存和入口说明；打包同步清理已移走的说明文件 | 已同步当前文档 |

旧批次的启动或有限冒烟不认证后续源码；Windows 已有运行证据，Linux 尚未实际编译或运行。日志／截图位于不提交的 `artifacts/`，准确证据见 [会话](docs/modules/SESSION.md#九项重构联合收尾与冒烟)、[任务](docs/gameplay/quests/ACT3_5.md#冒烟范围2026-10-04)、[诅咒](docs/gameplay/skills/NECROMANCER.md#诅咒逐项实现) 与 [自动地图](docs/gameplay/world/AUTOMAP.md)。

## 后续工作入口

继续工作从 [改造方案的执行顺序](docs/architecture/REFACTOR_PLAN.md#下一步执行顺序) 选择完整调用链，读取对应模块与玩法规则。完成后直接更新已有基线，删除被替代的说明，不追加另一份总状态或构建流水账。

本轮按用户授权完成 Release 构建、更新 `dist/current` 并运行包内 EXE，未编写测试程序。证据位于 `artifacts/package-smoke-20261004/`：三个启动现场 stderr 均为空，最终两个实例退出 0；包内 EXE 与构建产物 SHA-256 相同。只使用独立临时角色；资源、存档和截图不纳入 Git。本次有限冒烟不认证全部任务／技能、Linux 或完整原版兼容。
