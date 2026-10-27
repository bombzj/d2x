# 阵营、目标与归属

连接的权威服务端拥有阵营、敌对关系、所有者、命中和伤害。客户端只校验当前已知目标与MPQ资格，不运行旧CombatUnit／dealDamage／经验分配链。

## 当前目标边界

RemoteCombat与RemoteScene目标选择统一核对原服alignment、真实单位类型、生命／状态、原技能TargetCorpse及MonStats2.corpseSel。NPC、中立单位、友军和敌人不能由图形Token推断；显示替身保留真实身份。

当前按技能核对PvE敌怪／合格尸体、原关系位8允许的活玩家type0目标、Enchant友方及Unsummon本人PetType许可的宠物；Telekinesis另有物件／地面物品目标。四类佣兵自身战斗及典型友方光环已接，范围见[佣兵](../characters/HIRELINGS.md)；其余友方技能及完整PvP资格仍有缺口。本人尸体取回是专门type0入口；他人尸体仅按[HC单向授权](../characters/PLAYER_DEATH.md#hardcore永久死亡与尸体许可)的独立资格处理，不由友方关系自动开放。

客户端type1目标分类明确保留Unknown：未知MonStats身份或未解码状态不自动成为友军，只有已知NonHostile才通过友方技能的该项检查，存活和目标位置仍另验。hostileSource=false仅表示未确认敌对，不能作为通用友方授权；宠物名册、NPC／中立表与alignment沿当前公开值处理。依据D2MOO SUnit的GetOwner／AreUnitsAligned分开职责，不新增队伍或PvP语义。

服务端monsters::Actor能力谓词仅集中已有非玩家实体规则，不能以damageable推断敌对、以combatCompanion推断控制权限。AI目标威胁优先级、召唤归属与期限、NPC服务资格、伤害和物品事务仍由各领域持有；完整关系／争用边界仍待核对。

## 关系副本与证据

remote_social保存原名册／队伍／关系值，公开信息与本人私有状态分开；离开视野不等于退出名册。原服交易UI／发送及自研[交易权威](../items/PLAYER_TRADE.md)已接；聊天ignore／squelch执行原方向关系。组队邀请／名册及有向敌意已接，PvP当前入口与缺口见[武器与PvP](WEAPON_EFFECTS.md#pvp伤害)；聊天关系不能充当战斗服务权限。

原规则核对本地D2MOO SUnit::SUNIT_AreUnitsAligned、SUnitDmg伤害／经验分配与原包定义。关系及倍率仍由原服执行；MPQ数值不写死。协议边界见[联网模块](../../modules/NETWORK.md#多人只读副本)。

## 权威关系与技能资格

本节区分阵营关系与技能接收资格。历史概括中的“善方NPC可附魔”只描述关系候选，不足以证明服务NPC能安装列表；原公共投递还检查npc／isAtt等字段，详见[按原程序区分接收资格](../skills/EFFECTS.md#按原程序区分接收资格)。具体原程序优先于通用友方概括，完整NPC组合仍待核对。

普通怪物准入保留显式`hostile`事实；`Actor::alignment`统一原表阵营、主人与转化覆盖，`enemyTarget`还要求原敌对身份、与玩家善方不同阵营且非环境控制器。网络投影与权威关系使用同一有效值，不再将任意无主人实体视为敌人。`gameplay/combat/alignment.hpp`提供两端共用的原单位枚举：恶方0、中立1、善方2；MonStats.Align的善方1／中立2只在内容准备时转换。两个不同单位只有恶方／恶方、善方／善方算同阵营，中立／中立不算。

`Participants`按当前玩家、实际宠物主人和social关系返回Self／Allied／Neutral／Hostile／Unknown；普通攻击要求Hostile与目标受伤能力，帮助资格独立。玩家及宠物之间按攻击者→目标主人的位8决定PvP资格，优先于队伍；非队友不等于敌对，Unknown拒绝攻击。依据为本地D2MOO `STATLIST_AreUnitsAligned`、`SUnit::SUNIT_AreUnitsAligned`、`SUNIT_CanPetBeTargetedBySkill`、`SUNIT_CanAllyBeTargetedBySkill`；当前技能参数和过滤标志仍来自MPQ准备。原1.13c客户端静态依据与未复验边界见[阵营专题](../monsters/PRESENTATION.md#客户端与服务端阵营规则)。

| 双方／用途 | 当前策略 |
| --- | --- |
| 本人、同主人伙伴 | Self／Allied；禁止普通伤害，技能专用资格独立 |
| 同队玩家及伙伴 | 友方光环按social当前同队关系，候选和提交均复验 |
| 不同主人玩家及伙伴 | 有攻击者方向敌意为Hostile，否则按队伍或Neutral；不自动授予光环 |
| 玩家／伙伴与明确敌怪 | Hostile；目标还须具备受伤能力，活人／区域／城镇／碰撞另验 |
| 敌怪与敌怪 | Allied；特殊复活／死亡级联仍由专用规则执行 |
| 诱饵、女武神、佣兵 | 关系随控制者；攻击能力独立，诱饵不因友方身份获得攻击 |
| Hydra | 保留不可受普通伤害、独立发射者表现和主人施法归功 |
| 转换单位 | 临时善方；施法者记录用于AI／归功，不当作原AIGENERAL宠物主人；不因此获得宠物治疗资格。转换开始／结束递增控制代次，旧来源请求失效 |
| NPC、未知实体 | NPC按真实阵营及isAtt能力进入公共参与者视图；服务／任务身份本身不授权伤害或帮助，未知实体拒绝 |

AI、怪物施法、普通武器／范围目标、伙伴选敌、弹体、周期效果与最终combat已接共享关系或敌怪能力查询。Holy Bolt、Enchant、Unsummon、Redemption及尸体仍保留各自资格，不把同队当作所有技能的通用授权。非玩家光环在提交时复验当前关系，避免候选捕获后离队／转换仍授予效果。

Enchant按原SrvDo025对齐主人／队伍后的盟友关系，已知活目标不合资格时退回施法者；无主善方怪物／NPC也可获得附魔，NPC阵营由hosting准备并用于同一AA投影。Holy Bolt的治疗候选包含玩家及怪物，只接受自己的宠物或队友／队友宠物，碰撞与生命提交均复验；不能以“非敌对”代替治疗资格。Conversion只接受当前恶方，结束回到恶方；原A7不清旧属性，因此hosting在阵营非零变零时用A8显式发送零值。

GreaterMummy辅助候选按原Fn022要求与施法者同阵营且目标非善方，避免转化后继续治疗／复活恶方；独特FallenShaman回调按原Fn013要求尸体为恶方，不能仅凭非善方通过。
