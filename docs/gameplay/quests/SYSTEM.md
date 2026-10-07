# 任务系统与日志

原服拥有五幕任务、资格、剧情、奖励和保存；客户端只投影本人私有任务字／日志状态与原对白。本地任务转换、怪物首杀／奖励协调及Local任务适配已删除。

## 唯一显示链

原0x28／0x52／0x5D／0x50 → 原服本人事实 → QuestProjectionInput → projectQuestDisplay → QuestView → 公共quest_panel／controller。0x29本局公共字独立保存，不替代本人资格；每局ProtocolReady后一次0x40请求日志。

quest/catalog分别声明稳定ID、幕、面板槽、原任务号、图像槽及D2S槽。27项按实际幕列表显示，不能按六项推算第四幕或磁盘槽；以下专题维护身份及原证据：[第一幕](ACT1.md)、[第二幕](ACT2.md)、[第三至第五幕](ACT3_5.md)。

原标题、qstsa／qsta描述、qstsComplete／ThankYou及newquestlog取当前MPQ TBL／原图。缺原细分状态、真墓图案或剩余数保留未知；不会用种子或任务执行器补齐。

## 交互与通知

0x8A NPC提示及0x27原对白独立于日志；自动对白与话题均发0x31确认，确认不代表客户端发奖励。已完成条目可查看说明；首次未知→已知不重放历史完成动画，之后状态变化驱动共用通知。

当前任务服务、旅行资格、五幕剧情／领奖／特殊事件及多人共享规则仍未完成。日志已显示不等于该任务通关；准确观察见[联网记录](../../modules/NETWORK.md#既有有限证据)，原字段和UI边界见[NPC／任务模块](../../modules/NPC_QUEST.md)。

## 核对入口

本地D2MOO Quests／D2QuestRecord／Quests.h／D2Constants、各幕回调、ObjMode与SUnitNpc提供原位与流程依据。OpenDiablo2 quest_log只交叉核对原文字／图像布局；客户端不移植其本地资格或奖励。原D2S保存值与服务端状态不可混用，详见[存档](../../modules/SAVES.md)。
