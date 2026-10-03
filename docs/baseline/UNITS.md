# 公共战斗单位与内部绑定基线

更新：2026-10-03。对应改造方案 P4 的公共视图切片。已迁移源码及调用方，本批按后续授权通过 Windows Release 编译／链接与三职业简单冒烟，未打包；准确覆盖见下方验证范围。完整单位存储、控制策略和多人所有权仍待实施。

## 当前分工

| 入口 | 职责与边界 |
| --- | --- |
| `combat/identity.hpp` | `CombatRole`、`Relation`、`CombatIdentity`；身份、阵营、主人、队伍和可受攻击标志，不包含单位状态／属性或关系容器 |
| `combat/relations.hpp` | 本局单位／阵营的定向关系表与原默认关系；世界唯一拥有，身份消费者不引入 map 容器 |
| `combat/unit.hpp` | 公共 `CombatUnit`，借用坐标、生命／法力、毒／冷、随机流和效果能力，复制原属性快照和单位分类；不声明或保存 `PlayerState*`／`HirelingState*`／`Enemy*` |
| `combat/damage_request.hpp` | `DamagePermission`、`DamageRequest`；只依赖 ID、伤害类型和数组，不引入单位布局或属性派生 |
| `simulation/unit_records.hpp` | 内部 `RuntimeCombatUnit` 扩展公共视图，`records` 保存具体人物／佣兵／怪物绑定；只供当前权威实现，不能从技能／客户端端口返回 |
| `simulation/unit_records.cpp` | 按原 ID 查询当前状态并构造绑定／属性，枚举顺序仍为玩家、有效佣兵、当前区怪物、随从；保留原装备、奔跑、词缀、转换和属性求值次序 |
| `combat/damage_resolution.*` | `rawResistance(attributes, type)` 是原六通道未封顶抗性取值；减伤继续在原入口处理不同单位规则及上限 |
| `simulation/skill_world.cpp` | 技能端口返回公共视图，自动剥离内部记录绑定；冷效果／具体动作仍在适配内按 ID 取得当前记录，保留调用方属性快照 |
| `skills/world_values.hpp` | 技能需要的尸体位置／资格和光环发射借用，不再定义另一份战斗单位 |

`PlayerState`／`HirelingState` 的身份字段直接包含身份头，世界另包含关系与属性值头；状态模型不再通过公共单位头引入战斗请求。技能武器端口仅前置声明能力值，完整装备、动作、效果容器定义留在使用它们的实现文件。

## 调用与所有权

```text
权威记录 → Simulation::combatUnit(id) → RuntimeCombatUnit
  ├─ 当前战斗／受击／死亡／AI 适配 → records 中的具体绑定
  └─ 技能端口／通用目标筛选 → CombatUnit 公共能力
显式属性快照 → rawResistance → 原冷／诅咒／减伤规则
```

公共视图和内部绑定都只在当前调用中借用，不持久保存，不创建第二份人物或怪物。单位扩容、删除、恢复和切区后重新按 ID 获取；公共视图中的指针仍是权威侧的短期能力，不是网络消息或客户端可写状态。

`player`／`hireling`／`monster` 记录原存储类别，`CombatIdentity.role` 继续独立表达玩家、怪物、佣兵和召唤物的归属。召唤怪物仍保留 monster 分类，以维持原冰冻、诅咒、转换及死亡分支；没有用角色类别替代存储类别而改变规则。分类与内部指针由同一绑定函数一起准备。

会话里的光环／诅咒资格和 ColdEffect 原表桥仍是权威内部适配，参数改为内部绑定；技能只向端口传单位 ID。抵抗率本来只读取传入快照，已移至纯函数，删除模拟器成员、技能代理和世界虚接口，不增加 MPQ 查询或改为重新读取活动属性。

## 已清理的冗余与旧入口

- 删除 `skills/unit_view.hpp` 的重复 `SkillUnit`；全部处理器及端口直接用公共 `CombatUnit`，移除 `skillUnit`／`combatView` 两个逐字段转换函数。
- 删除 `Simulation::unitResistance`、技能同名代理及 `ISkillWorld::resistance`；原取值逻辑统一到 `rawResistance`，上限、冷时长、除法及截断位置保留。
- 删除无外部调用方的 `PlayerState::PendingCast`／`ChannelCast`／`ChargeState`／`ThunderStormState` 类型别名；成员直接使用对应技能运行值，字段／保存含义未变。
- 删除 `MonsterMissileCast` 与 `SkillSpec::OverlayVisual`／`ImpactVisual` 兼容名称，原调用方改用通用类型；定义仍来自当前 MPQ。
- 延迟副弹容器统一命名 `deferredMissiles_`，所有生产／消费点同步，不改变迭代期间延迟插入的时机。
- 清理迁移后无用的效果／单位包含和重复控制语句；重击直接传计算出的伤害值，删除仅取 amount 的临时伤害请求，命中类别仍由原 `AttackElements` 提交。

清理依据是当前源码引用和职责迁移；未删除 MPQ、存档、参考代码、旧运行包或 `mvp/`，未引入新的原版规则或自动迁移。

## 剩余范围与验证状态

当前 `WorldState` 仍有单个玩家及当前区域，人物／怪物／佣兵存储、AI、受击／死亡与资源推进仍在原权威实现；本批不代表 P4 全阶段完成。`RuntimeCombatUnit` 的具体绑定仍在内部适配中使用，后续可继续按动作／资源能力迁移。多玩家注册、跨区查找和远端可见投影尚未实现。

## 验证范围

2026-10-03：按后续用户授权构建最新源码，Windows Release 全项目编译及 `d2x.exe`／`d2x_assets.exe` 链接成功。修复九个技能实现文件在聚合初始化伤害请求时缺少 `combat/damage_request.hpp` 的问题，完整类型只在对应实现中显式包含，未重新放回公共单位／端口头。

使用既有 `Send-D2XCommand.ps1`、构建目录的游戏入口及当前 `assets/mpq2`，普通难度／整局 seed 210／区域 8 创建三个临时职业现场。通过正式升级、学习、快捷键选择和 `UseSkill` 消费者检查现有流程，未传 load／save，不读写用户角色档。

| 现场 | 实际观察 |
| --- | --- |
| 女巫 | 启动、外观和库存查询正常；Fire Bolt 正式施法命中巨兽，生命 17→13.515625；人物也实际受击，公共受击路径正常推进 |
| 死灵法师 | Amplify Damage 完成出手后目标状态 9、物抗修正 −100；调试击杀经原死亡／经验结算，尸体完成死亡动画后 Raise Skeleton 接受并生成骷髅，截图已查看 |
| 圣骑士 | 正式 F1／F2 选择 Might 98→Holy Fire 102，状态 33→35，旧状态移除；推进至原脉冲时点后巨兽生命 17→14.5859375 |

三个实例均经 `quit` 正常退出，分别记录退出码 0，stderr 全部为空。女巫／骷髅召唤截图已查看；构建日志在忽略的 `artifacts/refactor-units-build-final-20261003.log`，现场 JSON／截图与退出结果在 `artifacts/refactor-units-smoke-20261003/`，这些不纳入源码提交。

简单冒烟未覆盖全部技能／单位、佣兵／原生词缀、难度／免疫、保存往返或完整键鼠操作；未验证 Linux 或联机。未新增测试脚本／用例／专用程序，未打包；`dist/current` 仍是此前版本。D2S v96、编码、规则指纹、MPQ 和用户档未改；未测量增量编译时间。
