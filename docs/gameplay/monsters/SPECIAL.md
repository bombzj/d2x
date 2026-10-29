# 特殊单位、扩展事件与未归档身份

更新：2026-10-29。本页维护不适合直接归普通敌对家族的单位分类与待核实范围。type1、MonStats行、MonsterKind映射或技能槽都不等于敌对战斗实现；迁入和替身规则见[COMMON](COMMON.md)。

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
| Uber／Pandemonium等扩展首领与召唤 | 以本机1.13c为目标，145／146／147接RetailEmpty，不追击／攻击／召唤、不新增AITHINK事件；不复用D2MOO扩展。已有UberIzual及Lilith／UberDuriel身份保留，源码记录见[台账](ACT3.md)。本机方块特殊输出2／3同为空返回0；不创建Pandemonium门，不移植后续版本出生／奖励。本机零售目标不添加Realm World Event触发；其余来源范围见下文 |
| 未归档／保留的MonStats或预设行 | 用途、敌对资格与当前代码状态待核实，不能全部声明无用或未实现 |

后续按当前MPQ的MonStats／MonStats2、MonPreset、SuperUniques、Levels、PetType、Hireling及Objects交叉引用逐项归档，必要时查master与本地reference。确认使用者后归入所属ACT、BOSSES、伙伴或物件专题；本页只保留分类差异与尚未查明身份，不重复技能算法、任务奖励或运行流水。没有本类整体正常战斗／原服运行认证。

扩展首领依据：当前MPQ的MonAI索引145／146／147分别为UberBaal／UberMephisto／UberDiablo；本地1.13c DLL对应入口均为`ret 4`，不能用这些空入口认证D2MOO的完整Uber行为。目标按用户指定的本机1.13c保留这三个空分支，不移植D2MOO扩展或PD2程序；内容准备已识别这三项空AI，齐备时使用真实记录和对应基类生命周期，不因缺AI掉回沉沦魔；不要求AI不会使用的专用施法程序。空回调沿公共模式、伤害和诅咒外层后结束，不自行登记下一次AI事件。该源码修改已构建，行为仍待验收。本机表RVA10E788中，145／146／147默认入口B6C80／7B4E0／A39D0确为ret 4；145及147的切换回调B78A0／A59B0并非空，已保留清INFERNO、仅保留双坐标非零command10及中立等待1帧，再进入特殊状态。不能把默认空回调扩大成取消这些分支。空分支不再列为待补AI。本机方块输出表RVA FA5E8，第2／3项70180／70170均xor eax,eax;ret，已将零售钥匙门／最终门改为保留材料的空分支，直接门户准备亦拒绝原版该两种请求；PD2专用包装器保留。D2MOO A1Q4::CreateUberRunPortal／CreateUberFinalePortal及QuestsFX::UberPrimeEvil受D2_VERSION_HAS_UBERS保护，不能直接视为本机1.13c实现。World Event按本机零售DLL范围保留无自动触发路径，不增造SoJ阈值／全局计数或超级暗金替换；这不授权导入Realm／后续版本的出生修饰器。空输出源码已构建，行为仍待实测。

World Event范围补查：本机D2Game的pUnitList在1120、难度6D／资料片70等布局与D2MOO Game.h对应。对本机可执行节作线性反汇编，没有找到按该布局1DCC／1DD0（World Event状态／最后消息帧）直接寻址的读写；本地D2MOO也没有这些字段的触发消费者。该负面静态结果只支持“当前本机零售复刻不增加自动SoJ触发”的范围决定，不证明所有闭源Realm服务端都没有此事件。Diabloclone已有真实身份／Diablo程序和公共掉落记录；没有已核实的本机自然触发入口，不为它新造门、计数或替换规则。
