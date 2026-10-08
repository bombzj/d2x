# 公共规则、领域执行与表现的参考设计

依据：本地固定版本 `reference/` 源码阅读。更新：2026-10-08。本文维护公共函数的提取依据、具体边界和后续迁移方法。当前产品已有自研权威宿主与唯一原协议客户端；已实施范围见[内核子系统](../modules/SERVER_SYSTEMS.md)。以下明确区分已存在的代码、参考证据和后续设计，不据源码阅读认证运行表现。原规则与参数继续以当前 MPQ 和已核实证据为准。

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

DGEngine 的 [`Spell`](../../reference/dgengine/src/Game/Spell/Spell.h) 保存定义、公式与资源，[`SpellInstance`](../../reference/dgengine/src/Game/Spell/SpellInstance.h) 保存定义引用、等级和通用 `Queryable` 来源，[`PlayerSpells`](../../reference/dgengine/src/Game/Player/PlayerSpells.h) 管理拥有／选中的实例。定义／实例分离有参考价值；通过来源反向查询公式参数和把纹理放入定义，不符合当前纯玩法与表现隔离边界。

## 4. 被动：来源明确的属性贡献与能力

D2MOO 的 [`SKILLS_RefreshSkill`](../../reference/d2moo/source/D2Common/src/D2Skills.cpp) 查找被动状态对应的属性列表：没有有效技能或等级时移除；有等级时创建／更新 `PassiveStat` 与计算结果，记录技能及等级。它还处理光环状态对被动贡献的抑制。

[`D2GAME_RefreshPassiveSkills`](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp) 遍历单位技能刷新被动；[`ItemMode.cpp`](../../reference/d2moo/source/D2Game/src/ITEMS/ItemMode.cpp) 中存在装备相关刷新调用。可借鉴的是随来源变化刷新贡献，而不是每个固定步重新解释所有被动。

[`D2StatListStrc`](../../reference/d2moo/source/D2Common/include/D2StatList.h) 表达来源、状态、技能／等级、属性和移除回调。公共技能代码另有单位事件回调表，供护盾、反伤、装备触发等响应使用。属性贡献与事件反应需要分开表达，不能把所有效果都简化成属性相加。

## 5. 光环：发射实例、周期筛选、目标效果

D2MOO 的 [`SKILLS_SrvDo065_BasicAura`](../../reference/d2moo/source/D2Game/src/SKILLS/SkillPal.cpp) 分开本人状态与目标状态，计算范围和属性，再通过通用筛选回调处理目标；目标获得状态属性列表。`SKILLS_CurseStateCallback_BasicAura` 负责状态移除时的清理和被动刷新。

[`Skills.cpp`](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp) 的周期技能流程处理当前技能光环；`D2GAME_MONSTERS_AiFunction10` 查询装备 `STAT_ITEM_AURA`，取得等级后调用同一技能执行入口。这证明光环执行机制能接不同来源，不只由圣骑士右键控制。

可以理解为：发射者拥有光环实例，周期查询合格目标，再给目标刷新有期限的效果；停止刷新后，目标效果按规则到期。该描述来自上述普通光环路径，不代表所有光环都只有属性效果：原代码还分出伤害、尸体处理等路径。

## 6. D2MOO的公共层究竟共用什么

D2Common包含单位、属性、技能公式、物品计算、路径／碰撞和任务记录等基础能力；D2Game拥有世界调度、AI、技能执行、NPC交易和任务事件。D2Common也有可变状态和分配函数，不等于一个现代纯函数库；不能整体搬进两端。导出符号证明其公共接口存在，不单独证明1.13c客户端某个调用点使用它。本地快照缺完整D2Client，具体客户端流程仍以已有原包消费者、当前MPQ和已核实原版证据为准。

| 本地可查证据 | 能证明的边界 | 本项目采用方式 |
| --- | --- | --- |
| [D2Common/D2Skills.h](../../reference/d2moo/source/D2Common/include/D2Skills.h)：EvaluateSkillFormula、GetManaCosts、GetMin/MaxElemDamage | 公共技能数值、等级与来源基础；完整SrvDo不在这些函数中 | 继续使用现有resolve／damage_curve／rank_sources；显式输入基础／有效等级、装备和支配 |
| [D2Game/Skills.cpp](../../reference/d2moo/source/D2Game/src/SKILLS/Skills.cpp)、[MissMode.cpp](../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp) | 技能和弹体按原表函数号分派，多个技能复用执行族；冰封球散射／结束爆发有服务端回调 | 按行为族拆纯计算和领域执行，不按每个技能新建一个类，也不把整个SrvDo放到客户端 |
| [D2Common/PathMisc.cpp](../../reference/d2moo/source/D2Common/src/Path/PathMisc.cpp)、[Units](../../reference/d2moo/source/D2Common/include/Units/Units.h) | 单位尺寸、速度、路径属于公共基础 | 无副作用的几何、固定点换算共用；路线、阻挡单位与最终位置各端分别持有 |
| [D2Game/AiThink.cpp](../../reference/d2moo/source/D2Game/src/AI/AiThink.cpp) | 怪物家族决策由服务端调用，不是客户端AI副本 | 旧单机家族规则迁为纯决策，server/ai提供目标／时钟／随机状态并提交动作 |
| [D2Common/D2Items.h](../../reference/d2moo/source/D2Common/include/D2Items.h)：GetTransactionCost；[SUnitNpc.cpp](../../reference/d2moo/source/D2Game/src/UNIT/SUnitNpc.cpp) | 价格有公共计算；交易先验证NPC交互、物品归属，再由服务端执行 | 客户端可用已知值算报价，服务端重新算并原子成交；相同报价不等于授权成交 |
| [D2Common/D2QuestRecord.h](../../reference/d2moo/source/D2Common/include/D2QuestRecord.h)、[A1Q1.cpp](../../reference/d2moo/source/D2Game/src/QUESTS/ACT1/A1Q1.cpp) | 任务旗标读写／编码是基础能力；杀怪、换区、NPC事件和奖励回调在D2Game | 公共身份／旗标解释和纯资格条件可共用；任务推进和领奖只能由服务端提交 |

原版数据及版本差异优先于参考实现。已知NPC消息menu=0／2的1.13c证据与参考快照差异见[资料来源](../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。没有源码的部分不以简化开源客户端代替原版结论；先标明缺证据，再做最小范围核对。

## 7. 本项目的提取单位与所有权

复用分两类：两端都能消费的确定性计算，以及多个服务端领域／怪物／技能族之间的计算复用。后一类不需要为了“公用”而调用到客户端。禁止客户端通过公共函数拿到权威玩家存档、怪物目标列表、隐藏随机或任务世界状态。

- **content准备**：当前MPQ解析一次，产出带原身份的不可变规则值。资源路径／声音／图像由表现适配器消费，内核只拿对应数值。Srv／Clt字段分别保留，值相等须有数据证据，不能强制合并成一个默认值。
- **gameplay纯计算**：输入规则值、明确的属性／等级、帧相位、坐标和必要局部状态；输出数值、下一状态或动作描述。没有GameInstance／GameSession／ClassicData反向查找，不访问socket、MPQ、GPU、存档或隐式全局随机。
- **server执行**：ai／skills／missiles／effects／combat／npc／quests等领域拥有运行状态，校验身份、区域代次、资格和revision，经transactions／领域提交点修改权威值并发布原包事实。
- **client表现**：原服和自研共用一个原包消费者。公共计算只作用于已知副本和视觉对象；状态未知保留未知。显示接触、施法动画、报价、按钮可用性均不能直接扣血、改背包或发奖励。

函数形式按问题选择：数值公式返回值；多步骤行为用“小状态＋输入 → 下一状态＋动作描述”；目标集合筛选接收显式只读候选；几何返回位置／交点。随机计算显式接收并返回种子，调用者成功提交后才安装下一种子。不要把任意世界回调集合塞入一个公共SkillContext，重新形成隐式GameSession。

公共代码相同也不会自动同步两端世界：延迟、丢弃的视觉对象、未知目标、包时间和随机信息仍可能不同。服务器保存真实动作／命中；客户端只做可见预测并接受原位置／状态校正。没有原协议支持的隐藏随机不额外通过私有包传输，也不根据本地内容伪造服务器结果。

## 8. 各类技能怎样拆

以下是恢复旧单机能力时的分工，除明确标注的部分外不是已实现清单。

| 行为族／例子 | 适合提取的纯计算 | 服务端独有执行 | 客户端可消费部分 |
| --- | --- | --- | --- |
| 普通近战、连击、武器技能 | 攻速／动作帧、武器范围、命中与伤害公式、连击时序 | 攻击合法性、真实目标、命中随机、耐久、伤害和中断 | 已知动作时长与表现路径，不判定命中；近战纯计算与服务端skill 0已有 |
| 火弹／火球、冰弹、骨矛等直线弹体 | 速度／寿命、分段推进、墙面和单位交点、范围几何 | 弹体身份、敌我、穿透次数、真实碰撞、伤害和资源 | 同参数下的飞行／接触图像；火弹／火球权威切片及共用墙面裁剪已有 |
| 冰封球／多重散射／nova | 频率判定、64方向、每次数量、方向推进、结束爆发、整数转向 | 可伤害子弹体的创建、死亡／断线处理、碰撞结算 | 对应Clt参数生成视觉子弹体；环形／remaining调度已两端共用，冰封球服务端已接 |
| 充能弹、引导／追踪弹体 | 确定性路径、转弯／步进、已知种子下的局部序列 | 跟踪目标选择与失效、服务器随机、实际重定向 | 原包足够描述的路径；已有chargedBoltPath，不声称两端目标已同步 |
| 暴风雪、陨石、火墙、毒云 | 持续时间、周期、落点／分布规则、地面覆盖几何 | 真实区域、周期伤害／NextHit去重、隐藏随机、跨区清理 | 特效时间线与可见落点；需要确认Srv／Clt周期和随机起点 |
| 闪电／连锁、射线 | 射线／链段几何、伤害曲线、跳跃上限 | 链的真实候选、排序、再次命中限制 | 原消息给出的端点及链段；不在客户端扫描可见怪物重建权威链 |
| 冰甲／能量盾、被动、附魔 | 属性贡献、吸收公式、互斥／叠加条件 | 效果来源、到期、耗蓝、受击事件反应、贡献安装／撤销 | 原状态和已知数值下的说明／覆盖层；不是客户端刷新真实被动 |
| 光环／诅咒 | 范围、目标条件、等级比较与属性贡献 | 发射实例、刷新／到期、完整候选、阵营与状态提交 | 服务器已同步状态的显示；同一个行为可由玩家、怪物、装备来源调用 |
| 骷髅／石魔／伙伴召唤 | 等级曲线、数量上限、出生候选几何、继承属性 | 尸体／物品消耗、所有者与名额、实体准入、AI／同行与存档 | 原指派和动作；客户端不能自行补出生实体 |
| 传送／冲锋／击退 | 位移路径、到达阈值、碰撞与速度公式 | 目的地许可、资源、实际位置、伙伴与区域生命周期 | 原动作和位置校正；同区Teleport已接，同行者／跨区仍待实现 |

冰封球两端必须显式传remaining；已按原版修正公共调度，Clt／Srv参数仍独立。末次爆发与碰墙销毁也应分事件。共用函数不等于可以把两端Clt／Srv执行顺序互换。

## 9. NPC、怪物、任务与物品怎样拆

| 领域 | 公共或可提取部分 | 必须保持服务端拥有 | 当前入口／下一步 |
| --- | --- | --- | --- |
| 怪物 | 原身份、尺寸、动作、固定点速度、距离；家族决策可在服务端内部复用 | 人口、目标、仇恨、随机流、AI等待／冲锋／逃跑、攻击、复活、死亡奖励 | 旧master各家族`*_ai`优先；当前`melee_decision`迁入三个普通近战家族，适配在server/ai/melee_families |
| NPC移动／交谈 | 原类型、服务定义、已知距离／资格条件、对白编号解释 | 交互租约、NPC占用、距离复验、介绍记录、特殊服务完成 | content准备服务纯值，server/npc协调；客户端仍消费原27／2F等，不运行商人AI |
| 商店／维修／赌博 | 价格和减价公式、维修量、容量／装配条件 | 真实货架、物品掷值、刷新种子、金币扣除、买卖／维修事务 | 现有content/items/item_pricing仍依赖ClassicData；接server/merchant前先拆“表准备＋纯报价”，不能直接给内核传ClassicData |
| 任务记录／面板 | 原编号／日志／D2S槽映射、旗标含义、已知记录的纯显示条件 | 按人按难度进度、本局公共状态、成员资格、世界条件 | 现有quest/catalog与client/quest_projection继续复用；UI中的TBL／绘制部分留客户端适配层 |
| 任务推进／奖励 | 事件与快照到转移计划的纯规则可供服务端各任务复用 | 杀怪／物件／NPC事件认定、多人贡献、奖励唯一发生标识、保存及原子提交 | 旧master各任务规则迁到server/quests；转移计划经transactions一次改记录／物品／成长，不能凭公共完成旗标给每个客户端发奖 |
| 物品／装备／掉落 | 容量、需求、属性贡献、品质／词缀公式、已知实例报价 | 物品身份和归属、真实随机、拾取争用、掉落发生次数 | 已有items纯计算和inventory事务继续扩展；UI预览与真实库存修改分离 |
| 地图／物件 | 地图生成与碰撞、原身份／坐标、交互距离 | 动态门／箱／祭坛状态、掉落、任务资格、区域驻留 | NativeMapGenerator继续两端共用；客户端Reveal顺序与服务端世界状态分别拥有 |

“任务资格条件可共用”只适用于输入完整且定义相同的条件。客户端没有世界怪物总数、队伍历史或其他人的任务记录时，结果应是未知，不能按空集合判成功。D2Common包含任务SetQuestState也不代表本项目允许客户端调用它写权威进度。

## 10. 迁移方法、批准范围与完成标准

1. 先读master已实现的规则、行为分派及边界，再看当前content和公共计算是否仍在；能直接复用的纯代码不复制第二份。旧代码耦合Enemy／GameSession时只迁规则和必要局部状态。
2. 记录具体证据与缺口：旧文件／函数、当前MPQ字段、Srv／Clt分工、事件先后及随机消耗。D2MOO用于公共层／执行层定位和旧实现缺失点；不因为新架构就重做一遍全部原版研究。
3. 先界定纯函数输入／输出，尤其基础与有效等级、难度、状态来源、整数取整、帧起点、种子所有者、实体与区域代次。未准备数据明确拒绝，不造参数或静默降级成另一种技能／怪物。
4. 服务端接运行状态、固定步、失败重试和提交点，再用原包编码结果。多个观察者只复制一次事实，不重复施法、死亡或奖励；输出容量不足不能消耗第二次随机或扣第二次资源。
5. 两端都需要的计算再替换客户端调用；仅服务端需要的规则保持服务端调用。共享函数不能引入自研／原服分支，不能要求旧原服提供额外字段。
6. 用户已授权弹体散射／环形／轨迹公共纯函数提取；涉及行为差异仍逐项确认。此授权不扩大为NPC、任务或其他客户端任意改写许可；这些领域需要改客户端时仍单独说明并确认。
7. 更新本页的提取边界、SERVER_SYSTEMS的实际执行范围及MULTIPLAYER的迁移顺序。存档语义改变同步规则指纹与SAVES，不静默迁移旧档。不编写测试脚本／用例／专用程序；构建／打包／运行验证按当轮有效授权，不能把源码接线当作运行认证。

当前已落地：resolve／projectile_path／combat geometry等公共计算；原表准备与独立server领域；女巫26主动／4被动、周期／环射／连锁／射流、反击吸收与Hydra；三种新普通近战怪物的纯决策与固定点走跑。后续恢复按行为族推进：怪物远程／萨满复活、周期／散射弹体、状态与效果、NPC报价事务、任务事件／奖励。该顺序是结构依赖建议，不表示这些功能已经完成。

### 女巫公共计算落点

当前projectile_path共用missileVelocityFixed、missileWallDirection（双方采用整数施法／目标射线的垂线）、chargedBoltPath、missileRingDirection、冰弹转向、missileChainSuccessor和blizzardOffset；cast_timing的playerCastSequence由两端共用seq12／seq6步序及释放步，普通SC／FCR时钟已供服务端及已知本人属性的客户端调用；rank_bonus共用四项被动线性值，geometry共用裁墙／交点及三格knockbackDestination。随机流由调用端持有，输出纯值，不共享实体或计时器。暴风雪客户端和服务端随机偏移符号有原版差异：D2Client 1.13c RVA BBC10／B8DF0证实客户端按remaining帧及globalX重种，半径−1反向偏移；D2MOO服务端保留正向偏移，函数参数显式区分。Inferno客户端24经74D50／74930选择原两种火图。火墙CltDo26经74770在目标点生成两侧maker及中心火，CltDo28经73DF0／A1540／AFF10在目标点生成中心弹体，均无ClientSend创建门槛；原服回归未见常规73，故自研同步删除其重复创建广播，普通本人4C／4D按PlrMsg省略。73保留可重建表现的有界不同来源配对，晚入视野的服务端重同步仍未接。执行／事件／MPQ规则所有权及尚未共享的调度／推进见[女巫技能](../gameplay/skills/SORCERESS.md#公共层及原版证据)，这些纯函数不承担资源或伤害权限。

### 女巫公共层的原版核对

2026-10-08直接核对本地固定D2MOO 5596f5c与原1.13c DLL。参考源码为1.10f重建，不能沿用其ordinal认定1.13c入口；以下1.13c编号均重新从本机PE导出／导入和调用指令核对。DLL摘要及基址见[资料来源](../resources/THIRD_PARTY.md#原版113c静态与开发对照证据)，只读分析输出在忽略目录artifacts/sorceress-d2gs-20261008，不加入源码。

“公共”需区分模块能被谁调用和具体计算是否只实现一次。原1.13c D2Client确实静态导入D2Game的13个ordinal入口（10038／10047／10037／10040／10017／10039／10049／10053／10006／10024／10019／10008／10043）；不能说客户端程序完全不使用D2Game。单机／TCP Host也在产品进程内运行本地服务端，复用D2Game的权威内核。远端联机客户端的表现程序则不因此成为第二个权威世界；下列冰封球两套散射与暴风雪两套创建流程仍分别位于D2Client和D2Game。DLL导入关系本身不能证明某技能整体已抽成共用函数。对应本项目，嵌入／LAN／未来独立宿主共用server内核；两端真正相同的计算再共用gameplay纯函数。

| 案例 | D2MOO边界 | 原1.13c可直接确认的证据 | 本项目提取结论 |
| --- | --- | --- | --- |
| Lightning／Inferno序列 | [SequenceTbls.cpp](../../reference/d2moo/source/D2Common/src/DataTbls/SequenceTbls.cpp:501)保存Lightning 19步及Inferno 15步；[Units.cpp](../../reference/d2moo/source/D2Common/src/Units/Units.cpp:1020)初始化／推进，ComputeSequenceAnimation输出mode／图像frame／dir／event | 原D2Common中两个完整6字节步序表分别位于RVA9C008／9B888；序列映射RVA2E6E0、ordinal10292，由D2Client RVA4D5EC经导入调用。推进RVA32460、ordinal10853，同时被D2Client及D2Game导入并调用；D2Game还调用初始化ordinal10099（RVA32820） | 步序／事件／固定点时钟可共用；客户端把frame映射COF／DCC，服务端在event上释放。现已共用步序及SC／seq12整数时钟；本人FCR已接，图像映射／状态推进仍由适配器执行 |
| Frozen Orb | [MissMode.cpp](../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp:1232)的SrvDo15自行判remaining周期并查64方向，SrvDo16自行转向；SrvHit29自行生成结束环射。这些主体不在D2Common | CltDo表索引19指向D2Client RVAB8840；它在本模块内判周期、查64方向并创建视觉子弹体。环表在本模块RVA D3F10；D2Game也有自己的环表。周期访问经D2Common ordinal10985（RVA6A240），函数为total-current的remaining；CltDo20亦使用该访问器 | 原版没有把整个散射程序抽成公共函数；可比原版进一步共享频率／方向／转向／动作描述，保留Clt／Srv参数。两端已按此证据修正remaining相位及子弹转向窗口；73已过帧前缀不得重复扣除 |
| Blizzard | [MissMode.cpp](../../reference/d2moo/source/D2Game/src/MISSILES/MissMode.cpp:1020)的SrvDo10调用D2Common技能公式，再调用D2Game自己的CreateMissileWithCollisionCheck（:955）做周期、重种、落点、碰撞与准入 | D2Client RVABBC10经stub C316调用D2Common ordinal10786／RVA51BF0技能公式；随后调用本地RVAB8DF0做remaining周期、globalX重种、反向偏移及视觉碰撞。变体图选择也在D2Client | 公式和基础数据共用；调度／实体创建分别执行。相同整数落点可抽纯函数，但必须保留随机消耗次序、客户端反向符号／图像变体及服务端真实命中 |

因此原版实际是“公共基础＋两端技能／弹体程序”，并非“公共完整技能＋客户端只加渲染”。本项目可以比原版去重更多：公共函数接收明确的小状态、规则值、时钟与随机，输出下一状态及Spawn／Turn／Expire等纯动作；客户端创建视觉对象，服务端创建权威弹体并结算伤害。不要把服务器世界查询、事务或AI装进公共执行器，也不要为追求完全一样而取消已证实的Clt／Srv差异。


全部30项的分派／公共函数／两端执行台账维护在[女巫技能](../gameplay/skills/SORCERESS.md#30项公共职责核对)，不重复记一份实现清单。D2Common的SKILLS_GetManaCosts只返回原定点曲线；D2Game Skills.cpp的法力消费再钳MinMana。技能伤害与独立弹体伤害的取整顺序必须分开：SKILLS_GetMinElemDamage在HitShift后计算协同，且fixed<=256且首级增量为0时跳过；MISSILE_GetMin/MaxElemDamage在HitShift前计算协同。原1.13c D2Common RVA50460的EMin／ELevMin读取与cmp0x100分支、RVA6B330的Missiles EMin／ELevMin读取及先协同后shift进一步确认；当前公共damage_curve分别定义并执行，Meteor地面火不套用技能伤害顺序。

FCR依据Units.cpp的UNITS_UpdateCastAnimRateAndVelocity：120*FCR/(120+FCR)，总速率上限175，再乘AnimData基础速率。OtherAnimationRate属于另一个mode分支，不叠加CAST；当前公共cast_timing已修正。PathMisc::sub_6FD5CEB0的每五tick加速／限速逻辑提取为advanceMissileVelocity，数值表示仍由两端适配。公共规则是SkillRuleSpec；原图／声音／绘制参数只留SkillSpec内容定义，通过rules()投影，不发布到server。
