# 特殊单位、扩展事件与未归档身份

更新：2026-10-09。本页维护不适合直接归普通敌对家族的单位分类与待核实范围。type1、MonStats行、MonsterKind映射或技能槽都不等于敌对战斗实现；迁入和替身规则见[COMMON](COMMON.md)。

| 类别 | 当前范围与负责入口 |
| --- | --- |
| 可击杀单位型陷阱 | 第一幕GargoyleTrap已有专用AI／原序列与射线，算法归[BOSSES](BOSSES.md)与[怪物技能](../skills/MONSTERS.md)；其余单位型陷阱待逐项登记 |
| 不可击杀发射器／环境机关 | 不混入敌对战斗人口；真实类型与交互归[物件](../world/OBJECTS.md)，未核实条目不伪装成敌怪 |
| 鸟巢／召唤源与召唤出来的敌对单位 | 第一幕鸟巢与血乌召唤已有，出生次数／身份／NOXP／NOTC沿所属家族；其他特殊出生源未完整迁入 |
| 牢门等可破坏非追击单位 | 第五幕prisondoor保留真实身份并由AI维持静止／不攻击；原数值与外观未完整准备，见[ACT5](ACT5.md) |
| 中立NPC、囚犯、牲畜及环境单位 | 不使用敌对替身；静态NPC和囚犯移动有部分入口，完整活动／受伤／死亡行为未接，见[NPC模块](../../modules/NPC_QUEST.md)及任务专题 |
| 佣兵／宠物／友军 | 归[佣兵](../characters/HIRELINGS.md)与[同行者技能](../skills/COMPANIONS.md)，不计入敌怪完成数；死灵／德鲁伊／刺客召唤物尚未迁入 |
| 骨墙／骨牢等职业生成单位 | 定义／映射存在不代表生命周期或执行已接；职业权威尚未迁入，见[死灵](../skills/NECROMANCER.md) |
| 牛场与其他特殊区域 | 牛类／Cow King第一幕准备已登记；开门／材料归[方块](../items/CUBE.md)，完整事件与首杀／资格不由准入证明 |
| Uber／Pandemonium等扩展首领与召唤 | 地图／配方入口不证明专用怪物AI与战斗；完整逐身份／程序／证据台账尚未建立 |
| 未归档／保留的MonStats或预设行 | 用途、敌对资格与当前代码状态待核实，不能全部声明无用或未实现 |

后续按当前MPQ的MonStats／MonStats2、MonPreset、SuperUniques、Levels、PetType、Hireling及Objects交叉引用逐项归档，必要时查master与本地reference。确认使用者后归入所属ACT、BOSSES、伙伴或物件专题；本页只保留分类差异与尚未查明身份，不重复技能算法、任务奖励或运行流水。没有本类整体正常战斗／原服运行认证。
