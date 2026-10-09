# 物品技能来源与专用程序

更新：2026-10-09。本页维护物品相关技能分类、执行依赖与缺口。本页唯一维护充能与装备事件的技能来源／替代程序；共同事务与装备资格分别由[物品COMMON](../items/COMMON.md)和[EQUIPMENT](../items/EQUIPMENT.md)维护，技能本体归对应职业或[GENERAL](GENERAL.md)；本页不复制数值与保存编码。

| 类别 | 当前实现／缺口 |
| --- | --- |
| 卷轴／书本 | 鉴定217／218、回城219／220已有执行，来源和耗页详见GENERAL；不能据此推断其他物品程序已支持 |
| 普通／职业加成及物品授予技能 | rank_sources／rank_bonus与来源资格已有入口；只有已准备的具体技能程序可执行，授予条目不补齐未实现职业 |
| 充能技能 | 原技能层值、来源GUID、选择／热键、释放扣费、维修及保存已有入口；仅已实现程序可用，源失效不回退同ID普通技能 |
| 装备事件触发 | Attack／Hit／Kill／GetHit／Death／LevelUp有有界事件计划，原99／9A与已实现技能执行已有入口 |
| ItemEffect替代程序／目标检查 | hosting已有字段准备与部分处理；与已准备SrvDo不同的攻击替代程序、攻击方ItemTgtDo对怪物、随机尸体及额外start-check仍有明确暂缓项 |
| ItemCltEffect专用表现 | 全部程序与原条件尚未核实，不能把协议接收或普通技能Clt表现当作完整支持 |
| 其他物品专用Skills行 | 尚未完整逐项登记；未知条目归SPECIAL，不按物品名称猜规则 |

投掷药瓶属于[GENERAL](GENERAL.md)的武器／弹体分支，普通药水、Token和任务消耗品归[物品目录](../items/README.md)所链接的消费／任务专题；并非每种物品操作都有独立Skills行。触发概率、使用者／目标、替代程序和随机顺序沿原ItemEffect／ItemTarget／ItemTgtDo／ItemCheckStart证据核对，不以一次普通技能成功替代专用程序验证。

## 来源、扣费与触发程序

充能技能保留原层skill<<6|rank、原次数与源GUID；3C／51／23／7B、本人原物品位流及3E共同管理选择／热键／次数。充能释放免普通耗蓝，资源与次数同笔扣费，Inferno起始一次扣费；源失效显式取消，不回退到同ID普通技能。有效装备／套装层资格见[EQUIPMENT](../items/EQUIPMENT.md)，普通、符文之语和已激活套装层统一收集来源。保存沿原204层值，运行GUID不写D2S。客户端从原物品解析提供技能栏、热键与本人伤害显示，同样用于原D2GS。

装备触发由effects的有界ItemEventPlan接Attack／Hit／Kill／GetHit／Death／LevelUp，复用已实现技能程序并发原99／9A；概率只掷一次，输出背压不再次执行效果。hosting读取ItemEffect／ItemTarget／ItemTgtDo／ItemCheckStart，空ItemEffect不施放；本人／随机落点与防御方Oculus传送按SkillItem规则区分。Skills::Handler仅在a6／a7置位且ItemEffect>1时用替代Do程序；攻击触发的替代程序与已准备SrvDo不同时明确暂缓，不能沿普通技能执行。其他职业未实现程序、攻击方ItemTgtDo作用于怪物、随机尸体及额外start-check保持明确暂缓，诊断itemTriggers.deferred记录原因。事件框架不等于全部装备触发已实现。

## 依据与证据

后续先参考master的物品／技能处理，再核对当前MPQ与本地D2MOO `SkillItem.cpp`、事件和物品模式程序，来源见[资料来源](../../resources/THIRD_PARTY.md)。充能／触发的有限运行证据统一见[物品证据](../items/EVIDENCE.md)，当前包另见基线；未实现职业、完整目标／事件组合、专用客户端程序仍待补，不把框架标为全部可用。
