# 佣兵与召唤物自身能力

更新：2026-10-16。本页维护同行单位的技能使用入口、等级来源和当前缺口。玩家召唤技能的算法归所属职业，单位归属／AI／同行与保存归companions、monsters及[佣兵](../characters/HIRELINGS.md)，不为每种召唤物再复制一套技能引擎。

| 单位 | 当前自研能力 | 未完成边界／规则归属 |
| --- | --- | --- |
| 第一幕罗格 | 血乌奖励真实单位；按Hireling条件／权重／等级准备普通RogueMissile、射击技能及Inner Sight，同行与目标选择已有入口 | 服务、装备和成长已接；全部组合未认证；选技及资格见[佣兵](../characters/HIRELINGS.md)，引用技能算法见[亚马逊](AMAZON.md) |
| Hydra | 头部出生、选目标、攻击技能、寿命与限额已有运行入口 | 玩家施放和伤害规则见[女巫](SORCERESS.md)；不代表全部来源／参数已验收 |
| 诱饵 | 本人归属、受击、寿命与移除已有入口，不作为攻击伙伴 | 玩家召唤与专属规则见[亚马逊](AMAZON.md) |
| 女武神 | 内容／装备准备、归属、近战、同行及死亡已有入口 | 算法和原图死亡缺口见[亚马逊](AMAZON.md)，不重复其伤害台账 |
| 第二幕沙漠守卫 | 真实活动实体、Jab双释放、六种天然光环、自身装备和成长已接 | 有限运行边界见佣兵专题 |
| 第三幕铁狼／第五幕野蛮人 | 铁狼七项法术／装甲和野蛮人Bash／Stun、真实实体及任务奖励／保存已接 | 完整组合未认证；玩家职业范围另见[野蛮人](BARBARIAN.md) |
| 死灵召唤物 | 骷髅／法师、四类石魔、重生及墙牢生命周期已接 | 算法与有限运行证据见[死灵](NECROMANCER.md) |
| 刺客陷阱／影子 | 六类真实陷阱的上限／弹药／AI、两类影子的MonEquip／技能来源／自主释放／跟随／解散已接，完成构建打包与代表单机冒烟 | 见[刺客](ASSASSIN.md)；复用真实非玩家属性／资源／随机，不构造玩家或敌怪替身；完整组合与原版逐帧对照未认证 |
| 德鲁伊召唤物 | 职业召唤与单位运行尚未迁入 | 见[德鲁伊](DRUID.md)；不得套用敌对怪物替身 |

本人召唤物和佣兵头像按当前MPQ PetType显示实际类型、数量及单只生命，当前Release包已包含；实际观察与未专项复验的交互范围由[头像](../characters/HIRELINGS.md#召唤物与佣兵头像)维护。

Battle Command作用于佣兵自身的临时全技能属性，在生效和到期时使内容准备缓存重算实际技能／被动，刷新保留Battle Orders生命上限。公共抽取前罗格技能6→7→6的有限单机证据见[野蛮人](BARBARIAN.md#当前包有限冒烟)，不推定其他佣兵全部技能／光环组合已认证。

佣兵的职业技能继续使用resolve／aura／武器／弹体公共入口；本次将其光环范围几何与人物／精英光环统一，召唤物支配／抵抗及被动提示共用纯求值。共用不改变单位AI选技、有效等级来源或状态刷新。抽取已纳入第一幕历史包的构建及第一幕有限冒烟，未专项复验全部佣兵／召唤技能组合；入口与证据边界见[COMMON](COMMON.md#公共函数抽取)。

## 接入与核对入口

[hireling_content.cpp](../../../src/hosting/hireling_content.cpp)准备四类原佣兵规则，[companions/hireling_control.cpp](../../../src/server/systems/companions/hireling_control.cpp)选择行动并提交技能／效果请求；[companion_content.cpp](../../../src/hosting/companion_content.cpp)准备亚马逊宠物，[companions/system.cpp](../../../src/server/systems/companions/system.cpp)持归属与寿命。权威技能、弹体、命中和状态仍归相应领域，客户端只消费原归属／单位／动作消息。

刺客内容由 `hosting/assassin_summon_content` 准备，`companions/assassin`／`assassin_ai`／`shadow_ai` 持归属、弹药、AI与继承，`skills/native_caster` 提供真实影子的独立施法者视图；`ai/target_query` 共享原生目标选择。实体、装备、来源技能与种子仍唯一归 `monsters::Actor`，技能和效果执行仍归已有领域。实际程序与版本证据只见刺客专题。

后续按当前Hireling、MonStats、PetType、Skills及MonEquip登记单位使用的技能、等级／装备来源与实际AI条件。某单位复用已实现技能不表示该单位完整可用；同名怪物版本也不自动等同玩家版本。运行证据链接所属职业与佣兵页；本页不扩大这些有限证据的范围。
