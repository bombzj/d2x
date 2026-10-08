# 阵营、目标与归属

连接的权威服务端拥有阵营、敌对关系、所有者、命中和伤害。客户端只校验当前已知目标与MPQ资格，不运行旧CombatUnit／dealDamage／经验分配链。

## 当前目标边界

RemoteCombat与RemoteScene目标选择统一核对原服alignment、真实单位类型、生命／状态、原技能TargetCorpse及MonStats2.corpseSel。NPC、中立单位、友军和敌人不能由图形Token推断；显示替身保留真实身份。

当前按技能核对PvE敌怪／合格尸体、Enchant友方及Unsummon本人PetType许可的宠物；Telekinesis另有物件／地面物品目标。其余友方技能、佣兵及完整PvP资格未接。本人尸体取回是专门type0入口，不开放其他玩家loot权限。

## 关系副本与证据

remote_social保存原名册／队伍／关系值，公开信息与本人私有状态分开；离开视野不等于退出名册。原服交易UI／发送及有限成交已接；组队、敌意及自研交易权威未完成，不能把原flags当已解释的服务权限。

原规则核对本地D2MOO SUnit::SUNIT_AreUnitsAligned、SUnitDmg伤害／经验分配与原包定义。关系及倍率仍由原服执行；MPQ数值不写死。协议边界见[联网模块](../../modules/NETWORK.md#多人只读副本)。
