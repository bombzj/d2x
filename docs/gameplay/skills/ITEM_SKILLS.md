# 物品技能来源与专用程序

更新：2026-10-28。本页维护物品相关技能分类、执行依赖与缺口。本页唯一维护充能与装备事件的技能来源／替代程序；共同事务与装备资格分别由[物品COMMON](../items/COMMON.md)和[EQUIPMENT](../items/EQUIPMENT.md)维护，技能本体归对应职业或[GENERAL](GENERAL.md)；本页不复制数值与保存编码。

| 类别 | 当前实现／缺口 |
| --- | --- |
| 卷轴／书本 | 鉴定217／218、回城219／220已有执行，来源和耗页详见GENERAL；不能据此推断其他物品程序已支持 |
| 普通／职业加成及物品授予技能 | rank_sources／rank_bonus与来源资格已有入口；只有已准备的具体技能程序可执行，授予条目不补齐未实现职业 |
| 充能技能 | 原技能层值、来源GUID、选择／热键、释放扣费、维修及保存已有入口；仅已实现程序可用，源失效不回退同ID普通技能 |
| 装备事件触发 | Attack／Hit／Kill／GetHit／Death／LevelUp有有界事件计划，原99／9A与已实现技能执行已有入口 |
| ItemEffect替代程序／目标检查 | hosting准备原字段；替代Do36／151、受害者传送、目标4最后攻击者、尸体列表顺序选择、动态脚印随机目标和St14／19已接；已补原版非玩家诅咒、Static Field、AmazonMagic、Hydra及专用弹体；非玩家骨墙／骨牢及延伸、独立寿命已接；Fire Golem／德鲁伊非玩家召唤的后续AI仍未核实 |
| ItemCltEffect专用表现 | 99／9A独立即时回调、ItemCastSound／ItemCastOverlay、Clt10／96及尸体CltSt21已接；当前MPQ启用ItemCltCheckStart的两行均为CltSt21，按本地／远端资格处理，不改变普通动作或取消既有施法；各程序成功／声音时序的完整运行对照仍待完成 |
| 其他物品专用Skills行 | 尚未完整逐项登记；未知条目归SPECIAL，不按物品名称猜规则 |

投掷药瓶属于[GENERAL](GENERAL.md)的武器／弹体分支，普通药水、Token和任务消耗品归[物品目录](../items/README.md)所链接的消费／任务专题；并非每种物品操作都有独立Skills行。触发概率、使用者／目标、替代程序和随机顺序沿原ItemEffect／ItemTarget／ItemTgtDo／ItemCheckStart证据核对，不以一次普通技能成功替代专用程序验证。

## 来源、扣费与触发程序

充能技能保留原层skill<<6|rank、原次数与源GUID；3C／51／23／7B、本人原物品位流及3E共同管理选择／热键／次数。充能释放免普通耗蓝，资源与次数同笔扣费，Inferno起始一次扣费；源失效显式取消，不回退到同ID普通技能。有效装备／套装层资格见[EQUIPMENT](../items/EQUIPMENT.md)，普通、符文之语和已激活套装层统一收集来源。保存沿原204层值，运行GUID不写D2S。客户端从原物品解析提供技能栏、热键与本人伤害显示，同样用于原D2GS。

装备触发由reactions::ItemTriggers持有有界ItemEventPlan，effects保留推进适配入口；计划接Attack／Hit／Kill／GetHit／Death／LevelUp，复用已实现技能程序并发原99／9A；概率只掷一次，执行／通知分别保留继续点；输出背压下的全组合行为尚待验证。hosting读取ItemEffect／ItemTarget／ItemTgtDo／ItemCheckStart，空ItemEffect不施放；本人／随机落点与防御方Oculus传送按SkillItem规则区分。Skills::Handler仅在a6／a7置位且ItemEffect>1时用替代Do程序；原版Do36在受击者处生成64向Nova，Do151在真实目标处构造并随后写入Calc1；普通srvMissile攻击触发从目标出发，终点为两倍目标减施法者。受害者施法重新使用受害者的规则、状态和随机；Teleport使用实际单位尺寸及原脚印搜索。目标4在旧目标失效后查询最后攻击者，玩家与怪物均在接触阶段登记并保留身份引用。目标3按原房间／单位链选首个可用尸体，不额外掷数。St14／19拒绝城镇，St19另需真实目标；uninterruptable阻止执行。非玩家Fire Golem／德鲁伊召唤及其他未准备程序仍由itemTriggers.deferred明确记录，不以普通玩家技能可用推定全部怪物触发支持。事件框架不等于全部装备触发已实现。

## 依据与证据

后续先参考master的物品／技能处理，再核对当前MPQ与本地D2MOO `SkillItem.cpp`、事件和物品模式程序，固定来源见[资料来源](../../resources/THIRD_PARTY.md)。原版客户端专用入口静态核对本地1.13c D2Client SHA-256 `dd8bc6025de921216a97c17f97cd1a50fbb85926e838ec60e13451448836d906`：item wrapper RVA C9030、Clt10 RVA407C0、Clt96 RVA74610，替代选择由99／9A标记及当前MPQ的ItemCltEffect决定。尚未准备技能本体的拒绝边界、完整目标／事件组合及客户端逐程序运行对照仍待核实；分别维护具体来源的当前实现、原版依据和缺口，不以事件框架存在标记全部可用。

## 原版数据与回调边界

当前 `assets/mpq2` 按 Patch_D2、d2exp、d2data 优先级只读核对 UniqueItems、SetItems、Sets、Runes、MagicPrefix、MagicSuffix、AutoMagic、Gems：未发现概率施放光环或Stun的属性；Azurewrath和Lawbringer的Sanctuary为`aura`装备赋予光环。Skills表中119／139仍有ItemEffect=1，保留有原版依据的通用分支，不把可调用程序写成实际装备配置。单次光环回调不选择右手技能、不额外登记周期；Stun的Do2借用现有AllocCombat记录，无记录时不重新攻击，不执行St32。嵌套Drain、记录释放和背压继续点尚需实际覆盖。

D2MOO `SUnitDmg::ExecuteEvents`在HP扣除前发送伤害事件，扣除后按Killed、Kill顺序发送终结事件；随后`ExecuteMissileDamage`处理最终动作。`SkillItem::EventFunc20／21／30`在回调时读取当前总属性，再消费实际施法者随机。接触计划在扣血时冻结致死结果，终结回调同步推进且保留游标；背压重入不重新判定终结资格、不重扣HP，不另排队到装备撤出之后。原版怪物承伤补入GetHit／Death装备事件，GetHit按伤害记录标记准入，不以是否进入受击动画代替。物件伤害同样走这一继续点，依据`ObjEval::DamageUnit`的CalculateTotalDamage→ExecuteEvents→ExecuteMissileDamage调用链；直接扣生命的祭坛分支不扩成普通命中事件。原版Do16在本机1.13c RVA4D710额外限制玩家，非玩家Valkyrie触发按该版本拒绝，D2MOO对应版本没有此限制。

当前MPQ的ItemCheckStart仅Hydra／Bone Prison启用，对应St14／19；尸爆／毒爆只启用客户端CltSt21，不能误跑服务端St17。怪物负HPREGEN死亡另沿`MonsterMode::ApplyStatRegen`，无普通GetHit；该处Kill事件传空Damage，而EventFunc20要求DamageHitFlag32，不据事件名另加装备击杀施法。

触发计划的ResolveTarget／Execute／Notify和一次概率结果是源码继续点；可靠输出未受理时保留当前阶段。它们不构成已通过背压／重复执行验证的结论。非玩家骨墙／骨牢保留真实nativeMinionOwner，不进入玩家PetMax替换；延伸段继承原根，墙体按自身原寿命时钟推进，不另造NPC主人死亡清除规则。Fire Golem／德鲁伊非玩家召唤的合法主人及后续AI、客户端成功／音效时序的完整运行对照、全来源嵌套及逐随机轨迹仍未闭合，整个领域尚不能标记完全复刻。

有限运行范围：当前原版MPQ的Oculus受击传送、Arm of King Leoric受击骨牢及冷系Rainbow Facet死亡暴风雪均经真实物品请求安装、由实际怪物攻击触发；死亡暴风雪已观察原等级弹体及后续怪物死亡。隔离存档保存／重入、对应EXE身份、曾修复的失败及未覆盖范围见[验证记录](../../development/ORIGINAL_GAPS.md#装备触发工作区验证范围)。这些结果不认证全部技能、佣兵或强制背压组合。

非玩家召唤的拒绝边界：D2MOO Do56／119及公共SummonPet存在非玩家出生入口，但AIGENERAL_SetOwnerData直接保存实际来源，GetMinionOwner不向上寻找玩家；Fn067、Fn108调用的`sub_6FCE34E0`／PetMove读取PlayerData移动历史及传送坐标。当前1.13c Fn067跟随RVA1E140经Common导入序号10920取PlayerData，并在RVA1E36A／1E370读取传送坐标；配套Common的该导出RVA2EB70检查类型为玩家。不能据出生无类型守卫就推广玩家跟随或自造NPC兜底AI。相关非玩家入口明确拒绝，仍缺合法非玩家主人的完整原版执行依据；玩家召唤分支保留，未启用装备配置不伪装成功。PlayerPets::sub_6FC7D7A0在非玩家来源直接返回，因此NPC骨墙／骨牢不加入玩家容量替换。CltSt21（当前D2Client RVA C8330）的尸体选择检查只对本地施法者执行，远端只要求实际目标可解析。
