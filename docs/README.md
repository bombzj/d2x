# 文档目录

修改前阅读[项目基线](../BASELINE.md)与[协作约定](../AGENTS.md)，再读对应模块。本页只维护文档归属和入口，不维护功能状态或运行记录。

Agent快速入口：[架构速览](architecture/OVERVIEW.md#agent速览) → [编码规范](development/CODING.md) → [测试与验证](development/TESTING.md)。

## 负责页面

| 文档层 | 只维护什么 | 不放什么 |
| --- | --- | --- |
| 根README／AGENTS | 产品入口／协作约束 | 批次记录、详细架构、玩法台账 |
| BASELINE | 当前实现范围、架构边界、功能缺口与专题链接；保持短篇 | 开发过程、构建／打包／运行状态、包身份、验证记录、源码实现细节 |
| architecture | 大模块、依赖、数据流、设计依据；MULTIPLAYER维护后续顺序 | 重复的当前包清单、逐技能验收 |
| modules | 代码入口、状态所有权、接口与失败边界 | 复制各职业／物品／任务规则和全部运行记录 |
| gameplay及分类README | 分类入口、原版规则与参考结果、当前实现范围及明确缺口 | 自研运行台账、跨专题重复记录、无范围的完成宣称 |
| development | 编码与验证方法、构建／启停／诊断、原版对照方法及部署边界 | 历次构建／打包流水、历史包身份、业务实现清单 |
| resources／licenses | 原资源、参考来源、固定版本与许可 | 玩法完成宣称 |

更新原负责段落，不按每次会话追加日志。长期文档只维护当前实现、可定位的原版依据、已知差异及待核实项，统一口径见[总计划](architecture/MULTIPLAYER.md#21-统一状态口径)。原版参考记录版本、函数／RVA、MPQ表字段，或实机条件、操作和关键结果；混合连接明确原版所在的一侧。

构建、打包和自研运行情况在当次交付中说明，不进入长期功能台账。日志、截图、运行包及排障副本是可清理的临时产物；文档不依赖临时目录长期存在，也不把已删除材料写成现存依据。修改历史由Git追溯。

移动内容时保留有效证据和链接，外部已引用的锚点保留或同步修正。保存语义变化同步SAVES／格式／校验边界；执行授权以[协作约定](../AGENTS.md)和[验证指南](development/TESTING.md)为准，不在各页重复维护授权规则。

| 开发与参考主题 | 入口 |
| --- | --- |
| 独立控制台D2GS、PvPGN后端配置、启动／关闭与恢复 | [PvPGN服务端](development/PVPGN_SERVER.md) |
| 内核子系统、状态所有权、命令／固定步／事务／可靠事件与空实现入口 | [内核子系统](modules/SERVER_SYSTEMS.md) |
| 原版来源、固定版本与许可 | [资料来源](resources/THIRD_PARTY.md)、[MPQ](resources/MPQ.md)、[原许可](licenses) |
| 启动、构建、打包与已有诊断 | [开发指南](development/BUILD_AND_RUN.md)、[调试管道](development/DEBUG_PIPE.md)、[崩溃记录](development/CRASH_REPORTS.md) |
| 原版人物数值对照的存档装配、覆盖矩阵与截图数据契约 | [对照存档计划](development/CHARACTER_REFERENCE_FIXTURES.md) |
| 原版DLL地图对照与实机房间恢复排查 | [地图对照排查](development/MAP_PARITY.md) |

已完成的结构改造内容并入架构／现有模块，不再维护第二份历史重构计划；已删除执行器的SESSION／UNITS／SKILL_RUNTIME／REWARDS说明不再作为模块入口。原版结构比较仍见[参考设计](architecture/REFERENCE_DESIGN.md)。后续服务端精确复刻的目标结构、D2MOO证据、公共函数边界及重构验收统一见[服务端战斗复刻目标架构](architecture/SERVER_COMBAT_RECONSTRUCTION.md)，该页是目标设计，不是当前实现状态；全局顺序仍由[总计划](architecture/MULTIPLAYER.md)维护。

目标设计按“合理承载全部内容”组织，不以当前代码迁移步骤为中心：[战斗领域与组合设计](architecture/COMBAT_DOMAIN_DESIGN.md)负责模块职责、所有权、依赖和跨内容协作；[公共规则与原版算法设计](architecture/COMBAT_SHARED_RULES.md)负责原函数参考、共享粒度、输入输出与精确性契约。两页由总纲统一导航，不另维护完成台账。

## 当前模块

| 模块 | 内容 |
| --- | --- |
| [客户端](modules/CLIENT.md) | I*Client值契约、消息连接、公共消费者与两类后端适配 |
| [人物](modules/CHARACTER.md) | 原属性／技能、纯提示投影、请求／保存值边界 |
| [库存](modules/INVENTORY.md) | 人物／世界物品、内容准备、事务、生成／商店／加工与原包消费者的代码入口 |
| [地图](modules/MAP.md) | 原房间／地形、碰撞、地图UI与本局探索 |
| [怪物](modules/MONSTERS.md) | 内容准备、人口／AI／实体／技能／效果及原包适配的代码入口与所有权 |
| [NPC／任务](modules/NPC_QUEST.md) | 客户端任务／对白端口及修改入口，规则与生命周期链接专题 |
| [存档／偏好](modules/SAVES.md) | 独立D2S v96编码／拒绝、原服保存、客户端偏好／凭据 |

## 规则与表现专题

| 领域 | 入口 |
| --- | --- |
| 角色 | [属性／成长](gameplay/characters/ATTRIBUTES.md)、[佣兵](gameplay/characters/HIRELINGS.md)、[死亡／尸体](gameplay/characters/PLAYER_DEATH.md) |
| 战斗 | [阵营](gameplay/combat/FACTIONS.md)、[数值显示](gameplay/combat/NUMBERS.md)、[攻击／弹体](gameplay/combat/ATTACKS.md)、[武器命中效果](gameplay/combat/WEAPON_EFFECTS.md)、[随机机制](gameplay/combat/RANDOMNESS.md) |
| 技能 | [全分类目录](gameplay/skills/README.md)、[公共规范／所有权／状态](gameplay/skills/COMMON.md)、[MPQ 选择器／树描述](gameplay/skills/DESCRIPTIONS.md)；目录包含七职业、通用、怪物／精英、佣兵／召唤物、物品技能与特殊／未归档条目 |
| 物品 | [全分类目录](gameplay/items/README.md)、[公共规范](gameplay/items/COMMON.md)；定义／生成／属性、装备／库存／容器／消耗、镶嵌／耐久、掉落／地面／金币、方块／任务／经济、原包表现／特殊身份及原版依据 |
| NPC | [交互／对白](gameplay/npc/INTERACTIONS.md)、[交易／服务](gameplay/npc/TRADE.md) |
| 任务 | [系统与协议／所有权](gameplay/quests/SYSTEM.md)、[第一幕执行与证据](gameplay/quests/ACT1.md)、[第二幕执行与证据](gameplay/quests/ACT2.md)、[第三至第五幕执行与缺口](gameplay/quests/ACT3_5.md) |
| 怪物 | [全分类目录](gameplay/monsters/README.md)、[公共规范](gameplay/monsters/COMMON.md)、[自然生成工作总览](gameplay/monsters/POPULATION.md#自然生成工作总览)；五幕台账、精英／固定身份、首领、人口、原包表现及特殊／未归档单位 |
| 世界 | [五幕地图](gameplay/world/MAPS.md)、[物件](gameplay/world/OBJECTS.md)、[自动地图](gameplay/world/AUTOMAP.md)、[照明](gameplay/world/LIGHTING.md) |
| UI | [HUD／技能菜单／传送点](gameplay/ui/CLASSIC_HUD.md)；库存／NPC操作归对应专题 |

## 维护约定

技能分类与归属更新skills/README，公共规范／跨职业计算／状态更新COMMON；逐技能分派／专属Clt与Srv差异更新负责专题，复用同一技能只链接原页。MONSTERS登记怪物专用技能，COMPANIONS登记单位使用入口，ITEM_SKILLS登记物品来源与专用程序，SPECIAL保留未归档条目；原版依据和参考结果归所属职业、怪物、佣兵或物品专题，不复制运行流水。原模块调用证据更新REFERENCE_DESIGN；保存、人物投影、协议分别链接SAVES、CHARACTER、NETWORK；描述程序／公式／原版截图约束统一维护skills/DESCRIPTIONS。

怪物分类与归属更新monsters/README，公共规范／规则准备／生命周期更新COMMON；实际类型覆盖更新ACT页，专用策略更新BOSSES，rank／词缀更新ELITES，出生／房间更新POPULATION，原包／原图更新PRESENTATION，特殊身份更新SPECIAL。模块页只维护代码分工；具体技能、任务、佣兵与掉落链接原专题，各幕页维护所属身份的原版依据与缺口，不复制第二份完成清单。

物品分类更新items/README，COMMON维护公共规范，DATA／MODEL维护原表与身份，GENERATION／AFFIXES／EQUIPMENT分别维护生成／属性／贡献；操作、容器／消耗、镶嵌／耐久、掉落／地面／金币、方块／任务／经济更新对应专题。UI／原包分别归INVENTORY_UI／PRESENTATION，特殊条目归SPECIAL，原版依据归各负责页；充能／触发、任务阶段、佣兵与D2S仍链接原负责页，模块只维护代码入口。

任务通用边界更新SYSTEM，各幕规则／缺口／原版依据更新ACT专题，原编码与奖励位更新SAVES，客户端端口更新NPC_QUEST；不跨页面重复维护同一规则。全局能力概况与主要缺口更新BASELINE，实施顺序更新MULTIPLAYER，接口／所有权更新modules，原规则／表现限制更新对应专题；其他页面链接负责页面，不复制批次流水。根README维护项目展示、能力概况与启动入口，详细操作和部署链接开发指南。

保留可定位的原版依据与明确未知项，删掉被替代的入口和历史完成宣称；已导入定义／已显示面板不等于联机服务完成。资源、日志、截图、存档和运行包不纳入源码提交；临时产物允许清理，删除运行记录不改变已有实现或消除已知缺口。整理文档不删除代码兼容分支，代码变更仍按负责模块处理。

移动／删除文档须同步相对链接及源码引用；保存语义变化同步SAVES／格式／校验边界，旧档不静默迁移。默认不构建、运行或打包，不编写测试脚本、用例或专用程序；以当轮用户授权为准。
