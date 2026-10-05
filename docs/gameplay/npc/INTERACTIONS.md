# NPC 交互规则

第一至三幕雇佣服务分别由 Kashya、Greiz、Asheara 提供，原候选与技能成长见[佣兵](../characters/HIRELINGS.md)。原雇佣 NPC 也可复活已有死亡佣兵。

本页负责原对白、身份、交互资格和路径行为。接口／投影与任务协调归 [NPC／任务模块](../../modules/NPC_QUEST.md)，任务推进归 [任务系统](../quests/SYSTEM.md)，报价与交易归 [NPC 交易](TRADE.md)，佣兵归 [佣兵](../characters/HIRELINGS.md)。

## 原文与身份绑定

- 五幕原 `a1npc–a5npc` 文本读取 NAME／Name、SPEED、wave 和正文；wave 按 `Sounds.FileName / Sound` 关联 `MonStats.Id / NameStr`，TBL 提供姓名，原语音路径提供幕别。
- 职业介绍与闲聊按实际原记录索引，不维护 Greiz／Griez、CainAct2 等姓名别名；同名跨幕 NPC 不串用话题。没有独立 Intro 的单位只提供实际存在的 Gossip。
- 原 DS1 中立单位通过 `MonPreset → MonStats / MonStats2` 加载，姓名与服务不由外观 token 推断。未实现的中立单位不替换成敌对怪物。
- 原 D2S 初见位不是 MPQ 字段：D2MOO `PLAYER/PlrIntro.cpp` 的 npcIndexMap 规定位 1–34。幕别优先 MonPreset.Act；cain5 无预设、tyrael1 预设幕别与初见幕别不同，保留原引擎这两处例外，不按读取顺序重新编号。

## 交谈与提示

可点击资格取 `MonStats.interact`；部分营地 rogue 单位虽然 npc=1，interact=0，仍不可交谈。治疗、鉴定及菜单按可信原身份选择服务。

初次接触使用当前难度／职业的原介绍，有自动任务对白时随后播放；后续无新消息显示服务菜单。Talk 提供实际可用的 Introduction、任务回顾和 Gossip。重播不推进初见／任务状态；交谈中隐藏头顶提示，结束菜单不自动重开。

头顶感叹号通过 `npcQuestAlert` 查询，以分配、携带任务物品、待领奖与未读反应决定；原图取 `Overlay.txt/npcalert`，高度取 `MonStats2.OverlayHeight`。普通介绍按难度保存；部分第一幕完成反应仅本局保存，读档不重放。各幕提示资格归各幕 QuestModule，不由 UI 按名字推算。

## 原路径与项目适配

DS1 v14+ 的 NPC 路径记录解码后，以原 MonStats AI／速度选择移动单位。当前仅动作 1／3、距出生点不超过 8 子格且可达的节点参与移动；最多 12 节点，结束等待 120 帧，交谈时停步。位置与路线只在本局保留，加载角色重新建立。

66% 选点概率、12 次动作预算、25 FPS 节拍和 Velocity/16 固定点转换参考 D2MOO；8 子格半径、完整 120 帧停顿和随机流是项目适配，不能视为完整原 AI。实现入口及区域推进见 [地图模块](../../modules/MAP.md) 和 [NPC／任务模块](../../modules/NPC_QUEST.md)。

## 表现与限制

字幕使用当前 MPQ `FontFormal12`，第一行打开时在顶部显示，随后连续滚动；滚轮暂停自动滚动，点击／Esc 结束。NPC 附近菜单及字幕框、约 2.2 秒一行的节奏是适配，依据原版截图和本地客户端参考核对。对白音频引用已导入，语音播放／同步、自然结束、多消息逐段确认与提示重播时序尚未完整实现。

五幕加载与通用聊天不代表所有 NPC 专属服务可用；任务、交易、雇佣、复活等限制由各自专题维护。源码／包状态见 [项目基线](../../../BASELINE.md)，历史冒烟只覆盖原批次，不认证当前全部交互。
