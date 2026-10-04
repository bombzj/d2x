# 通用技能执行与代码分布基线

更新：2026-10-05。第四项装备／等级来源已随第五项通过 Windows Release，简单冒烟仅覆盖代表性装备路径，未重做技能来源与全职业回归；详见下方来源分工与[库存基线](INVENTORY.md#装备与技能来源第四项)。后续公共动作批次已让武器执行复用 units 的武器选择／帧时钟／取消，突进复用同步移动能力，施法起手共用攻击意图清理；技能施法／引导与专用释放继续由原 SkillRuntime 执行。该续批随活角色组合通过 Windows Release、Fire Bolt 与 Frozen Armor 代表性现场，未重做全部职业／武器／引导／光环回归，见[单位基线](UNITS.md)。当前已实现技能的执行迁移已接入源码：具体处理器不再实现 `Simulation` 成员，不包含会话、模拟器、`PlayerState` 或 `Enemy`。保留原 MPQ 定义、原技能 ID、已有能力范围及求值时点。技能行为迁移批次曾通过 Windows Release 与代表性冒烟；后续公共单位／冗余清理也已通过 Windows Release 与三职业简单冒烟，准确范围另行记录；见 [单位基线](UNITS.md)。当前运行包已包含上述迁移。未来多玩家、装备充能／触发及未实现技能不属于本轮完成项。

当前技能直接复用公共 `CombatUnit`，删除重复 `SkillUnit` 与两个逐字段转换；尸体／发射值在 `world_values.hpp`，原快照抗性取值调用纯 `rawResistance`，不再经世界虚接口。旧类型别名与延迟副弹命名同步清理；事实见 [单位基线](UNITS.md)。

最新诅咒批次（2026-10-04）已逐项完成十项规则修正、Windows Release链接和打包，`dist/current`已更新；后续简单冒烟覆盖属性互斥、真实反伤／治疗、AI控制与持续祭坛双向覆盖，源码与文档纳入本次提交。新增独立 `curse_data.*` 导入、`curse_resolve.*` 纯等级／免疫求值及 `curse_events.cpp` 命中事件；技能只借用公共单位和权威端口。`CurseLevel`按技能／状态／等级跨来源比较，同级只续期，低级拒绝；持续祭坛按原等级0加入同一覆盖通道，吸引保护有效状态。AI、参考冲突及未移植分支见[死灵法师基线](../gameplay/skills/NECROMANCER.md#诅咒逐项实现)，下方技能迁移运行证据均属既有批次，本次诅咒冒烟单列在上述专题基线。

毒素与白骨整页十项（2026-10-05）已逐项完成 Release 构建／打包及包内联合冒烟，当前包已包含。新增 `bone_data.*` 导入及 `bone_runtime.cpp` 的尸爆、障碍与飞弹行为；详细 `BoneSkillSpec` 由不透明不可变指针携带，公共骨墙／飞弹运行扩展也用前置声明。装甲吸收复用公共效果池和伤害入口，骨墙保持中立单位并通过权威端口生成。原规则证据、近似路径／放置和实际检查范围见 [毒素与白骨](../gameplay/skills/NECROMANCER.md#毒素与白骨技能)。

召唤整页（2026-10-05）补齐七项：`necro_summon_data.*`原表导入、`summon_resolve.*`纯等级／硬点求值、私有 `necro_summon_spec.hpp`详细定义、`companions.cpp`单位行为和会话 `session_necro_summons.cpp`物品／尸体适配。权威端口区分地面与尸体召唤，复用施法时序与成功后扣蓝；类型化减速事件、血魔治疗、铁魔武器命中沿公共消费者。逐项Release／打包、联合有限冒烟和铁魔新进程D2S恢复通过，准确边界见[召唤技能](../gameplay/skills/NECROMANCER.md#召唤技能整页)。

## 代码分工与入口

亚马逊标枪与长矛整页（2026-10-05）由 `amazon_spear_data.*` 导入九项新增行为，原瘟疫标枪继续用 `weapon_skill_data.*`。私有 `spear_spec.hpp` 携带不可变序列与参数，不透明指针仅在武器／弹体契约前置声明；新增序列数组或数值参数不会传播到会话、UI和存档头。`weapon_runtime.cpp` 复用装备选择、命中与连续动作，`spear_runtime.cpp` 经既有单位／世界端口生成充能弹、连锁及分裂弹。戳刺／刺爆的同一序列时钟映射原A1／A2帧；刺爆特殊磨损通过会话适配库存事务，毒枪沿物理弹体驱动39号毒云。十项逐项Release／打包、包内有限联合冒烟与原D2S重载通过，证据与明确适配见[长矛技能页](../gameplay/skills/AMAZON.md#标枪与长矛技能整页)。

亚马逊弓与弩整页（2026-10-05）由 `amazon_bow_data.*` 原表导入、私有 `bow_spec.hpp` 详细规则、`resolve.cpp` 等级／硬点求值接入。`WeaponSkillSpec` 仅携带不透明不可变程序指针；原A1时钟、武器来源、消耗和连续回滚复用 `weapon_runtime.cpp`，引导复用 `bone_runtime.cpp` 的原共同搜索程序；伤害转换／扇形快照／子弹体沿公共战斗链，牺牲火单位沿既有火周期。详细定义不扩散到公共状态头；首批公共命中值扩展需要重编依赖，后继私有弓程序变更无需重编全部公共模块。十项逐项Release／打包、联合有限冒烟与原D2S重载通过，准确规则和限制见[亚马逊](../gameplay/skills/AMAZON.md#弓与弩技能整页)。

| 层／入口 | 当前职责 |
| --- | --- |
| `character/learning.*`、`session_character.cpp` | 学习、选择、绑定和既有等级算术；角色学习记录不要求怪物共享 |
| `items/skill_sources.*`、`skills/rank_sources.*` | 物品侧筛选既有合格授予并携带 handle／revision；技能侧只合成传入的学习等级、授予与加成，不查询角色／怪物／库存 |
| `content/skills/skill_metadata.hpp`、`skill_data.*`、各 `*_data.*` | MPQ 与职业树导入；`SkillMetadata` 保存学习／显示字段，`SkillRecord` 通过只读定义指针关联执行参数，元数据头不引入完整 `SkillSpec` |
| `skills/spec.hpp`、`cast_spec.hpp`、各行为 `*_spec.hpp`、`visual.hpp` | 原定义、已求值结果、行为参数和表现描述；世界运行态只取施法结果，场景缓存只取所需表现字段 |
| `resolve.*`、`damage_curve.*`、`summon_resolve.*`、`aura_resolve.*`、`curse_resolve.*`、`passive.*`、`rank_bonus.*` | 纯求值；等级、协同、专精／装备加成由调用方显式准备。Warmth／专精复用等级算术，护盾扣蓝复用纯函数；诅咒按目标基础抗性独立折减 |
| `source.*`、`session_skill_sources.cpp` | 宿主映射学习身份和借用装备，准备有效等级；`UnitSkillSources` 按单位 ID 注册数值来源；本地玩家起手／延迟／反应走同一求值服务；怪物原生特殊飞弹继续显式传等级、空协同和零加成 |
| `caster.hpp`、`aura_owner.hpp`、`weapon_caster.hpp`、`projectile_source.hpp`、公共 `combat/unit.hpp` | 当前调用内借用动作／资源、武器派生值、坐标／随机流及公共战斗能力；没有完整角色、背包、任务或怪物记录 |
| `runtime.hpp`、`runtime.cpp` | 无持久状态的 `SkillRuntime` 入口；仅借用权威端口，声明不引入完整模拟器、角色／怪物、装备或技能总定义 |
| `world_port.hpp`、`weapon_port.hpp` | 权威世界查询／效果和独立武器能力端口；技能不读取地图存储、MPQ、设备输入或 GPU |
| `simulation/skill_world.cpp` | 当前唯一具体适配，连接实时世界／碰撞／战斗／物件／召唤与装备事务；这里允许读 `Simulation`、人物和怪物 |
| `cast_state.hpp`、`missile.hpp`、`events.hpp` | 待释放／引导／持续／突进值、飞弹／视觉效果值及技能事实；原权威状态和事件聚合头导入这些值，不拥有第二份运行态 |
| `monsters/companions.cpp` | 尸体消耗、宠物创建／上限、跟随、AI、旅行与死亡；技能只经端口请求召唤，世界保留具体单位所有权 |
| `combat/*`、`effects/state.*` | 通用攻击、弹体飞行／碰撞、伤害／减伤／死亡、属性与状态容器；不承担具体技能起手或效果分支 |
| 本人客户端投影、`presentation/hud/skill_*` | 学习资格、提示、树和菜单；客户端提交意图，读取授权视图 |

## 执行文件按行为归类

| 行为 | 当前文件 |
| --- | --- |
| 起手、延迟释放、引导和分派 | `casting.cpp`、`release.cpp` |
| 直接伤害、诅咒、自身／友军状态、物件／传送 | `direct_damage.cpp`、`curse_runtime.cpp`、`applied_effects.cpp`、`utility_effects.cpp` |
| 普通／扇形／路径弹体、通用直线发射 | `projectile_launch.cpp`、`runtime.cpp`、`projectile_path.*` |
| 电弧、陨石、暴风雪、冰封球、冰尖柱、火墙、天堂之拳、圣光弹 | `arc.cpp`、`meteor.cpp`、`blizzard.cpp`、`frozen_orb.cpp`、`glacial_spike.cpp`、`firewall.cpp`、`heaven.cpp`、`holy_bolt.cpp` |
| Blaze、Thunder Storm、火墙周期 | `elemental_runtime.cpp` |
| 武器技能、突进及武器命中贡献 | `weapon_runtime.cpp`、`charge.cpp`、`weapon_contributions.*` |
| 冰甲反击、护盾、反伤／偷取生命及状态事件 | `chilling_armor.cpp`、`reactions.cpp`、`shield.cpp`、`curse_events.cpp` |
| 光环启停／目标筛选／周期、怪物词缀光环／诅咒／死亡弹体 | `aura.cpp`、`native_effects.cpp`、`native_projectiles.cpp` |
| 尸爆、骨墙／骨牢与骨魂／骨矛推进 | `bone_runtime.cpp`；详细规则在 `bone_spec.hpp`，权威生成／尸体来源在世界端口 |
| 专属飞弹推进／命中分派与小型规则 | `missile_dispatch.cpp`、`missile_rules.*` |

`monsters/monster_element.cpp` 已不含玩家 Blaze／Thunder Storm，只准备 Countess 火墙与怪物元素伤害。`monster_enchantments.cpp` 保留词缀选择、事件与调度，效果进入技能处理器；原词缀光环与玩家光环存在周期／堆叠／来源差异，通过两个明确处理路径保留，不强行合并规则。`combat/attacking.cpp` 保留普通武器起手适配，武器技能与动作推进已移出。`player/movement.cpp` 只编排突进处理器，不再实现突进。原 `effects/reactions.cpp` 和 `skills/summoning.cpp` 已迁走，CMake 使用新路径。

## 当前调用链

```text
玩家意图 → 会话资格／来源准备 → UnitSkillSources → resolveSkill
  → SkillCaster／WeaponSkillCaster → SkillRuntime → 权威世界／武器端口
怪物 AI／动作／词缀事件 → 显式原参数／SkillProjectileSource
  → 同一发射、火墙、原生效果／死亡飞弹处理器 → 权威端口
通用弹体固定步 → 技能专属推进／命中分派 → 通用战斗与状态
延迟／反应求值 → 单位 ID ＋ 技能 ID ＋ 等级 → 原来源服务
```

共同发射入口 `launchStraight` 负责 ID、插入与子随机种子；玩家火／冰／圣光弹／引导和原生怪物普通飞弹实际调用它。多重射击、副弹及技能专属参数仍按原调用位置准备。怪物并未强行改用玩家 FCR、法力、装备或技能树。

## 所有权、时序与联机约束

- `SkillRuntime` 无长期单位／区域引用。`Simulation` 唯一拥有当前适配和状态；`SkillCaster` 等仅在本次调用链借用原字段。恢复／切区后重新准备能力，适配每次查当前存储。不得把能力视图存入队列或跨单位增删／区域替换使用。
- 等级／协同／加成显式传入；需要原后续动态求值时，端口按单位 ID 请求来源。`UnitSkillSources` 拒绝无效／重复绑定和未知单位，不回退到当前玩家；提供函数每次读取当前人物，恢复不保留旧记录引用。现有玩家与会话同寿命，独立来源注销使用 `unbind`。
- 飞弹处理期间的怪物死亡副弹经 `enqueueMissile` 进入现有延迟容器，避免使正在处理的飞弹引用失效。通用发射返回的借用只到下一次插入；怪物多重射击先复制原弹体。
- 起手、释放复验、扣蓝、装备变化复验、引导停止及延迟重新求值保持原顺序。武器命中／引导回调可能清动作，返回后检查动作仍存在，避免继续借用已销毁的状态。
- 技能运行值继续由人物／区域／效果容器拥有；没有全局侧表或复制一份人物。Hydra 的上限／移除及主人生存查询改按显式主人 ID，仍由具体随从适配管理。
- 联机客户端只能提交技能意图。等级、伤害、法力、充能与命中由权威侧准备／执行；远端可见单位无需复制他人的任务／背包。当前端口支持单机与未来服务端共用实现，但尚无网络传输／多玩家所有权。

## 完成范围与剩余边界

当前已实现技能的 S1–S5 行为迁移已接入：定义／元数据／结果拆分、来源路由、动作能力、主动行为、武器、反应、光环和原生怪物技能效果均有实际调用点。这里的“完成”指当前技能执行的代码迁移，不指整个 P3／P4／P5 的未来功能完成。

当前适配仍只为玩家提供完整施法／装备动作能力，未知施法单位明确拒绝。怪物复用当前已存在的原生飞弹、火墙、词缀效果和公共目标／伤害；未注册怪物不能请求玩家延迟求值。已有装备加成与授予已抽为物品筛选和纯等级规则，来源事实携带当前物品实例版本；真实装备充能／触发和起手／释放消费事务尚未实现，版本事实不代表已完成施法来源授权。今后接入应新增权威来源／能力适配，不能让技能反查 `PlayerState`。

被动贡献保留原已支持范围（Blessed Aim、抗性光环、专精／Warmth／护盾）；没有补齐其他职业的未实现被动。具体 AI、宠物存储、普通武器事务及完整单位所有权仍在世界／战斗适配。公共单位的具体指针已收进内部 `RuntimeCombatUnit.records`；怪物状态已独立到 `monsters/state.hpp`。商店／佣兵投影已迁移，完整单位存储、区域所有权和地图投影属后续阶段。

D2S v96、编码、指纹及保存语义未改，技能动作／宠物状态不新增入档；未读取或修改用户档和原 MPQ。未新增测试脚本、用例或专用程序。验证范围如下；不据此认证所有原版规则／随机流／客户端像素等价。

## 技能迁移批次的既有验证

以下是公共单位清理之前的证据；最新清理批次的构建与简单冒烟另见 [单位基线](UNITS.md#验证范围)。Windows Release 完整项目编译、`d2x.exe`／`d2x_assets.exe` 链接通过。统一验证在当前行为迁移完成后进行，收尾又移除了 Hydra 火弹与会话光环启停的残留，并重新编译对应调用链。

使用现有 `Send-D2XCommand.ps1`，普通／整局 seed 210、区域 8 的临时角色，经正式升级、学习和属性分配准备现场；未指定保存路径。

| 现场 | 观察证据 |
| --- | --- |
| 女巫 | 火弹命中巨兽 17→8.15625；暴风雪 158／159、冰封球 260／261；陨石 101 延迟后出现 18 个 240 地火；Inferno 引导后正式停止；冻结甲／护盾／碎冰甲状态、骷髅法师与反击承伤、Chain Lightning 施放、Thunder Storm 状态、Blaze 67 与传送落点均有记录 |
| 死灵法师 | 伤害加深物抗 -100，混乱覆盖旧诅咒，降低抵抗四系 -31；骷髅召唤截图已查看，同一尸体再次施法拒绝 |
| 圣骑士 | 正式快捷键选 Might→Holy Freeze，状态 33→43，旧效果移除；神圣之盾状态 101／盾防御 40%；重击伤害／位移、突进起手与移动击杀、热诚击杀、锤子 92 路径、天堂之拳 233 延迟及亡灵死亡 |
| Hydra 收尾 | 最终发射入口生成 247 火弹（观察伤害 20.3125）并击杀巨兽；生成截图已查看 |

四个临时实例均经 `quit` 正常退出，stderr 均空；圣骑士与最终 Hydra 实例另记录进程退出码 0。构建日志在忽略的 `artifacts/skill-runtime-build.log`，现场 JSON／截图在 `artifacts/skill-runtime-smoke-20261003/`。这些不是源码提交项；`dist/current` 未打包更新。

代表性冒烟未穷举所有原生词缀／光环、难度／免疫、所有装备变化与反伤死亡组合；不认证完整联机、装备充能或未实现职业技能。Linux 未在本机编译／运行；玩法保持现有 C++20、无 Win32／GPU 依赖。

## 公共头的共享合理性

2026-10-05 毒素与白骨逐项实现期间，已完成技能行为枚举依赖隔离。仅需存储枚举的值契约采用不透明声明，具体行为消费者在实现文件包含完整枚举；`ClassicData` 的怪物特殊飞弹改持不可变技能描述指针，`model/definitions.hpp` 删除未使用技能头。Windows Release 构建通过，同配置 Ninja 依赖快照显示 整页收尾时 `behavior.hpp` 264→34、`spec.hpp` 113→26、`cast_spec.hpp` 148→149 个编译单元；最后一项仍被运行状态按值持有。此计数是依赖闭包，不是编译耗时，也不代表全部公共状态已经稳定。

引用数用于衡量修改影响，不能单独判断设计质量。稳定的 ID、坐标、属性值、抽象接口或通用参数可以被广泛引用。重点核对：消费者是否需要这些类型、具体行为改动是否要扩展公共头、公共头是否带入不必要的完整结构，以及实际演进中是否保持稳定。

以下记录本批拆头前的证据。2026-10-03 既有 Ninja 依赖快照覆盖 212 个项目 `.hpp`／274 个项目编译任务；其中一条 `local_quest_client.cpp` 的源码时间晚于对象，标为不确定，未为此重新构建。完整计数和列表在忽略的 `artifacts/header-dependencies/report/`，不是耗时或其他配置的测量。

| 拆头前公共头 | 记录的受影响任务 | 当时判断及原因 |
| --- | ---: | --- |
| `core/bytes.hpp`／`math.hpp`／`id.hpp` | 230／228／219 | 广泛共享基本合理；字节、坐标和身份是基础概念，不能仅因计数高优先改为间接对象 |
| `quest/id.hpp` | 135 | 身份与幕别的小值契约，广泛共享合理；任务阶段和簿已另分头，不因新增对白／奖励扩展此头 |
| `skills/aura.hpp` | 202 | 值数据＋运行实例的表示总体合理，具体行为在 `.cpp`；共享范围和传递依赖过大，且还含 Redemption 等具体机制字段，尚不能认定完全稳定 |
| `effects/state.hpp` | 204 | 状态／来源／生命周期共用合理；状态描述、容器和具体反应动作共一个头，仅需描述值的消费者也获得全部定义，适合拆窄值头 |
| `skills/spec.hpp` | 165 | 优先收窄；混合多个行为参数、未求值／已求值数据和表现元数据，召唤引入完整战斗单位头，表现资源头直接持有具体技能结构 |
| `monsters/monster_spawn.hpp` | 202 | 优先收窄；怪物身份／生成、AI、战斗值、伤害类型、动作时序、特殊法术和词缀集中；仅需伤害枚举或 AI 参数也会获得完整光环 |
| `model/definitions.hpp` | 207 | 部分共享合理，但技能行为、怪物实现类别、区域／交互和定义混合；扩展一类枚举会波及其他领域，宜按消费者分开 |
| `simulation.hpp` | 50 | 计数低于上述值头，仍需拆执行职责；它包含不断扩展的具体技能／AI 私有声明，影响频率和开发耦合比单纯排名更重要 |

`aura.hpp` 并非光环基类：它只有 `AuraDefinition` 和 `ActiveAura`，没有具体光环子类或大段实现。已实现的不同光环复用参数，调整现有数值／公式或 `.cpp` 行为通常不要求更改其布局。此方向值得保留，是否使用继承不是主要问题。

原分析记录的直接字面量引用有两个：`unique_modifiers.hpp` 和角色显示计算实现。202 个任务主要经以下公共包含链传播：

```text
monster_spawn.hpp
  → unique_modifiers.hpp
    → aura.hpp
      → effects/state.hpp

inventory.hpp → items/definitions.hpp → weapon_projectile.hpp
  → missile_effects.hpp → monster_spawn.hpp → … → aura.hpp

quest_panel.cpp → scene_view.hpp → model/events.hpp
  → monster_spawn.hpp → … → aura.hpp
```

拆头前 `missile_effects.hpp` 需要的 `MonsterDamageType` 只是六类伤害的枚举，却由怪物生成大头提供。命中率与 AI 适配类似：只用怪物防御／类别或 AI 参数，却包含完整生成／词缀／光环。应先按真实消费者分离这些值。客户端事件还混入按值携带 `MonsterIdentity` 的死亡结算事实；权威侧必须保留资格快照，客户端表现消息应只携带许可的必要字段。

稳定性也有源码证据的限制：2026-10-01 的 `1fead56` 为 Redemption 向 `AuraDefinition` 增加三项参数，`e53d49b` 又增加命中类别／结果旗标。说明公共参数仍在随机制补齐演进，不能仅看 28 行就认定不会变化。反过来，也不能因此给每个原光环建立子类；原数据可表达的机制继续通过类型化参数和行为实现复用。

拆头前 `spec.hpp` 的扩散有更明确的表现边界问题：`scene_assets.hpp` 按值保存 `BlizzardSpec`、`ArcSpec`、`MeteorSpec`，并使用嵌套的 `SkillSpec::OverlayVisual`。资源显示需要相应表现描述，不需要诅咒、召唤属性和施法消耗的完整参数。应分开表现描述、行为参数和公共求值输入；仅拆成多个头再由原总头全部包含，不能完成隔离。

重构优先级：先截断伤害／AI／怪物身份值的跨领域传播，再分开技能表现描述与执行参数、整理来源／求值和执行端口。保留稳定的公共值与必要共享，复杂行为留实现。每次调整后用同一统计口径比较依赖闭包；多个包含路径可能同时存在，不能从删除一条 `include` 推算具体下降数量。
