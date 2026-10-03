# 怪物身份、生成与能力边界基线

更新：2026-10-03。本页记录当前代码分工与公共头边界；原 MPQ 规则、实现覆盖与行为缺口见 [怪物实施计划](../MONSTERS.md) 和 [怪物生成](../MONSTER_POPULATION.md)。怪物拆头／技能迁移批次曾通过 Windows Release 与冒烟；后续公共单位／记录绑定清理也已通过 Windows Release 与三职业简单冒烟，不代表怪物完整解耦或多人功能完成。

## 当前入口

| 入口 | 职责与依赖 |
| --- | --- |
| `gameplay/monsters/identity.hpp` | 原怪物／固定首领身份、生成键、级别、来源、分组、首领归属键和勇士变体资格；只引用等级枚举及标准库，不包含战斗词缀／光环 |
| `monsters/monster_spawn.hpp` | 单条生成指令：身份、实现类别、位置与技能位置；只引用身份、类别、坐标和 vector，不聚合 AI、法术、词缀或生成设置 |
| `monsters/population_settings.hpp`、`world/population.*` | 整局刷怪种子／难度、MPQ 与地形到生成计划；不拥有活动怪物，不分配实体 ID |
| `monsters/implementation.*`、`kind.hpp`、`rank.*` | 已实现类别注册、原授权替身判定、基础定义及等级显示名称；注册实现已从旧 `monster_spawn.cpp` 迁出 |
| `monsters/combat_values.hpp`／`ai_spec.hpp`／`animation_spec.hpp`／`ability_spec.hpp` | 普通战斗值、AI 参数、动作时序、飞弹／复活／巢／蛛网参数；相应内容适配只引用需要的值头 |
| `content/monsters/monster_catalog.*` | 当前 MPQ 怪物、固定首领、动作与能力目录；引用窄值头，不包含生成指令、活动词缀或完整身份 |
| `model/state.hpp` 的 `Enemy`、`monsters/unique_modifiers.hpp` | 活动怪物唯一拥有词缀；`Enemy::enchantment` 保存原属性、能力、光环与诅咒参数，`enchantmentData()` 仅在本次调用借用 |
| `monsters/reward.hpp`、`model/events.hpp`、`loot/loot.hpp` | 死亡奖励快照，只记录是否有词缀奖励，以及等级加成／经验倍率；没有活动词缀、光环或单位指针 |
| `monsters/*_ai.hpp`、`monster_wander.hpp` | 决策／移动函数声明，前置声明 `Enemy` 并引用所需参数／空间类型；完整活动状态依赖留在实现文件 |
| `session_monster_combat.cpp` | 内容基础属性缓存与活动词缀合成；内部传身份和显式词缀，诊断查询经 `EntityId` 查当前活动单位 |

`monster_spawn.hpp` 当前源码的直接包含者只有 `world/population.hpp` 和 `gameplay/model/state.hpp`。这是源码包含关系，不是编译任务数量；世界状态仍可将生成指令传递给很多实现文件。

原生怪物普通飞弹已实际接 `SkillRuntime::launchStraight`，Countess 火墙接共同火墙发射；词缀光环／诅咒／死亡副弹效果迁至技能模块，怪物文件保留原事件和调度。宠物创建／AI／寿命从旧技能召唤文件迁至 `monsters/companions.cpp`，技能只经端口提交请求。具体怪物动作和完整所有权尚未拆完，见 [通用技能基线](SKILL_RUNTIME.md)。

## 生命周期与奖励约束

生成计划不携带完整词缀。模拟器分配 ID 并建立 `Enemy` 后，会话沿原顺序初始化自然精英／固定首领及随从。怪物神殿沿原入口修改活动怪物词缀。基础生命、精英生命倍率、继承、动作事件及随机消耗的先后保持原实现。

活动词缀随整个 `Enemy` 保留在区域缓存，切区重访、死亡尸体和延迟词缀行为继续访问同一拥有者；不放到全局侧表，不增加共享指针。只复制身份不会复制光环或战斗能力。

死亡仍在原 `alive → dead` 结算位置发 `EnemyDied`，同时由 `monsterRewardModifiers` 按值复制原 `levelBonus`／`experienceFactor`。掉落请求、玩家经验及佣兵经验都使用该快照；没有词缀时仍执行各级别的原回退逻辑。随从掉落仍要求首领归属与奖励参数，召唤／转换的死亡资格分支保持原位置。奖励处理不能为了取倍率再查活动怪物，也不能把光环带入死亡事实。

公共战斗诊断改为 `GameSession::monsterCombatProfile(EntityId, RegionId)`，在当前活动单位中取身份／词缀；不存在的单位返回空。内部基础属性查询明确不传词缀，活动战斗查询明确传借用的词缀。它不是完整跨区单位注册服务。

姓名、修饰说明和调色板原消费者显式接收活动词缀，仍使用原名称种子与字段；场景资源头仅前置声明词缀类型，绘制实现仍读取活动怪物状态，尚未形成远端怪物表现投影。

当前公共 `CombatUnit` 已移除具体人物／佣兵／怪物指针，原受击、冷、AI和目录适配从内部 `RuntimeCombatUnit.records` 获取记录；身份和关系表另分头。当前清理已通过 Windows Release 与简单冒烟，入口与删除项见 [单位基线](UNITS.md)。

## 依赖路径与剩余限制

```text
人口计划 → MonsterSpawn → MonsterIdentity ＋ MonsterKind ＋ Vec
死亡事件／掉落请求 → MonsterIdentity ＋ MonsterRewardModifiers
内容怪物目录 → 战斗／AI／动作／能力值
活动怪物 Enemy → MonsterEnchantment → AuraDefinition
```

已消除生成头向身份、事件、掉落、目录和声明头传播完整词缀的路径。稳定的身份／生成值仍可被广泛共享，不以继续降低字面量引用数为目标。

尚未完成：`Enemy` 和 `WorldState` 的大公共头、`Simulation` 中具体怪物执行声明、完整单位存储与控制能力、多玩家施法来源、全部 AI／光环生命周期及远端可见投影。`EnemyDied` 仍是本地权威结算事实，包含掉落资格与玩家收益参数，不能直接作为联网广播消息。

此前怪物拆头源码随技能迁移完成 Windows Release 编译／链接；当前公共单位清理不使用此结论；原生骷髅法师、巨兽及亡灵战斗、骷髅／Hydra 召唤有代表性现场，范围见技能基线。未打包或新增测试程序，未重新统计头文件依赖闭包或测量增量编译耗时。旧 202 个编译任务等统计属于拆分前证据，不能作为当前引用量或性能改善值。D2S v96 不保存怪物运行态，本批未改存档语义、编码、规则指纹、MPQ 或用户档。
