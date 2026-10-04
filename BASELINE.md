# 项目当前基线

更新：2026-10-05。实现以当前源码及对应模块为准；本页仅维护全项目范围、源码／包差异和交接入口。完整索引见 [文档目录](docs/README.md)，修改前阅读 [AGENTS.md](AGENTS.md)。

## 当前范围

| 领域 | 当前事实 | 具体边界 |
| --- | --- | --- |
| 架构 | 九项基础解耦切片已完成；客户端值契约、本地适配、角色组合、公共单位／技能、库存／装备、区域、任务／奖励及固定步已有独立入口 | [架构](docs/architecture/OVERVIEW.md)、[后续改造](docs/architecture/REFACTOR_PLAN.md) |
| 世界 | 五幕城镇与已接入的地图生成／按需加载，房间激活怪物、自动地图可见格揭示及边车持久化 | [地图](docs/gameplay/world/MAPS.md)、[怪物](docs/gameplay/world/MONSTERS.md)、[自动地图](docs/gameplay/world/AUTOMAP.md) |
| 角色／技能 | 七职业成长、学习与原图技能树；已有通用执行、被动、光环及部分职业行为，死灵法师三页共30项、亚马逊三页共30项均有执行／被动入口 | [角色模块](docs/modules/CHARACTER.md)、[公共技能](docs/gameplay/skills/COMMON.md)、[职业技能目录](docs/README.md#玩法规则) |
| 物品／NPC | 唯一库存事务、两组武器、装备派生、掉落品质、腰带／箱子／方块、NPC 对话与已有交易／雇佣服务 | [物品支持](docs/gameplay/items/SUPPORT.md)、[NPC](docs/gameplay/npc/INTERACTIONS.md) |
| 任务／保存 | 五幕 27 项日志任务的主要流程与各幕规则模块；原 D2S v96，不写项目私有版本或规则指纹，旧内部档不迁移 | [任务系统](docs/gameplay/quests/SYSTEM.md)、[存档](docs/modules/SAVES.md) |
| 联机 | 多玩家所有权、并行活区、网络后端及 D2GS 兼容均未实施；当前先继续单机解耦 | [联机设计](docs/architecture/MULTIPLAYER.md) |

加载原资源、显示技能树或任务入口可用，不等于完整原版行为已还原。具体未实现项只在所属模块／专题维护。

## 源码与运行包

| 批次 | 源码事实与验证范围 | `dist/current` |
| --- | --- | --- |
| 亚马逊被动与魔法整页 | 剩余十项逐项Release构建／打包后再进入下一项，依据当前MPQ和本地D2MOO；有限联合冒烟覆盖内视、慢速箭、暴击、三项防御被动、刺入、诱饵／女武神装备与生命周期、弓箭／闪电之怒穿透及原引导箭排除，原装备穿透叠加、D2S同进程／新进程重载及最终退出0通过。亚马逊现30/30入口，随机流／路径／客户端边界见[亚马逊](docs/gameplay/skills/AMAZON.md#被动与魔法技能整页) | 已包含十项、规则指纹和当前文档 |
| 亚马逊标枪与长矛整页 | 十项按顺序分别Release构建／打包后进入下一项，含原瘟疫复核，依据当前MPQ及本地D2MOO；包内有限联合冒烟覆盖戳刺双武器序列、元素／毒伤、刺爆堆叠／耐久、充能弹、Fend多目标、连锁与分裂闪电、延迟和清理。十项等级／标枪数量／长矛耐久同进程及新进程D2S保持，新进程退出0。参考重建差异及原客户端／路径限制见[亚马逊](docs/gameplay/skills/AMAZON.md#标枪与长矛技能整页) | 已包含十项、规则指纹和当前文档 |
| 亚马逊弓与弩整页 | 十项逐项Release构建／打包后进入下一项，参考当前MPQ和本地D2MOO；联合有限冒烟覆盖无弹药魔法箭、元素／范围伤害、冻结、引导、扇形／连射及牺牲火单位；弩复验、多重箭独立掷值、十项等级同进程／新进程D2S重载及退出0通过。客户端／路径限制见[亚马逊](docs/gameplay/skills/AMAZON.md#弓与弩技能整页) | 已包含十项、规则指纹和当前文档 |
| 死灵法师召唤整页 | 剩余七项逐项Release构建、打包后再进入下一项；四石魔、四元素法师、合格原身份重生与三被动求值接入。包内有限联合冒烟、镶jewel铁魔原kf保存／新进程恢复、十项等级保持及退出0通过；原型／装备消费者和路径限制见[死灵法师](docs/gameplay/skills/NECROMANCER.md#召唤技能整页) | 已包含召唤整页、原kf铁魔和当前文档 |
| 死灵法师毒素与白骨／头文件依赖收窄 | 整页十项按顺序逐项完成 Release 构建与打包，参考当前 MPQ 和本地 D2MOO；包内联合冒烟、十项原技能等级同进程／新进程保存重载及退出0通过。修复飞弹／尸体渲染编号冲突；行为枚举依赖264→34、完整技能定义113→26。客户端／路径与有限检查边界见 [死灵法师](docs/gameplay/skills/NECROMANCER.md#毒素与白骨技能)、[执行模块](docs/modules/SKILL_RUNTIME.md) | 已包含全部十项、依赖收窄与当前文档 |
| 方块合成与 Crafted | 当前 MPQ 全部146条启用配方、36条 Crafted、原生 quality=8 与 TXT 编号、Token 使用及三类红门接入；Windows Release 和全部配方正式合成、物品保存重载、门户往返冒烟通过，具体消费者与事件首领限制见 [方块](docs/gameplay/items/CUBE_AND_GOLD.md) | 已包含 |
| 物品／装备复核与镶嵌 | 对照当前 MPQ 和本地 reference，补齐宝石／符文／三品质 jewel 镶嵌、孔内身份与顺序、需求、符文之语、8 条加孔与去镶嵌配方、原生 D2S；同步 carry1、任务互斥及职业药剂修正。Windows Release 与有限实机冒烟通过，含 UI 拖入、属性和保存重载；效果消费者及原图孔位叠层仍有限，见 [物品支持](docs/gameplay/items/SUPPORT.md)、[参数覆盖](docs/gameplay/items/DATA.md) | 已包含 |
| 五幕地图复核与营地道路 | 核对当前MPQ与本地参考，修正交界地板／道路、主题替换、KillEdge、屋顶参数、熔岩动画、丛林实际连接及山顶零操作距离；Trees.ds1恢复完整13组，仅跳过原不完整末组。Windows Release及更新包通过；Trees修订后五种子各136区域加载／出口关联、原树显示及临时档重载复验通过，主批另有40张代表地图显示及营地往返步行记录，见[地图验证](docs/gameplay/world/MAPS.md#本批验证) | 已更新，包含Trees兼容、地图规则v9及当前文档 |
| 九项基础重构、自动地图、五幕任务、死灵法师诅咒与启动音频修复 | 已有 Windows Release 与有限冒烟记录；各模块保留实际覆盖及未覆盖项 | 2026-10-04 既有包包含这些批次 |
| 玩家死亡与装备尸体 | Windows Release 构建通过；有限冒烟覆盖死亡提示、Esc 满资源回城、尸体 D2S 同进程／新进程回城恢复及点击取回初始法杖；宠物死亡时序和复杂库存组合未验收 | 已包含，见 [死亡规则](docs/gameplay/characters/PLAYER_DEATH.md) |
| 传送点与开发快捷键清理 | Windows Release 构建通过；实机查看原菜单、默认第一点／读档开启及关闭按钮，打开时祭坛计时继续下降；修复白色字体变换导致标题透明和调试重置字段遗留引用 | 已包含，见 [HUD](docs/gameplay/ui/CLASSIC_HUD.md)、[地图模块](docs/modules/MAP.md) |
| 文档整理 | 目录按职责归类，合并重复架构／状态／旧计划，技能公共规则与职业说明分开，修正过期保存和入口说明；打包同步清理已移走的说明文件 | 已同步当前文档 |

旧批次的启动或有限冒烟不认证后续源码；Windows 已有运行证据，Linux 尚未实际编译或运行。日志／截图位于不提交的 `artifacts/`，准确证据见 [会话](docs/modules/SESSION.md#九项重构联合收尾与冒烟)、[任务](docs/gameplay/quests/ACT3_5.md#冒烟范围2026-10-04)、[诅咒](docs/gameplay/skills/NECROMANCER.md#诅咒逐项实现) 与 [自动地图](docs/gameplay/world/AUTOMAP.md)。

## 后续工作入口

继续工作从 [改造方案的执行顺序](docs/architecture/REFACTOR_PLAN.md#下一步执行顺序) 选择完整调用链，读取对应模块与玩法规则。完成后直接更新已有基线，删除被替代的说明，不追加另一份总状态或构建流水账。

最新技能批按用户授权逐项 Release 构建、更新 `dist/current` 并运行包内 EXE，未编写测试程序。毒素与白骨证据位于 `artifacts/bone-skills-20261004/`，召唤整页在 `artifacts/summon-skills-20261005/`，弓与弩整页在 `artifacts/amazon-bow-20261005/`，标枪与长矛整页在 `artifacts/amazon-spear-20261005/`，被动与魔法整页在 `artifacts/amazon-passive-20261005/`；各批均有联合冒烟／重载证据，长矛新进程重载实例退出0。stderr 的 Trees.ds1 兼容提示属于已知原资源诊断，不是技能错误。只使用独立临时角色；资源、存档和截图不纳入 Git。旧全项目包证据仍在 `artifacts/package-smoke-20261004/`。有限冒烟不认证全部任务／技能、Linux 或完整原版兼容。
