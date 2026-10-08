# 战斗数值显示

最终命中、伤害、抗性／吸收、持续伤害、格挡、消耗和经验由连接的权威服务端结算；原服为D2GS，自研为server领域。共享纯公式不授予客户端结算权限。客户端的纯数值计算仅供提示及原资源报告，不能据旧单机计算覆盖原服副本。

## 保留输入与精度

当前MPQ的Properties、ItemStatCost、装备基础表、CharStats、DifficultyLevels、Skills／SkillDesc与怪物表提供定义。content准备显式值，gameplay纯函数不读取MPQ、设备或GPU。

TXT列名统一ASCII忽略大小写；StrBonus／DexBonus保留原字段。原武器属性增伤依据SUnitDmg::SUNITDMG_ApplyDamageBonuses：武器系数×人物属性／100进入百分比项，定点精度1／256，面板取整数。此公式需要完整输入；联机缺装备／加成时最终伤害、命中和格挡仍显示 `?`。

ItemStatCost在不同协议／保存路径的位宽、符号、Save Add与ValShift分别处理，不把D2S编码偏移套到原状态发送位流。怪物生命0x0C剥离暗金位后解释原0–128比例；不推造绝对生命。

人物提示入口见[角色模块](../../modules/CHARACTER.md)，原物品参数与缺口见[数据](../items/DATA.md)和[支持](../items/SUPPORT.md)。本地D2MOO ItemsTbls、ItemMods、SUnitDmg用于规则／字段交叉核对；不能单独认证原客户端全部显示。
