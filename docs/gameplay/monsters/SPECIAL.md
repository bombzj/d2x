# 特殊单位、扩展事件与未归档身份

更新：2026-10-17。本页维护不适合直接归普通敌对家族的单位分类与待核实范围。type1、MonStats行、MonsterKind映射或技能槽都不等于敌对战斗实现；迁入和替身规则见[COMMON](COMMON.md)。

| 类别 | 当前范围与负责入口 |
| --- | --- |
| 可击杀单位型陷阱 | 第一幕GargoyleTrap已有专用AI／原序列与射线，算法归[BOSSES](BOSSES.md)与[怪物技能](../skills/MONSTERS.md)；其余单位型陷阱待逐项登记 |
| 不可击杀发射器／环境机关 | 不混入敌对战斗人口；真实类型与交互归[物件](../world/OBJECTS.md)，未核实条目不伪装成敌怪 |
| 鸟巢／召唤源与召唤出来的敌对单位 | 第一幕鸟巢与血乌召唤已有，出生次数／身份／NOXP／NOTC沿所属家族；其他特殊出生源未完整迁入 |
| 牢门等可破坏非追击单位 | 第五幕prisondoor及window1／2已接真实属性／外观与空Idle，保持静止／不攻击，见[ACT5](ACT5.md) |
| 中立NPC、囚犯、牲畜及环境单位 | 不使用敌对替身；NPC使用唯一Actor和公共出生／休眠流程，囚犯已有实际移动与状态能力；完整行为及运行认证仍有缺口，见[NPC模块](../../modules/NPC_QUEST.md)及任务专题 |
| 佣兵／宠物／友军 | 归[佣兵](../characters/HIRELINGS.md)与[同行者技能](../skills/COMPANIONS.md)，不计入敌怪完成数；死灵、刺客与德鲁伊已有召唤执行入口，当前范围、原版依据与缺口以所属专题为准 |
| 骨墙／骨牢等职业生成单位 | 原生出生／列表关系／期限及生命周期已接，范围和原版依据见[死灵](../skills/NECROMANCER.md)，不因类型映射存在就扩大完成范围 |
| 牛场与其他特殊区域 | 牛类／Cow King第一幕准备已登记；开门／材料归[方块](../items/CUBE.md)，完整事件与首杀／资格不由准入证明 |
| Uber／Pandemonium等扩展首领与召唤 | 本轮暂缓，不作为常规战役缺口；已有UberIzual及Lilith／UberDuriel身份等代码保留，源码记录见[台账](ACT3.md)。高级事件专用出生、修饰器／AI与战斗仍未完成 |
| 未归档／保留的MonStats或预设行 | 用途、敌对资格与当前代码状态待核实，不能全部声明无用或未实现 |

后续按当前MPQ的MonStats／MonStats2、MonPreset、SuperUniques、Levels、PetType、Hireling及Objects交叉引用逐项归档，必要时查master与本地reference。确认使用者后归入所属ACT、BOSSES、伙伴或物件专题；本页只保留分类差异与尚未查明身份，不重复技能算法、任务奖励或运行流水。没有本类整体正常战斗／原服运行认证。

扩展首领依据：当前MPQ的MonAI索引145／146／147分别为UberBaal／UberMephisto／UberDiablo；本地1.13c DLL对应入口均为`ret 4`，不能用这些空入口认证D2MOO的完整Uber行为。后续以D2MOO已公开的完整程序为学习依据，明确保留版本差异与未验收范围。
