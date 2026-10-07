# 角色、怪物与技能的参考设计

依据：本地固定版本 `reference/` 源码阅读。本文比较结构与调用边界，不认证原版参数、功能完整度或运行表现；未构建、运行检查或修改玩法。当前产品不实现本地权威宿主；以下只保留参考结构与证据。原规则与参数继续以当前 MPQ 和已核实证据为准。

## 1. 参考价值与限制

已核对本地提交：D2MOO `5596f5c`、Diablerie `9e42ef2`、OpenDiablo2 `7f92c57`、DGEngine `ae6dcab`、OpenD2 `0578244`。来源和许可见 [THIRD_PARTY.md](../resources/THIRD_PARTY.md)。只引用本地入口，不把参考代码、其附带数据或资源复制进源码。

| 项目 | 观察到的结构 | 可借鉴之处 | 不宜据此推断 |
| --- | --- | --- | --- |
| D2MOO | 通用单位结构、玩家／怪物专属数据、技能函数表、单位属性列表和周期事件 | 原执行机制、技能来源、被动和光环的生命周期 | 不提供现代 C++ 封装或编译隔离；仍有全局表、单位反向查询和类型分支 |
| Diablerie | `Entity → Unit`；玩家聚合多个组件，怪物使用独立控制器；技能执行接收 `Unit` | 共用单位能力，控制器与单位分离，事件通知表现系统 | Unity 单位仍混合动画、移动、战斗；本地技能实现简化，未找到可采用的完整被动／光环执行链 |
| DGEngine | `LevelObject → PlayerBase → Player`；组合库存／属性／法术；`Spell` 与 `SpellInstance` 分离 | 定义和实例分离，通用查询接口代替具体玩家类型 | 偏 Diablo I／通用引擎；查询和纹理耦合仍存在，不能当作 D2 光环实现 |
| OpenDiablo2 | 空间实体通过接口和嵌入复用；`HeroState`、`HeroSkill` 分离；技能按 ID 恢复定义 | 小接口、角色成长状态、保存技能 ID 与点数 | `MapEntity` 是含绘制的地图接口；本地施法链仍绑定玩家，网络分支转发施法包，不是完整权威战斗系统 |
| OpenD2 | 已查看到技能表结构、被动／光环字段和包说明 | 字段布局交叉证据 | 未找到足以采用的角色／怪物／被动／光环完整执行设计 |

`dgengine-core` 提供通用 `Queryable` 等基础接口；未找到其独立的 D2 技能／被动／光环执行模块。`d2s-validation` 是存档验证依赖，不作为玩法架构参考。

## 2. 角色与怪物：共用单位，分开控制来源

### D2MOO：通用结构加专属数据

[`D2UnitStrc`](../../reference/d2moo/source/D2Common/include/Units/Units.h) 用单位类型区分玩家、怪物、物件、飞弹、物品和地砖；通用部分携带身份、路径、属性列表、技能列表、主人等信息，专属部分通过玩家／怪物等数据指针表达。它是带类型标记的组合结构，不是角色和怪物继承同一个 C++ 虚基类。

[`AITACTICS_UseSkill`](../../reference/d2moo/source/D2Game/src/AI/AiTactics.cpp) 将怪物选定的技能和目标交给动作流程；公共技能执行再处理通用单位。值得参考的是“控制决策与执行分离”，而非照搬整个单位大结构及原内存布局。

### Diablerie：公共 Unit 与独立控制器

[`Entity`](../../reference/diablerie/Assets/Scripts/Diablerie/Engine/Entities/Entity.cs) 是 Unity 抽象基类，[`Unit`](../../reference/diablerie/Assets/Scripts/Diablerie/Engine/Entities/Unit.cs) 提供移动、生命、阵营、动作和 `UseSkill`。[`Player`](../../reference/diablerie/Assets/Scripts/Diablerie/Engine/Player.cs) 聚合 Unit、Equipment、Inventory、CharStat；它本身不是 Unit 子类。[`MonsterController`](../../reference/diablerie/Assets/Scripts/Diablerie/Game/AI/MonsterController.cs) 获取 Unit，决定目标并调用相同的 `UseSkill`。

[`Events`](../../reference/diablerie/Assets/Scripts/Diablerie/Engine/Events.cs) 发布单位初始化、技能开始和死亡事件，声音等系统订阅事件。此处体现组件组合与观察者机制；不是完整 ECS，也不是每种怪物各建一个子类。

## 3. 主动技能：共享定义与行为处理器

D2MOO 的 [`SkillStartFunc`／`SkillDoFunc`](../../reference/d2moo/source/D2Game/include/SKILLS/Skills.h) 接收游戏、通用单位、技能 ID 和等级。[`Skills.cpp`](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp) 按原表的起手／执行函数号选择处理器，多个技能可共用一个函数。这可以理解为数据驱动的行为分派／策略注册，不需要“一项技能一个派生类”。

[`D2SkillStrc`](../../reference/d2moo/source/D2Common/include/D2Skills.h) 同时记录定义指针、等级、运行参数、来源 `nOwnerGUID` 和充能。`D2Common_10954` 在 [`D2Skills.cpp`](../../reference/d2moo/source/D2Common/src/D2Skills.cpp) 中按来源维护技能实例；技能不是只能来自角色学习。注意：执行和公式仍查询单位属性，不能说它实现了“调用方传入全部加成”的隔离。

Diablerie 的 [`SkillInfo.Do(Unit, Unit, target)`](../../reference/diablerie/Assets/Scripts/Diablerie/Engine/Datasheets/SkillInfo.cs) 也是公共单位入口，但导入记录兼有 UI 信息和执行分支，并直接访问装备、声音、世界生成；不适合整体搬入纯玩法库。

DGEngine 的 [`Spell`](../../reference/dgengine/src/Game/Spell/Spell.h) 保存定义、公式与资源，[`SpellInstance`](../../reference/dgengine/src/Game/Spell/SpellInstance.h) 保存定义引用、等级和通用 `Queryable` 来源，[`PlayerSpells`](../../reference/dgengine/src/Game/Player/PlayerSpells.h) 管理拥有／选中的实例。定义／实例分离有参考价值；通过来源反向查询公式参数和把纹理放入定义，不符合当前纯显示／数据接口边界。

## 4. 被动：来源明确的属性贡献与能力

D2MOO 的 [`SKILLS_RefreshSkill`](../../reference/d2moo/source/D2Common/src/D2Skills.cpp) 查找被动状态对应的属性列表：没有有效技能或等级时移除；有等级时创建／更新 `PassiveStat` 与计算结果，记录技能及等级。它还处理光环状态对被动贡献的抑制。

[`D2GAME_RefreshPassiveSkills`](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp) 遍历单位技能刷新被动；[`ItemMode.cpp`](../../reference/d2moo/source/D2Game/src/ITEMS/ItemMode.cpp) 中存在装备相关刷新调用。可借鉴的是随来源变化刷新贡献，而不是每个固定步重新解释所有被动。

[`D2StatListStrc`](../../reference/d2moo/source/D2Common/include/D2StatList.h) 表达来源、状态、技能／等级、属性和移除回调。公共技能代码另有单位事件回调表，供护盾、反伤、装备触发等响应使用。属性贡献与事件反应需要分开表达，不能把所有效果都简化成属性相加。

## 5. 光环：发射实例、周期筛选、目标效果

D2MOO 的 [`SKILLS_SrvDo065_BasicAura`](../../reference/d2moo/source/D2Game/src/SKILLS/SkillPal.cpp) 分开本人状态与目标状态，计算范围和属性，再通过通用筛选回调处理目标；目标获得状态属性列表。`SKILLS_CurseStateCallback_BasicAura` 负责状态移除时的清理和被动刷新。

[`Skills.cpp`](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp) 的周期技能流程处理当前技能光环；`D2GAME_MONSTERS_AiFunction10` 查询装备 `STAT_ITEM_AURA`，取得等级后调用同一技能执行入口。这证明光环执行机制能接不同来源，不只由圣骑士右键控制。

可以理解为：发射者拥有光环实例，周期查询合格目标，再给目标刷新有期限的效果；停止刷新后，目标效果按规则到期。该描述来自上述普通光环路径，不代表所有光环都只有属性效果：原代码还分出伤害、尸体处理等路径。

## 当前采用边界

D2X消费原服只读副本，把控制意图、协议、MPQ定义和表现分开；不照搬参考宿主、完整单位大结构或技能执行器。纯显示函数使用显式已知输入，未知状态保留未知。当前文件归属及依赖见[架构](OVERVIEW.md)，未完成功能见[联机计划](MULTIPLAYER.md)。
