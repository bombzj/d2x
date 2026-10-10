# 佣兵与召唤物自身能力

更新：2026-10-10。本页维护同行单位的技能使用入口、等级来源和当前缺口。玩家召唤技能的算法归所属职业，单位归属／AI／同行与保存归companions、monsters及[佣兵](../characters/HIRELINGS.md)，不为每种召唤物再复制一套技能引擎。

| 单位 | 当前自研能力 | 未完成边界／规则归属 |
| --- | --- | --- |
| 第一幕罗格 | 血乌奖励真实单位；按Hireling条件／权重／等级准备普通RogueMissile、射击技能及Inner Sight，同行与目标选择已有入口 | 服务、装备和成长已接；全部组合未认证；选技及资格见[佣兵](../characters/HIRELINGS.md)，引用技能算法见[亚马逊](AMAZON.md) |
| Hydra | 头部出生、选目标、攻击技能、寿命与限额已有运行入口 | 玩家施放和伤害规则见[女巫](SORCERESS.md)；不代表全部来源／参数已验收 |
| 诱饵 | 本人归属、受击、寿命与移除已有入口，不作为攻击伙伴 | 玩家召唤与专属规则见[亚马逊](AMAZON.md) |
| 女武神 | 内容／装备准备、归属、近战、同行及死亡已有入口 | 算法和原图死亡缺口见[亚马逊](AMAZON.md)，不重复其伤害台账 |
| 第二幕沙漠守卫 | 真实活动实体、Jab双释放、六种天然光环、自身装备和成长已接 | 有限运行边界见佣兵专题 |
| 第三幕铁狼／第五幕野蛮人 | 铁狼七项法术／装甲和野蛮人Bash／Stun、真实实体及任务奖励／保存已接 | 完整组合未认证；佣兵入口不代表玩家野蛮人职业已实现 |
| 死灵／德鲁伊／刺客召唤物 | 职业召唤与单位运行尚未迁入 | 见[死灵](NECROMANCER.md)、[德鲁伊](DRUID.md)、[刺客](ASSASSIN.md)；不得套用敌对怪物替身 |

## 接入与核对入口

[hireling_content.cpp](../../../src/hosting/hireling_content.cpp)准备四类原佣兵规则，[companions/hireling_control.cpp](../../../src/server/systems/companions/hireling_control.cpp)选择行动并提交技能／效果请求；[companion_content.cpp](../../../src/hosting/companion_content.cpp)准备亚马逊宠物，[companions/system.cpp](../../../src/server/systems/companions/system.cpp)持归属与寿命。权威技能、弹体、命中和状态仍归相应领域，客户端只消费原归属／单位／动作消息。

后续按当前Hireling、MonStats、PetType、Skills及MonEquip登记单位使用的技能、等级／装备来源与实际AI条件。某单位复用已实现技能不表示该单位完整可用；同名怪物版本也不自动等同玩家版本。运行证据链接所属职业与佣兵页；本页不扩大这些有限证据的范围。
