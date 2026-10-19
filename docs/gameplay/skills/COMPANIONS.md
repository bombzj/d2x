# 佣兵与召唤物自身能力

更新：2026-10-16。本页维护同行单位的技能使用入口、等级来源和当前缺口。玩家召唤技能的算法归所属职业，单位归属／AI／同行与保存归companions、monsters及[佣兵](../characters/HIRELINGS.md)，不为每种召唤物再复制一套技能引擎。

| 单位 | 当前自研能力 | 未完成边界／规则归属 |
| --- | --- | --- |
| 第一幕罗格 | 血乌奖励真实单位；按Hireling条件／权重／等级准备普通RogueMissile、射击技能及Inner Sight，同行与目标选择已有入口 | 服务、装备和成长已接；全部组合未认证；选技及资格见[佣兵](../characters/HIRELINGS.md)，引用技能算法见[亚马逊](AMAZON.md) |
| Hydra | 头部出生、选目标、攻击技能、寿命与限额已有运行入口 | 玩家施放和伤害规则见[女巫](SORCERESS.md)；不代表全部来源／参数已验收 |
| 诱饵 | 本人归属、受击、寿命与移除已有入口，不作为攻击伙伴 | 玩家召唤与专属规则见[亚马逊](AMAZON.md) |
| 女武神 | 内容／装备准备、归属、近战、同行及死亡已有入口 | 算法和原图死亡缺口见[亚马逊](AMAZON.md)，不重复其伤害台账 |
| 第二幕沙漠守卫 | 真实活动实体、Jab双释放、六种天然光环、自身装备和成长已接 | 原版依据与组合缺口见佣兵专题 |
| 第三幕铁狼／第五幕野蛮人 | 铁狼七项法术／装甲和野蛮人Bash／Stun、真实实体及任务奖励／保存已接 | 完整组合未认证；玩家职业范围另见[野蛮人](BARBARIAN.md) |
| 死灵召唤物 | 骷髅／法师、四类石魔、重生及墙牢生命周期已接 | 算法、原版依据与缺口见[死灵](NECROMANCER.md) |
| 刺客陷阱／影子 | 六类真实陷阱的上限／弹药／AI、两类影子的MonEquip／技能来源／自主释放／跟随／解散已接 | 见[刺客](ASSASSIN.md)；复用真实非玩家属性／资源／随机，不构造玩家或敌怪替身；完整组合与原版逐帧对照未完成 |
| 德鲁伊召唤物 | 乌鸦、狼／狂狼／熊、三藤蔓与三灵体接入真实Actor、原生技能／AI、PetType上限及互斥 | 依据、实际验证和限制见[德鲁伊](DRUID.md)；不套用敌对怪物替身 |

本人召唤物和佣兵头像按当前MPQ PetType显示实际类型、数量及单只生命；原版依据与交互缺口由[头像](../characters/HIRELINGS.md#召唤物与佣兵头像)维护。

Battle Command作用于佣兵自身的临时全技能属性，在生效和到期时使内容准备缓存重算实际技能／被动，刷新保留Battle Orders生命上限。原技能列表与资源规则见[野蛮人](BARBARIAN.md#生命周期与限制)，其他佣兵全部技能／光环组合仍需分别核对。

佣兵的职业技能使用resolve／aura／武器／弹体公共入口；光环范围几何与人物／精英光环共用，召唤物支配／抵抗及被动提示共用纯求值。共用不改变单位AI选技、有效等级来源或状态刷新；全部佣兵／召唤技能组合仍有缺口，入口与原版依据见[COMMON](COMMON.md#公共函数抽取)。

## 接入与核对入口

[hireling_content.cpp](../../../src/hosting/hireling_content.cpp)准备四类原佣兵规则，[companions/hireling_control.cpp](../../../src/server/systems/companions/hireling_control.cpp)选择行动并提交技能／效果请求；[companion_content.cpp](../../../src/hosting/companion_content.cpp)准备亚马逊宠物，[companions/system.cpp](../../../src/server/systems/companions/system.cpp)持归属与寿命。权威技能、弹体、命中和状态仍归相应领域，客户端只消费原归属／单位／动作消息。

移动继续点按原AI分别持有：四类佣兵在`hirelingStep`的WALK／RUN判断保留路线；女武神与死灵召唤物在各自AI重选目标前保留路线，重生另区分主人跟随与原家族移动。影子、德鲁伊召唤物和BladeCreeper已有`route.empty()`判断；固定诱饵、Hydra与不移动的卫哨不进入跟随移动。归位、寿命／死亡和动作背压仍归既有入口，不因等待路线而停止生命周期处理。具体依据与缺口见所属职业及佣兵专题。

刺客内容由 `hosting/assassin_summon_content` 准备，`companions/assassin`／`assassin_ai`／`shadow_ai` 持归属、弹药、AI与继承，`skills/native_caster` 提供真实影子的独立施法者视图；`ai/target_query` 共享原生目标选择。实体、装备、来源技能与种子仍唯一归 `monsters::Actor`，技能和效果执行仍归已有领域。实际程序与版本证据只见刺客专题。

后续按当前Hireling、MonStats、PetType、Skills及MonEquip登记单位使用的技能、等级／装备来源、实际AI条件、原版依据与缺口。某单位复用已实现技能不表示该单位完整可用；同名怪物版本也不自动等同玩家版本。具体规则链接所属职业与佣兵页，不在本页追加自研运行台账。
