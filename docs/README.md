# 文档目录

修改前阅读[项目基线](../BASELINE.md)与[协作约定](../AGENTS.md)，再读对应模块。Single Player／LAN嵌入宿主与独立PvPGN游戏服务复用权威内核，客户端共用原MCP／D2GS。客户端`dist/current`与服务端`dist/server`分别打包；当前均为Windows Debug产物，已有有限冒烟，完整覆盖仍未认证。包摘要与有限证据只由基线维护，不以旧冒烟认证新功能。

## 负责页面

| 问题 | 唯一负责页面 |
| --- | --- |
| 当前范围、已交付包与未入包源码 | [BASELINE](../BASELINE.md) |
| 客户端／服务端双向差距、完整实施顺序、复用边界、完成门槛、地图对照与本机参考服 | [内核与服务端总计划](architecture/MULTIPLAYER.md) |
| 公共函数提取、技能／怪物／NPC／任务复用边界与D2MOO依据 | [参考设计](architecture/REFERENCE_DESIGN.md) |
| 代码分工、实际链接与修改入口 | [架构](architecture/OVERVIEW.md)、[数据流](architecture/DATA_FLOW.md) |
| 原协议、副本、适配与实际有限原服证据 | [联网模块](modules/NETWORK.md) |
| 消息ID、编码格式、参数、分帧与预留状态 | [消息速查](modules/MESSAGES.md) |
| 领域处理器、收发所有权、扩展顺序与宿主管理边界 | [服务端协议](modules/SERVER_PROTOCOL.md) |
| 独立控制台D2GS、PvPGN后端配置、启动／关闭与恢复 | [PvPGN服务端](development/PVPGN_SERVER.md) |
| 内核子系统、状态所有权、命令／固定步／事务／可靠事件与空实现入口 | [内核子系统](modules/SERVER_SYSTEMS.md) |
| 原版来源、固定版本与许可 | [资料来源](resources/THIRD_PARTY.md)、[MPQ](resources/MPQ.md)、[原许可](licenses) |
| 启动、构建、打包与已有诊断 | [开发指南](development/BUILD_AND_RUN.md)、[调试管道](development/DEBUG_PIPE.md)、[崩溃记录](development/CRASH_REPORTS.md) |

已完成的结构改造内容并入架构／现有模块，不再维护第二份重构计划；已删除执行器的SESSION／UNITS／SKILL_RUNTIME／REWARDS说明不再作为模块入口。原版结构比较仍见[参考设计](architecture/REFERENCE_DESIGN.md)。

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
| 战斗 | [阵营](gameplay/combat/FACTIONS.md)、[数值显示](gameplay/combat/NUMBERS.md)、[攻击／弹体](gameplay/combat/ATTACKS.md)、[随机机制](gameplay/combat/RANDOMNESS.md) |
| 技能 | [全分类目录](gameplay/skills/README.md)、[公共规范／所有权／状态](gameplay/skills/COMMON.md)；目录包含七职业、通用、怪物／精英、佣兵／召唤物、物品技能与特殊／未归档条目 |
| 物品 | [全分类目录](gameplay/items/README.md)、[公共规范](gameplay/items/COMMON.md)；定义／生成／属性、装备／库存／容器／消耗、镶嵌／耐久、掉落／地面／金币、方块／任务／经济、原包表现／特殊身份及历史证据 |
| NPC | [交互／对白](gameplay/npc/INTERACTIONS.md)、[交易／服务](gameplay/npc/TRADE.md) |
| 任务 | [系统与协议／所有权](gameplay/quests/SYSTEM.md)、[第一幕执行与证据](gameplay/quests/ACT1.md)、[第二幕执行与证据](gameplay/quests/ACT2.md)、[第三至第五幕执行与缺口](gameplay/quests/ACT3_5.md) |
| 怪物 | [全分类目录](gameplay/monsters/README.md)、[公共规范](gameplay/monsters/COMMON.md)；五幕台账、精英／固定身份、首领、人口、原包表现及特殊／未归档单位 |
| 世界 | [五幕地图](gameplay/world/MAPS.md)、[物件](gameplay/world/OBJECTS.md)、[自动地图](gameplay/world/AUTOMAP.md)、[照明](gameplay/world/LIGHTING.md) |
| UI | [HUD／技能菜单／传送点](gameplay/ui/CLASSIC_HUD.md)；库存／NPC操作归对应专题 |

## 维护约定

技能分类与归属更新skills/README，公共规范／跨职业计算／状态更新COMMON；逐技能分派／专属Clt与Srv差异更新负责专题，复用同一技能只链接原页。MONSTERS登记怪物专用技能，COMPANIONS登记单位使用入口，ITEM_SKILLS登记物品来源与专用程序，SPECIAL保留未归档条目；详细运行证据链接所属职业或已有怪物／佣兵专题或[物品证据](gameplay/items/EVIDENCE.md)，不复制。原模块调用证据更新REFERENCE_DESIGN；保存、树／提示、协议分别链接SAVES、CHARACTER、NETWORK。

怪物分类与归属更新monsters/README，公共规范／规则准备／生命周期更新COMMON；实际类型覆盖更新ACT页，专用策略更新BOSSES，rank／词缀更新ELITES，出生／房间更新POPULATION，原包／原图更新PRESENTATION，特殊身份更新SPECIAL。模块页只维护代码分工；具体技能、任务、佣兵与掉落链接原专题，运行证据归所属幕，当前第一幕证据集中ACT1，不复制第二份完成清单。

物品分类更新items/README，COMMON维护公共规范，DATA／MODEL维护原表与身份，GENERATION／AFFIXES／EQUIPMENT分别维护生成／属性／贡献；操作、容器／消耗、镶嵌／耐久、掉落／地面／金币、方块／任务／经济更新对应专题。UI／原包分别归INVENTORY_UI／PRESENTATION，特殊条目归SPECIAL，物品历史记录归EVIDENCE；充能／触发、任务阶段、佣兵与D2S仍链接原负责页，模块只维护代码入口。

任务通用边界更新SYSTEM，各幕规则／缺口／证据更新ACT专题，原编码与奖励位更新SAVES，客户端端口更新NPC_QUEST；不跨页面重复维护同一规则。全局交付更新BASELINE，实施顺序更新MULTIPLAYER，接口／所有权更新modules，原规则／表现限制更新对应专题；其他页面链接负责页面，不复制批次流水。README仅介绍启动／操作。

保留可追溯原版证据与明确未知项，删掉被替代的入口、兼容分支和历史完成宣称；已导入定义／已显示面板不等于联机服务完成。最新未构建源码不能沿用旧运行结论。资源、日志、截图与旧包留忽略目录，历史修改由Git追溯。

移动／删除文档须同步相对链接及源码引用；保存语义变化同步SAVES／格式／指纹，旧档不静默迁移。默认不构建、运行或打包，不编写测试脚本、用例或专用程序；以当轮用户授权为准。
