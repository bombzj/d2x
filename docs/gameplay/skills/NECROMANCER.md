# 死灵法师技能边界

更新：2026-10-09。自研服务端尚未迁入死灵法师30项职业执行。现有MPQ定义、树／图标、纯提示及铁魔D2S字段不能算作技能执行；通用操作见[GENERAL](GENERAL.md)，迁入规范见[COMMON](COMMON.md)。本页只维护已保留资料、明确缺口和核对入口，不记录旧单机完成度。

## 当前客户端与自研缺口

| 家族 | 已保留内容 | 待补／待核实 |
| --- | --- | --- |
| 诅咒 | `curse_data`／`curse_resolve`等级、范围与描述；原States／Overlay消费 | 自研诅咒覆盖、免疫、AI控制、反伤／吸取未迁入；客户端状态视觉与完整目标资格未认证 |
| 毒素／白骨 | `bone_data`、Missiles及纯伤害／路径资料；骨矛部分拖尾 | 自研毒／白骨主动未迁入；客户端骨魂追踪、毒云、墙／牢创建和装甲碎片未完整接入 |
| 召唤／被动 | `necro_summon_data`／`summon_resolve`、MonStats／MonEquip／PetType资料 | 自研召唤／装备／同行／AI及完整被动生命周期未迁入；通用7A名册与9D怪物装备可复用，但不表示全部死灵召唤显示或私有信息齐备 |

尸体目标按TargetCorpse、hide／udead及MonStats2.corpseSel筛选，消耗仍等待服务端事实，不由客户端提前删尸。铁魔原kf材料物品的独立编码见[SAVES](../../modules/SAVES.md)，不表示客户端新局重建铁魔。完整30项原服、多人与召唤生命周期没有逐项认证。

## 后续核对入口

先读master的既有死灵规则和当前纯函数，再查当前MPQ。D2MOO SkillNec、Skills、AiThink、PlayerPets、SUnitDmg／SUnitEvent及MissMode提供召唤、诅咒、状态反应、骨魂和毒云的执行证据；来源见[资料来源](../../resources/THIRD_PARTY.md)。

降低抵抗的D2Common_11033重建取整与原表曲线尚有未核实差异，现有纯显示求值不能据此扩张为权威免疫结算。骨墙段搜索、骨魂轨迹／尾迹缺完整原客户端证据。服务器执行、共享计算和Clt表现须分别补齐，不能把原服会执行或召唤资料已被女武神复用算作本职业完成。
