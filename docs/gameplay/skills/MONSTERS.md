# 怪物、首领与精英技能

更新：2026-10-09。本页负责非玩家敌对单位的技能入口与覆盖台账。身份、AI、人口、动作、数据契约及有限运行证据由[怪物目录](../monsters/README.md)链接负责页维护；本页不复制其完整实现报告。第一幕已有实际程序，不能统一写成未实现，也不能据此认定其他幕已完成。

## 当前执行家族

| 使用者／能力 | 当前自研入口与边界 |
| --- | --- |
| 沉沦魔萨满 | 本族尸体复活、按BaseId链选择火弹、普通近战；不开放任意尸体复活 |
| 鸟巢 | 原序列出生、数量／间隔及耗尽死亡；巢子不重复给予经验／掉落 |
| 蜘蛛 | SpiderLay轨迹、蛛网／地面减速；生命周期与效果分开持有 |
| 吸血鬼 | 当前vampire5的火球、FireHead；普通AI表未开启Firewall／Meteor，不能因存在技能槽声明已执行 |
| 安达利尔 | AndyPoisonBolt及AndrialSpray多次释放；毒伤、方向与原Clt／Srv偏移分开处理 |
| 血乌 | Quick Strike序列及zombie2召唤；普通弓箭与专用召唤分别准备 |
| 伯爵夫人 | 真实DS1路径节点上的FirewallMaker／地面火；缺节点不自造落点 |
| GargoyleTrap | 原序列发射、距离／轴向限制；是可击杀单位型陷阱，不是刺客陷阱或普通物件 |
| 精英主动／周期效果 | Amplify Damage、友方属性光环、Holy Fire／Freeze／Shock脉冲、低生命Teleport；只是精英来源入口，不表示对应玩家职业全部可用 |
| 精英受击／死亡效果 | 电强化延时／冷却与充能弹、冰／火死亡事件等由独立效果时钟处理；并非每项都是可施放Skills行 |

普通箭矢、尖刺、骷髅法师元素弹、近战与元素附伤已有第一幕程序，但A1／A2或MissA1／A2不一定对应独立Skills行。SkeletonRaise槽是复活动画，不归普通AI主动；完整关系与限制见[第一幕](../monsters/ACT1.md)。其他幕怪物／首领程序尚未全面迁入，任务死亡链和身份替身不能认证其战斗技能。

## 代码与归档

[monster_actions_content.cpp](../../../src/hosting/monster_actions_content.cpp)按当前MonStats技能槽、Skills、Missiles、MonSeq及难度准备规则；[skills/monster_actions.cpp](../../../src/server/systems/skills/monster_actions.cpp)持开始／释放队列，monsters执行复活／出生／移动，missiles／effects执行弹体和效果。AI只提交窄请求，不另建技能执行器。精英程序与所有权见[精英](../monsters/ELITES.md)与[首领](../monsters/BOSSES.md)。

已有公共方向／路径计算及原Clt／Srv差异沿[技能COMMON](COMMON.md)和[怪物COMMON](../monsters/COMMON.md)维护。怪物调用职业技能时链接职业页，怪物专用程序才在本页登记算法；与物件、任务死亡演出、伙伴能力的分类见[目录](README.md)。

后续逐项补当前MPQ技能ID／原名、实际使用槽与AI条件、Clt／Srv程序、共享函数、执行和证据。当前表为代码执行家族摘要，不是全部非玩家Skills行清单；未归档行先列入[SPECIAL](SPECIAL.md)。历史代表路径有限V2及未认证分支以[第一幕证据](../monsters/ACT1.md#有限运行证据)为准。
