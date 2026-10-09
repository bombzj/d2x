# 圣骑士技能边界

更新：2026-10-09。自研服务端尚未迁入圣骑士30项职业主动／光环执行，已有部分基础被动数值不代表完整生命周期。当前MPQ技能树、战斗／20光环定义和纯提示仍保留；通用操作见[GENERAL](GENERAL.md)，迁入规范见[COMMON](COMMON.md)。

## 当前客户端与自研缺口

| 范围 | 已保留内容 | 待补／待核实 |
| --- | --- | --- |
| 战斗／武器 | `weapon_skill_data`、纯伤害公式、通用请求／提示 | Holy Bolt友方、Conversion目标、Charge序列、Blessed Hammer螺旋及Fist of the Heavens分裂未完整接入；服务端职业执行未迁入 |
| 光环／被动 | `aura_data`、`state_data`、`aura_resolve`／`passive`及部分基础贡献 | 服务端周期／目标筛选／范围／互斥及完整来源生命周期未迁入；宠物／多人／PvP目标与客户端全状态表现未认证 |
| Holy Shield | 合格盾牌及纯提示规则 | 装备变化后的贡献与格挡生命周期未迁入；特殊盾图缺明确原版组件选择依据，不能由导入原表推断已还原 |

提示缺武器、盾牌、属性或光环输入时保持未知；客户端不重算原服权威防御／格挡，不安装光环。原状态消费与Clt程序范围见[NETWORK](../../modules/NETWORK.md#联机游玩表现与输入)。没有本职业全30项原服／多人认证。

## 后续核对入口

先读master的既有圣骑士规则和当前纯函数，再按MPQ核对。本地D2MOO SkillPal、D2Skills、Units::GetDefense／UpdateBlockAnimRateAndVelocity、SUnitDmg及事件／状态代码提供执行入口；光环实例／目标效果分工见[参考设计](../../architecture/REFERENCE_DESIGN.md#5-光环发射实例周期筛选目标效果)，来源见[资料来源](../../resources/THIRD_PARTY.md)。

Holy Shield的盾牌资格、装备变化、被动和光环贡献分别建生命周期；当前MPQ未提供可直接采用的特殊盾组件替换规则，原图选择须继续核实。原服执行这些技能不等于客户端表现或自研对应程序已完成。
