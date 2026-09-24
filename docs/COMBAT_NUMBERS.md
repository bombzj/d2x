# 战斗数值

本轮按运行时 MPQ 的 `Properties.txt`、`ItemStatCost.txt`、装备基础表、`CharStats.txt`、`DifficultyLevels.txt` 和怪物三难度表取值。`reference/d2moo/source/D2Common/src/Items/ItemMods.cpp` 用于核对属性函数，`reference/d2moo/source/D2Game/src/UNIT/SUnitDmg.cpp` 用于核对物理增伤、抗性、减伤、吸收与中毒单位。玩法层只消费内容层类型化的属性，不读取 MPQ 或物品展示文本。

## 入口与顺序

- `content/item_properties.cpp` 标出需要在生成实例时掷值的原属性函数；`content/equipment_modifiers.cpp` 和 `content/equipment_combat.cpp` 按原行、掷值及物品归属生成角色与逐武器修正。未知函数不猜效果。
- `gameplay/character/attributes.cpp` 由职业、等级、已分配点、有效装备和限时效果统一派生资源、命中、防御和抗性；`gameplay/items/equipment_stats.cpp` 派生每把武器的定点物理伤害、护甲和盾牌格挡。失效装备不参与修正。
- `gameplay/combat/weapon_elements.cpp` 在普通近战命中或箭矢／投掷发射时掷元素附伤、毒素和致命一击；即时元素范围与角色面板共用 `attackElementRanges`。弹体保留发射快照；`combat.cpp` 对每种伤害查怪物对应抗性。持续毒素在模拟帧中结算，死亡仍走同一个经验与掉落入口。
- `gameplay/combat/damage_resolution.cpp` 将来袭伤害转成 1/256 生命单位，按固定减伤、抗性、百分比吸收和固定吸收顺序结算。四种元素抗性先应用当前难度的 MPQ 惩罚，普通上限 75%，加上限属性最多 95%；物理抗性最高 50%。冰冷和毒素持续时间另行受抗性、免冰冻与毒素长度修正影响。
- `gameplay/combat/stat_modifiers.hpp` 是物品、技能、怪物、祭坛及环境效果共用的数值载体。`session/session_effects.cpp` 以来源、拥有者和 ID 管理限时状态，到期重算派生值。需要具体技能、怪物或祭坛施加的效果仍须逐个核对触发条件后接入。

角色面板显示已经参与当前结算的生命、法力、耐力、命中、防御、格挡、四抗、物理／魔法抗性、固定减伤及部分持续状态修正。`invchar.dc6` 的两组伤害／命中框分别对应左右技能图标，依据 [Blizzard 角色面板说明](https://classic.battle.net/diablo2exp/basics/characters.shtml)：普通攻击与当前以普通攻击代替的技能显示当前武器物理伤害及即时元素附伤，投掷显示投掷基础值，已接入原 MPQ 的火弹、火球、冰霜新星显示与实际施法共用的技能等级／协同伤害；无需命中检定的法术留空命中率。传送、取消召唤与无法用固定伤害表示的静电力场留空伤害。旋风仍显示当前 MVP 每秒伤害，跳跃攻击和战嗥显示当前 MVP 固定伤害；这三项尚待原 MPQ 技能规则重做。面板的物理范围使用原角色面板的整数呈现方式；毒素属于持续伤害，不加进即时范围。命名管道 `status.combat` 暴露每把武器的物理伤害、各类元素范围和其它已解析修正，方便验收。

## 明确保留的规则

`CombatModifiers` 已为吸取、压碎性打击、撕裂伤口、攻击／施法／受击／格挡速度、寻宝和金币加成留字段；它们的动画帧、目标特例、怪物吸取系数或掉落规则尚未全部核对，因此当前不宣称已生效。套装件数、插槽、按等级、条件、触发、光环、其余技能独有伤害与多数 Buff 的激活仍待对应系统实现。角色面板不把这些未生效字段写成战斗结果。

正式角色存档 v83、规则指纹 `v117-combat-calculation`：物品实例继续保存原词缀掷值，旧版拒绝载入；限时效果、怪物中毒和飞行弹体不存档。保存时当前资源裁剪到无临时效果的派生上限，载入后在城镇开启新的一局。没有把 MPQ 表抽取为独立运行文件。
