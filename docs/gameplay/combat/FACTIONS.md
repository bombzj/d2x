# 阵营、目标与归属

原服拥有阵营、敌对关系、所有者、命中和伤害。客户端只校验当前已知目标与MPQ资格，不运行旧CombatUnit／dealDamage／经验分配链。

## 当前目标边界

RemoteCombat与RemoteScene目标选择统一核对原服alignment、真实单位类型、生命／状态、原技能TargetCorpse及MonStats2.corpseSel。NPC、中立单位、友军和敌人不能由图形Token推断；显示替身保留真实身份。

当前单位技能目标主要限PvE敌对怪物及合格尸体；玩家、佣兵／宠物、友方技能及PvP资格未完整接入。本人尸体取回是专门type0入口，不开放其他玩家loot权限。

## 关系副本与证据

remote_social保存原名册／队伍／关系值，公开信息与本人私有状态分开；离开视野不等于退出名册。尚无完整组队、敌对、交易UI／发送链，不能把原flags当已解释的服务权限。

原规则核对本地D2MOO SUnit::SUNIT_AreUnitsAligned、SUnitDmg伤害／经验分配与原包定义。关系及倍率仍由原服执行；MPQ数值不写死。协议边界见[联网模块](../../modules/NETWORK.md#多人只读副本)。
