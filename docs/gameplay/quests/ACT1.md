# 第一幕任务

本页维护保留的原身份／槽映射及核对入口；不是已通关或当前联机执行清单。显示链与未知状态见[任务系统](SYSTEM.md)，资格、剧情、奖励和保存由原服。

## 原任务身份

| 任务 | 内部ID | 原幕内任务号 | 面板槽（从0起） | 图像槽 | 原记录槽 |
| --- | --- | --- | --- | --- | --- |
| 邪恶洞穴 | DenOfEvil | A1Q1 | 0 | 0 | 1 |
| 埋骨之地 | SistersBurialGrounds | A1Q2 | 1 | 1 | 2 |
| 寻找凯恩 | SearchForCain | A1Q4 | 2 | 3 | 4 |
| 遗忘之塔 | ForgottenTower | A1Q5 | 3 | 4 | 5 |
| 交易的工具 | ToolsOfTheTrade | A1Q3 | 4 | 2 | 3 |
| 屠戮的姐妹 | SistersToTheSlaughter | A1Q6 | 5 | 5 | 6 |

## 边界与依据

寻找凯恩的原任务号为4，交易工具为3；原面板顺序、图像顺序及保存槽不能混用。牛王限制使用A1Q4相关原位，不把它当凯恩代救位。完整石柱顺序、Tristram门户、首杀／领奖和Akara重置仍需原服闭环。

映射来自当前gameplay/quest/catalog.hpp及persistence/d2s_quests，原文字／身份／图像由当前MPQ读取。原回包任务编号与记录槽分别解码，不能按面板位置猜包号。客户端不推进本地任务阶段。

本地D2MOO固定版本的Quests／D2QuestRecord及QUESTS/ACT1任务回调、ObjMode和SUnitNpc是核对入口；OpenDiablo2 quest_log只作原布局／文字交叉参考。来源及许可见[资料来源](../../resources/THIRD_PARTY.md)。

旧单机逐项任务冒烟已退场，不认证联机。当前只有有限接取／STARTED／日志恢复证据，完整任务完成／奖励、三难度及多人共享尚未认证，见[联网记录](../../modules/NETWORK.md#既有有限证据)。
