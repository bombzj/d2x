# 文档目录

修改前阅读[项目基线](../BASELINE.md)与[协作约定](../AGENTS.md)，再读对应模块。当前源码以原MCP／D2GS字节协议统一自研Single Player和原服客户端，并接服务端角色／D2S；Windows Release已构建打包，有限单机冒烟与覆盖限制见基线。历史离线行为或旧冒烟不作为新内核认证。

## 负责页面

| 问题 | 唯一负责页面 |
| --- | --- |
| 当前范围、已交付包与未入包源码 | [BASELINE](../BASELINE.md) |
| 客户端／服务端双向差距、完整实施顺序、复用边界、完成门槛、地图对照与本机参考服 | [内核与服务端总计划](architecture/MULTIPLAYER.md) |
| 公共函数提取、技能／怪物／NPC／任务复用边界与D2MOO依据 | [参考设计](architecture/REFERENCE_DESIGN.md) |
| 代码分工、实际链接与修改入口 | [架构](architecture/OVERVIEW.md)、[数据流](architecture/DATA_FLOW.md) |
| 原协议、副本、适配与实际有限原服证据 | [联网模块](modules/NETWORK.md) |
| 自研消息目录、领域处理器、stub状态与宿主管理边界 | [服务端协议](modules/SERVER_PROTOCOL.md) |
| 内核子系统、状态所有权、命令／固定步／事务／可靠事件与空实现入口 | [内核子系统](modules/SERVER_SYSTEMS.md) |
| 原版来源、固定版本与许可 | [资料来源](resources/THIRD_PARTY.md)、[MPQ](resources/MPQ.md)、[原许可](licenses) |
| 启动、构建、打包与已有诊断 | [开发指南](development/BUILD_AND_RUN.md)、[调试管道](development/DEBUG_PIPE.md)、[崩溃记录](development/CRASH_REPORTS.md) |

已完成的结构改造内容并入架构／现有模块，不再维护第二份重构计划；已删除执行器的SESSION／UNITS／SKILL_RUNTIME／REWARDS说明不再作为模块入口。原版结构比较仍见[参考设计](architecture/REFERENCE_DESIGN.md)。

## 当前模块

| 模块 | 内容 |
| --- | --- |
| [客户端](modules/CLIENT.md) | I*Client值契约、消息连接、公共消费者与两类后端适配 |
| [人物](modules/CHARACTER.md) | 原属性／技能、纯提示投影、请求／保存值边界 |
| [库存](modules/INVENTORY.md) | 原物品、面板／预览、异步组合与服务限制 |
| [地图](modules/MAP.md) | 原房间／地形、碰撞、地图UI与本局探索 |
| [怪物](modules/MONSTERS.md) | 第一幕权威AI／精英／首领、原协议表现、公共函数与人口准备 |
| [NPC／任务](modules/NPC_QUEST.md) | 本人任务／对白接口、UI生命周期、服务端交谈与旅行边界 |
| [存档／偏好](modules/SAVES.md) | 独立D2S v96编码／拒绝、原服保存、客户端偏好／凭据 |

## 规则与表现专题

| 领域 | 入口 |
| --- | --- |
| 角色 | [属性／成长](gameplay/characters/ATTRIBUTES.md)、[佣兵](gameplay/characters/HIRELINGS.md)、[死亡／尸体](gameplay/characters/PLAYER_DEATH.md) |
| 战斗 | [阵营](gameplay/combat/FACTIONS.md)、[数值显示](gameplay/combat/NUMBERS.md)、[攻击／弹体](gameplay/combat/ATTACKS.md)、[随机机制](gameplay/combat/RANDOMNESS.md) |
| 技能 | [公共入口／规范](gameplay/skills/COMMON.md)、[通用攻击／卷轴／书本](gameplay/skills/GENERAL.md)、[女巫](gameplay/skills/SORCERESS.md)、[亚马逊](gameplay/skills/AMAZON.md)、[死灵法师](gameplay/skills/NECROMANCER.md)、[圣骑士](gameplay/skills/PALADIN.md) |
| 物品 | [数据](gameplay/items/DATA.md)、[模型](gameplay/items/MODEL.md)、[支持／缺口](gameplay/items/SUPPORT.md)、[包裹](gameplay/items/INVENTORY_UI.md)、[腰带／使用](gameplay/items/BELT_AND_CONSUMABLES.md)、[箱子](gameplay/items/STORAGE.md)、[方块／金币](gameplay/items/CUBE_AND_GOLD.md) |
| NPC | [交互／对白](gameplay/npc/INTERACTIONS.md)、[交易／服务](gameplay/npc/TRADE.md) |
| 任务 | [系统与协议／所有权](gameplay/quests/SYSTEM.md)、[第一幕执行与证据](gameplay/quests/ACT1.md)、[第二幕身份与缺口](gameplay/quests/ACT2.md)、[第三至第五幕身份与缺口](gameplay/quests/ACT3_5.md) |
| 世界 | [五幕地图](gameplay/world/MAPS.md)、[物件](gameplay/world/OBJECTS.md)、[人口报告](gameplay/world/POPULATION.md)、[怪物表现](gameplay/world/MONSTERS.md)、[自动地图](gameplay/world/AUTOMAP.md)、[照明](gameplay/world/LIGHTING.md) |
| UI | [HUD／技能菜单／传送点](gameplay/ui/CLASSIC_HUD.md)；库存／NPC操作归对应专题 |

## 维护约定

全局交付更新BASELINE，实施顺序更新MULTIPLAYER，接口／所有权更新modules，原规则／表现限制更新对应专题；其他页面链接负责页面，不复制批次流水。README仅介绍启动／操作。

保留可追溯原版证据与明确未知项，删掉被替代的入口、兼容分支和历史完成宣称；已导入定义／已显示面板不等于联机服务完成。最新未构建源码不能沿用旧运行结论。资源、日志、截图与旧包留忽略目录，历史修改由Git追溯。

移动／删除文档须同步相对链接及源码引用；保存语义变化同步SAVES／格式／指纹，旧档不静默迁移。默认不构建、运行或打包，不编写测试脚本、用例或专用程序；以当轮用户授权为准。
