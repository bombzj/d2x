# 亚马逊技能资料与联机边界

当前MPQ30项技能、三页图标／前置及既有纯提示数据保留；原服执行武器消耗、伤害、被动与召唤。旧弓弩、长矛／标枪和宠物执行器及其本地30／30完成记录已退场，不代表当前联机全部效果可用。

## 保留数据与显示

content/skills/amazon_bow_data、amazon_spear_data、amazon_magic_data、amazon_summon_data读取当前Skills／Missiles／States／MonStats／MonEquip。纯召唤／等级求值只供提示，不能在客户端生成女武神、装备、诱饵或被动能力。

| 家族 | 原规则证据与联机限制 |
| --- | --- |
| 弓／弩 | 原itypea、noammo、SrcDam、NextDelay、decquant与转换／协同字段；原普通装备弹体、部分扇形／爆炸已共用，引导追踪、炮轰序列和持续火未完整接入 |
| 标枪／长矛 | MonSeq、A1／A2事件、毒云／分裂／充能弹字段；实际耐久／数量与元素伤害由原服，连锁／毒云和全部武器动作未认证 |
| 被动／魔法 | 原有效等级、States、技能说明及召唤原图；三项防御、暴击／刺入／穿透、慢速箭及宠物生命周期不由客户端结算 |

## 核对入口

本地D2MOO SkillAma起手／SrvDo007／008／010／011／013／014／015／016、SequenceTbls、SUnitDmg、Missile／MissMode及SkillSor充能弹用于原规则交叉核对。参考重建函数曾有目标X／Y笔误及Calc[0]语义差异，不照抄矛盾字段；当前MPQ优先。

MonEquip候选与装备品质是原服生成规则，不能从显示缺口补造宠物装备。等级、选择、请求和未知提示沿[公共技能](COMMON.md)，通用客户端程序覆盖见[攻击表现](../combat/ATTACKS.md)。亚马逊完整原服技能、动作和保存重入尚未逐项认证，旧本地存档／联合冒烟不保留为当前支持清单。
