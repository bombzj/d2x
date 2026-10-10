# 技能目录与归档边界

更新：2026-10-10。本目录覆盖职业、通用、怪物、同行者、物品与特殊技能。分类入口已建立，不表示当前MPQ的全部技能已逐ID核对或实现；源码、有限运行及原版验收分别记录。迁入流程与公共计算由[COMMON](COMMON.md)维护。

## 分类入口

| 分类 | 负责页面 | 当前自研执行范围 |
| --- | --- | --- |
| 亚马逊 | [AMAZON](AMAZON.md) | 30项已接；仅列出代表路径有有限运行证据 |
| 女巫 | [SORCERESS](SORCERESS.md) | 30项已接；仅列出代表路径有有限运行证据 |
| 死灵法师 | [NECROMANCER](NECROMANCER.md) | 职业执行尚未迁入，保留定义与部分公共计算 |
| 圣骑士 | [PALADIN](PALADIN.md) | 10项战斗／20项光环执行已接，原MPQ参数／D2MOO依据与有限验证边界见专题 |
| 野蛮人 | [BARBARIAN](BARBARIAN.md) | 职业执行尚未迁入 |
| 德鲁伊 | [DRUID](DRUID.md) | 职业执行尚未迁入 |
| 刺客 | [ASSASSIN](ASSASSIN.md) | 职业执行尚未迁入 |
| 通用攻击／固有／卷轴／书本 | [GENERAL](GENERAL.md) | 十项入口已接，包含隐藏Kick；有限运行边界见专题 |
| 怪物／首领／精英／单位型陷阱 | [MONSTERS](MONSTERS.md) | 第一幕部分专用技能与精英效果已接；其他幕未全面迁入 |
| 佣兵／召唤物自身能力 | [COMPANIONS](COMPANIONS.md) | 四类真实佣兵、Hydra、诱饵／女武神有运行入口；其他职业召唤物未迁入 |
| 物品授予／充能／触发／专用程序 | [ITEM_SKILLS](ITEM_SKILLS.md) | 来源与触发框架已有部分实现，技能可用性依赖具体程序 |
| 其他特殊／保留／未归档技能 | [SPECIAL](SPECIAL.md) | 预留；剩余原表条目的用途与逐项状态待核实 |

## 唯一归属与复用

职业页维护对应Skills行的专属算法及Clt／Srv差异；GENERAL维护通用十项。MONSTERS维护怪物专用程序和精英触发能力。COMPANIONS维护单位选技、等级与运行入口；ITEM_SKILLS维护物品来源和替代程序。同一技能被佣兵、怪物或物品使用时链接原规则页，不复制伤害、动作或弹体算法；仅记录使用者实际不同的条件。

物件机关与任务机关分别由[物件](../world/OBJECTS.md)和[任务系统](../quests/SYSTEM.md)负责；只有真实调用技能程序的部分进入技能台账。普通A1／A2、AI决策、MonUMod效果、状态／Overlay、弹体行和Skills行不是同一概念，不能全部计入职业技能完成数。跨技能行为族与弹体机制分别链接[COMMON](COMMON.md)及[攻击与弹体](../combat/ATTACKS.md)，不再建第二份公共规则。

## 逐项登记与待补范围

完整登记应以当前MPQ的Skills行为主键，保留ID／原名／使用者、CltSt／CltDo／SrvSt／SrvDo、弹体／状态关联、客户端支持、自研执行、公共函数、证据与缺口。SkillDesc／CharStats用于职业归属，MonStats／Hireling／物品来源用于交叉引用；未被技能树收录不等于无用途。保留行、空行与未引用程序分别标记，未知用途先归SPECIAL，不自行推断执行。

现有女巫、亚马逊、圣骑士和通用技能有逐项台账；怪物与同行者页先按当前代码的执行家族归档。其余四职业及全部非职业原表行尚未形成完整逐ID清单，后续在所属页补齐，不能把本目录分类齐备宣称为全技能审计完成。当前状态及后续顺序见[基线](../../../BASELINE.md)与[总计划](../../architecture/MULTIPLAYER.md)。
