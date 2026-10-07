# 公共技能与状态表现

当前产品只提交原技能请求并消费原服等级、动作、状态与结果；SkillRuntime、本地效果容器及技能执行器已删除。MPQ定义和原提示公式继续共用，文件边界见[角色模块](../../modules/CHARACTER.md)与[联网表现](../../modules/NETWORK.md#联机游玩表现与输入)。

## 定义、等级与提示

Skills／SkillDesc／CharStats、原TBL及七套技能树／职业图标提供ID、页／行／列、前置、左右键／城镇资格和描述。当前原表每职业30技能；节点数量、图标可见或提示可计算不代表全部联机视觉已实现。

projectCharacterDisplay使用原服基础／加成等级与已知属性；既有resolve、damage_curve、rank_sources／rank_bonus、passive、aura_resolve等纯函数只供显示／工具。协同基础等级与有效等级分开，缺必要加成、武器、宠物或描述函数时标未知，不调用旧执行器。

0x3C选择等待原0x23；0x3B学习等基础等级变化；0x51绑定无即时ACK，重入0x7B恢复16槽，UI使用前8槽。初始职业技能来源于原服装备／选择，不能无条件送技能等级；新角owner=0选择限制见[基线](../../../BASELINE.md)。

## 输入与状态

SceneController统一首次／Hold、目标锁定、释放、失焦及UI消费；RemoteCombat发送原坐标／单位技能请求。普通施法在当前连续位置切换动作，原服省略本人通知时补本人显示；明确位置校正单独处理。

States／Overlay提供原状态、group、hide／shatter／udead及叠层定义，客户端消费A7–AA回包；到期、互斥、属性贡献、反击、冻结／吸收及死亡保留由原服决定。客户端不自己设状态、刷新派生属性或执行周期伤害。

尸体资格分别核对hide、udead与MonStats2.corpseSel；冻结碎裂只使用原冰碎／融化图及公共声音。大小分档、随机变体、创建节拍与全部客户端程序仍缺完整证据，不能以服务端规则推导完整D2Client表现。

## 职业与证据

[女巫](SORCERESS.md)、[亚马逊](AMAZON.md)、[死灵法师](NECROMANCER.md)、[圣骑士](PALADIN.md)维护当前表现／提示与核对入口；没有专题的职业不据此推断完整支持。女巫原表／公式已做过，不从头重做；未完成的是特定协议目标和客户端视觉。

本地D2MOO D2Skills／Skills／PlrModes／PlrMsg／SCmd／D2States提供原流程证据，原图／参数仍读当前MPQ。来源及许可见[资料来源](../../resources/THIRD_PARTY.md)，有限原服观察见[联网记录](../../modules/NETWORK.md#既有有限证据)。
