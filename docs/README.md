# 文档目录

开始修改前读 [项目基线](../BASELINE.md) 和 [协作约定](../AGENTS.md)，再选择下面的领域入口。联网主流程源码已补齐，新增部分未构建；旧包已有有限双账号协议冒烟，世界副本尚未实现。源码／包差异见基线。

## 目录与职责

```text
README.md                  项目介绍、启动与操作
BASELINE.md                当前全局范围、源码与运行包差异、交接入口
docs/
  architecture/            依赖、所有权、数据流、剩余改造与设计证据
  modules/                 当前代码接口、文件分工、兼容边界与模块验证
  gameplay/
    characters/            成长属性、佣兵、玩家死亡与尸体
    combat/                阵营、数值、公共攻击与随机规则
    skills/                公共效果与职业技能
    items/                 原表、实例、库存操作与支持范围
    npc/                   对话交互与交易
    quests/                通用任务系统与各幕规则
    world/                 地图、怪物、物件、探索与照明
    ui/                    经典 HUD 与面板操作
  development/             构建运行、调试管道与崩溃记录
  resources/               原始 MPQ 清单、参考来源与许可归属
  licenses/                第三方原始许可文本
```

文档分类按要回答的问题划分，不要求一比一复制源码目录：模块基线回答“由谁拥有、经什么接口调用”，玩法专题回答“采用什么规则、实现到哪里”。实际库边界以 CMake 为准。

## 架构与后续计划

| 文档 | 负责内容 |
| --- | --- |
| [代码结构](architecture/OVERVIEW.md) | 目录／CMake 分工、依赖图、所有权及修改落点 |
| [数据流](architecture/DATA_FLOW.md) | MPQ、区域、单位、技能、库存、投影与保存生命周期 |
| [剩余解耦工作](architecture/REFACTOR_PLAN.md) | 当前代码的五类缺口、执行顺序、入口与完成条件 |
| [参考项目设计](architecture/REFERENCE_DESIGN.md) | 组合／领域服务、主动技能、被动与光环的参考证据 |
| [D2GS 接入计划](architecture/MULTIPLAYER.md) | 现有参考服部署入口，地图／世界副本的下一步与验收条件 |

## 模块边界

| 文档 | 负责内容 |
| --- | --- |
| [客户端](modules/CLIENT.md) | 值契约、本地适配、已迁移 UI 与剩余兼容访问 |
| [联网模块](modules/NETWORK.md) | 登录／注册、服务器角色、创建／加入房间、配置／协议边界与准确验证范围 |
| [会话](modules/SESSION.md) | 权威宿主、命令与固定步、生命周期与组装 |
| [角色](modules/CHARACTER.md) | 保存值、成长／学习、活角色组合及角色投影 |
| [公共单位](modules/UNITS.md) | 身份、能力访问、内部记录绑定与控制策略 |
| [怪物](modules/MONSTERS.md) | 生成头、真实身份、能力及家族行为的代码边界 |
| [技能运行](modules/SKILL_RUNTIME.md) | 来源参数、求值、执行处理器、世界／武器端口与适配 |
| [库存／装备](modules/INVENTORY.md) | 唯一库存事务、装备贡献及已有技能来源 |
| [地图／区域](modules/MAP.md) | 地形与活导航、区域仓储、投影与资源接口 |
| [NPC／任务](modules/NPC_QUEST.md) | 交互权限、对白／服务投影、任务计划及奖励协调 |
| [死亡奖励](modules/REWARDS.md) | 怪物死亡、首杀任务、经验与掉落结算顺序 |
| [存档](modules/SAVES.md) | 原 D2S v96 字段、编码／校验、保存恢复与拒绝边界 |

## 玩法规则

| 领域 | 文档与职责 |
| --- | --- |
| 角色 | [属性与成长](gameplay/characters/ATTRIBUTES.md)、[佣兵](gameplay/characters/HIRELINGS.md)、[玩家死亡与尸体](gameplay/characters/PLAYER_DEATH.md) |
| 战斗 | [阵营与归属](gameplay/combat/FACTIONS.md)、[战斗数值](gameplay/combat/NUMBERS.md)、[公共攻击／导弹](gameplay/combat/ATTACKS.md)、[随机流](gameplay/combat/RANDOMNESS.md) |
| 技能 | [公共技能／状态](gameplay/skills/COMMON.md)、[女巫](gameplay/skills/SORCERESS.md)、[圣骑士](gameplay/skills/PALADIN.md)、[死灵法师](gameplay/skills/NECROMANCER.md)、[亚马逊](gameplay/skills/AMAZON.md)；没有专题的职业不据此推断已有完整实现 |
| 物品数据 | [原表与掉落](gameplay/items/DATA.md)、[实例／容器模型](gameplay/items/MODEL.md)、[效果支持与缺口](gameplay/items/SUPPORT.md) |
| 库存操作 | [包裹](gameplay/items/INVENTORY_UI.md)、[腰带／消耗品](gameplay/items/BELT_AND_CONSUMABLES.md)、[私人储物箱](gameplay/items/STORAGE.md)、[方块／堆叠／金币](gameplay/items/CUBE_AND_GOLD.md) |
| NPC | [对白／交互／路径](gameplay/npc/INTERACTIONS.md)、[交易／赌博／修理](gameplay/npc/TRADE.md)；任务推进归任务专题，佣兵规则归角色专题 |
| 任务 | [通用系统与扩展](gameplay/quests/SYSTEM.md)、[第一幕](gameplay/quests/ACT1.md)、[第二幕](gameplay/quests/ACT2.md)、[第三至第五幕](gameplay/quests/ACT3_5.md) |
| 世界 | [五幕地图](gameplay/world/MAPS.md)、[交互物件／祭坛](gameplay/world/OBJECTS.md)、[人口计划／房间激活](gameplay/world/POPULATION.md)、[怪物行为](gameplay/world/MONSTERS.md)、[自动地图](gameplay/world/AUTOMAP.md)、[照明](gameplay/world/LIGHTING.md) |
| 界面 | [经典 HUD／技能选择／传送点](gameplay/ui/CLASSIC_HUD.md)；专属库存和 NPC 面板操作随对应玩法专题维护 |

## 开发与资料

| 文档 | 负责内容 |
| --- | --- |
| [构建、运行与打包](development/BUILD_AND_RUN.md) | Windows／Linux 说明、资源定位、角色入口及固定分发目录 |
| [调试管道](development/DEBUG_PIPE.md) | 本机协议、已有命令和操作限制 |
| [崩溃记录](development/CRASH_REPORTS.md) | 本机报告、转储文件与实现限制 |
| [MPQ](resources/MPQ.md) | 当前原包、摘要、覆盖与查看方法 |
| [资料来源](resources/THIRD_PARTY.md) | 各参考仓库用途、固定提交、借鉴边界和素材权利 |
| [原始许可文本](licenses) | 保留原始第三方声明，不代替来源说明 |

## 维护规则

- 全局交付状态只更新 BASELINE；README 保持启动和操作入口，不复制模块实施记录。
- 接口／所有权变化更新 modules，规则与支持范围变化更新所属玩法专题；跨领域文档链接到负责人，不复制整段结论。
- 实施顺序只维护改造方案。完成后改写当前结论，删去被替代的旧步骤；规划目录／接口必须明确未实施。
- 已有验证只记录实际覆盖、批次和限制，后续未经构建的源码不能沿用旧结论。过程日志、截图和资源留在不提交目录。
- 存档语义以 SAVES 为准；运行指纹与 D2S 字段区分。旧内部版本流水不作为当前格式说明。
- 原规则先查本地 reference 和当前 MPQ；证据与许可归资料来源，当前内容仍由运行时 MPQ 决定。
- 新建文档时先确认现有页面是否能承接，优先更新已有基线；移动后同步 Markdown 链接和源码中的文档引用。
