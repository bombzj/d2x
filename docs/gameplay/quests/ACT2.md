# 第二幕任务

本页维护保留的原身份／槽映射及核对入口；不是已通关或当前联机执行清单。显示链与未知状态见[任务系统](SYSTEM.md)，资格、剧情、奖励和保存由原服。

## 原任务身份

| 任务 | 内部ID | 原幕内任务号 | 面板槽（从0起） | 图像槽 | 原记录槽 |
| --- | --- | --- | --- | --- | --- |
| 罗达门特的巢穴 | RadamentsLair | A2Q1 | 0 | 6 | 9 |
| 赫拉迪克法杖 | HoradricStaff | A2Q2 | 1 | 7 | 10 |
| 堕落的太阳 | TaintedSun | A2Q3 | 2 | 8 | 11 |
| 神秘避难所 | ArcaneSanctuary | A2Q4 | 3 | 9 | 12 |
| 召唤者 | Summoner | A2Q5 | 4 | 10 | 13 |
| 七座古墓 | SevenTombs | A2Q6 | 5 | 11 | 14 |

## 边界与依据

A2Q0杰海因欢迎用原槽8位0，独立于六项日志和普通初见。法杖材料／插杖、真墓符号、日蚀、宫殿／都瑞尔旅行资格仍未完整接入；无原服生产者的旧插杖面板已删除。不能用地图种子补真墓符号。

映射来自当前gameplay/quest/catalog.hpp及persistence/d2s_quests，原文字／身份／图像由当前MPQ读取。原回包任务编号与记录槽分别解码，不能按面板位置猜包号。客户端不推进本地任务阶段。

本地D2MOO固定版本的Quests／D2QuestRecord及QUESTS/ACT2任务回调、ObjMode和SUnitNpc是核对入口；OpenDiablo2 quest_log只作原布局／文字交叉参考。来源及许可见[资料来源](../../resources/THIRD_PARTY.md)。

旧单机逐项任务冒烟已退场，不认证联机。当前只有有限接取／STARTED／日志恢复证据，完整任务完成／奖励、三难度及多人共享尚未认证，见[联网记录](../../modules/NETWORK.md#既有有限证据)。
