# 圣骑士技能资料与联机边界

当前MPQ战斗技能、20光环及30项技能树／提示定义继续保留。原服执行近战、突进、转换、治疗、格挡、光环范围／周期／互斥与伤害；旧本地圣骑士执行器已删除，历史30／30入口不代表完整联机视觉。

## 数据与边界

content/skills/weapon_skill_data、aura_data、state_data和gameplay纯aura_resolve／passive／伤害公式供已知提示与资源分析；不足武器、盾牌、属性或光环输入时保持未知，不重算原服防御／格挡。

普通技能原请求沿[公共技能](COMMON.md)。Holy Bolt友方、Conversion玩家／友军、宠物／多人光环目标与PvP尚未完整接入；Charge原序列、Blessed Hammer螺旋、Fist of the Heavens分裂及Holy Shield特殊盾图也不能以原表导入认定已还原。

## 参考证据

本地D2MOO SkillPal、D2Skills、Units::GetDefense／UpdateBlockAnimRateAndVelocity、SUnitDmg及事件／状态代码用于核对原服规则。Holy Shield必须有合格盾牌、装备变化与被动／光环贡献的生命周期仍归原服；当前MPQ没有可直接采用的特殊盾组件替换字段，不猜造原图。

已有纯公式与原图共享，未认证七职业完整表现。状态／模式消费与客户端程序限制见[联网模块](../../modules/NETWORK.md#联机游玩表现与输入)，来源／许可见[资料来源](../../resources/THIRD_PARTY.md)。
