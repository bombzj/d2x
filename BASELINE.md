# 项目基线

更新：2026-09-24。供维护者和协作 agent 从当前代码继续工作。

资源与规则数据以运行时挂载的原 MPQ 为准；`resources` 解码、`content` 类型化适配，玩法只接收只读定义。不要把抽取后的表当作另一个需维护的数据源，也不要在玩法或界面写死物品名、数值、概率与资源路径。原 MPQ 未记载的引擎规则单独实现并注明来源；未核实的规则暂缓。既有模块仍有历史硬编码，按 [实施清单](docs/ITEM_COMPLETION.md) 逐项清理。

| 文档 | 内容 |
| --- | --- |
| [能力与缺口](docs/baseline/STATUS.md) | 全项目已实现内容、限制、后续工作 |
| [模块与接口](docs/baseline/ARCHITECTURE.md) | 依赖方向、状态所有权、扩展入口 |
| [数据与生命周期](docs/baseline/DATA.md) | MPQ、地图、怪物、物品、存档 |
| [开发与交接](docs/baseline/DEVELOPMENT.md) | 构建、资源、协作约定、当前检查状态 |
| [Agent 约定](AGENTS.md) | 开始修改前必读 |

专题细节：

- 世界：[地图](docs/ACT1_MAPS.md)、[怪物生成](docs/MONSTER_POPULATION.md)。
- 物品：[原表](docs/ITEM_DATA.md)、[实例与事务](docs/ITEM_MODEL.md)、[包裹](docs/INVENTORY_UI.md)、[腰带](docs/BELT_AND_CONSUMABLES.md)、[储物箱](docs/STORAGE.md)。
- 本轮物品补全顺序与当前进度：[物品与掉落补全](docs/ITEM_COMPLETION.md)。
- NPC：[对话、服务与原路径移动](docs/NPC_COMPLETION.md)。
- NPC 交易：[购买实施顺序与限制](docs/NPC_TRADE.md)。
- 界面与保存：[经典 HUD](docs/CLASSIC_HUD.md)、[存档](docs/SAVES.md)。
- 成长与战斗属性：[角色属性实施计划](docs/CHARACTER_ATTRIBUTES.md)、[七职业技能树](docs/SKILLS.md)。
- 怪物：[人口与替身](docs/MONSTER_POPULATION.md)、[分步实施与数值范围](docs/MONSTERS.md)。
- 工程：[构建与分发](docs/BUILD_AND_SHARE.md)、[MPQ 清单](docs/MPQ_RESOURCES.md)、[第三方许可](docs/THIRD_PARTY.md)。

当前工作：普通怪物及其普通随从的三难度生命、A1／A2 伤害与命中、防御、暴击、再生、抗性按真实身份共用运行时 MPQ 解析；缺少 A1 近战列时仍保留其余原属性。血腥荒地的普通沉沦魔随从不再误用精英 Minion 回退数值，`brute1–5` 使用原 `YE` 外观；普通骷髅的接近／停顿／攻击几率、沉沦魔的追击距离／游走／攻击几率、僵尸的警觉／游走／受击追击／埋骨之地强制追击与 Brute 受伤加速、近身攻击及绕行几率已分文件接入原 AI 参数与参考规则。`corruptrogue1–5` 按原 AI 参数接近、停顿及跑步，RN 动作和速度由 MPQ 驱动；`goatman1–5` 按原 AI 参数接近、攻击和停顿；`quillrat1–5` 使用原 A2 弹体和后撤行为；`wraith1–3` 按原参数接近、停顿和攻击，并按 A1 原元素列吸取法力；`cr_lancer1–3` 按原参数接近、跑动、攻击和停顿，长距离冲锋状态在当前游戏中维持；`cr_archer1–4` 使用原 A1 箭和后撤；`sk_archer1–3` 按原 SkeletonBow 参数射箭、停顿和短距离接近；`bighead1–4` 按原生命阈值切换近战、A2 闪电、绕行和后撤；`hellbovine` 使用原 Skeleton AI、牛形、`hal` 长柄武器及 A1／A2；`skmage_fire1–2`／`skmage_ltng1–2` 使用原火／闪电 A1 弹体和 SkeletonMage AI；`fetish1` 使用原 Fetish 近战循环与撤退阶段；`vampire5` 使用原 Vampire 近战、SC 施法和低血量撤退，技能弹体从 `Skills.txt`／`Missiles.txt` 运行时解析；`fallenshaman1–4` 使用原 A2 复活与火焰弹体，复活的同一尸体不会再次结算经验或掉落；`crownest1–2` 使用原 S1 孵化动作生成各自 `foulcrow1–2`，孵出的鸟无经验／掉落，鸟的 BloodHawk AI 按 MPQ 参数决策；`arach1` 使用原 A1／A2 动作、低血量 SpiderLay 与原蛛网图形，蛛网暂时减速角色。Fallen 家族见附近死亡动作中的尸体后逃跑，并以原 AI 参数作同组命令及 S2 喊叫，S2 图形、时长和声音均读取运行时 MPQ。`brute1–5`、`zombie1–5`、`skeleton1–5`、`fallen1–5`、`corruptrogue1–5`、`goatman1–5`、`quillrat1–5`、`wraith1–3`、`cr_lancer1–3`、`cr_archer1–4`、`sk_archer1–3`、`bighead1–4`、`hellbovine`、`skmage_fire1–2`、`skmage_ltng1–2`、`fetish1`、`vampire5`、`fallenshaman1–4`、`crownest1–2`、`foulcrow1–2`、`arach1` 的原动作和 MPQ 所列声音已接运行时 MPQ；A1 动作速度及命中帧、Brute、普通骷髅、僵尸与沉沦魔的 A2 动作和命中帧读取运行时 `AnimData.d2`；Brute、骷髅、僵尸与沉沦魔的 A1／A2 选择读取原 AI 参数。僵尸及骷髅同族变体复用各自 AI，由运行时 MPQ 的 `TransLvl`／`palshift.dat` 选色；`zombie4` 的冰冷、`zombie5` 的毒素、`skeleton4` 的 A1 火焰和 `skeleton5` 的 A2 闪电均从各自元素表触发，读取三难度原伤害、持续时间及难度抗性惩罚，冰冷影响移动与普攻，毒素逐帧扣血；状态及 AI 只在当前游戏中维持；正式角色存档为 v81、规则 v113，不保存怪物和临时战斗状态。命名管道可按 MPQ 怪物 ID 在指定可走格生成敌人并直接扣血／击杀；其他专属 AI、精英／首领和远程行为尚未完成，范围与顺序见 [怪物实施计划](docs/MONSTERS.md)。此前七职业技能树、基础远程攻击、五项女巫技能与 F1–F8 绑定已接；运行时继续直接读取原始 MPQ，旧档不迁移。
