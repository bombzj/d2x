# 任务系统与日志

更新：2026-10-09。任务进度、资格、剧情、奖励和存档由所连接的服务端拥有。原D2GS与自研宿主共用原协议客户端；客户端不执行任务转换、发奖或读取D2S。旧Local任务适配和会话级权威执行已退场，保留／迁入的纯阶段函数由服务端调用。

## 当前支持范围

| 范围 | 当前状态 | 负责页面 |
| --- | --- | --- |
| 五幕27项日志 | 身份映射、原任务记录／日志消息消费、公共MPQ面板及通知；未知细分状态保持未知 | 本页与各幕身份表 |
| 第一幕六任务 | 个人阶段、自然目标事件、任务物品／机关、一次性奖励、城镇服务及Warriv旅行已执行；普通单人主流程有有限v24运行证据 | [第一幕](ACT1.md) |
| 第二幕 | 身份、日志和原保存语义保留，已有任务物品消费／组装入口；完整任务事件／前置资格未迁入 | [第二幕](ACT2.md) |
| 第三至第五幕 | 身份、日志、原保存值及部分通用物品消费／加工接口保留；自研剧情与奖励资格生产者未迁入 | [第三至第五幕](ACT3_5.md) |
| 组队与扩展事件 | 队伍任务传播、完整多人贡献、欢迎／剧情时序、牛王资格写入及扩展任务仍有缺口 | [总计划P3／P4／P6–P9](../../architecture/MULTIPLAYER.md) |

已有任务记录或加工接口不代表相应任务可从头完成。原服接取／STARTED／日志恢复的历史观察只认证列出的原服路径，不认证自研完成或旧单机实现；原服证据见[联网记录](../../modules/NETWORK.md#既有有限证据)。

## 服务端所有权与执行入口

| 层／入口 | 所有权与职责 |
| --- | --- |
| gameplay/quest各任务纯函数 | 阶段转移与一次性领取条件；不读MPQ、不识别连接、不直接改世界或存档 |
| [quests/system.hpp](../../../src/server/systems/quests/system.hpp)、system.cpp／act_one.cpp | 实例清场／目标事件、在场资格快照、个人观察、石柱顺序、准备token和重试；Refresh／Acknowledge／ClaimReward／ClaimRespec、物件事件与固定步入口 |
| [quests/rewards.cpp](../../../src/server/systems/quests/rewards.cpp)、preparation.hpp | 有界任务内容准备、交谈／物件来源复验、奖励安装与完成回执；个人持久任务簿只有PlayerStore一份 |
| [hosting/quest_content.cpp](../../../src/hosting/quest_content.cpp) | 当前MPQ任务物品、戒指与罗格奖励内容准备；缺规则返回明确deferred，不编造奖励 |
| objects／monsters／population | 真实机关身份、模式／碰撞／时钟与自然怪物身份／死亡／准入统计；quests不接管实体生命周期 |
| npc／merchant／crafting／companions／travel | 分别管理交谈租约、货架与服务、物品加工、实际佣兵、门户与换区；任务资格通过窄入口协作 |
| transactions／persistence | InventoryEdit／CharacterEdit原子提交个人任务字、材料／产物、成长／佣兵／幕完成；宿主沿原D2S保存，运行世界不写盘 |

内容准备绑定玩家／actor、区域代次、来源GUID、token、库存版本与交谈revision；安装再次核对活人、入局、区域、等级、库存及任务条件，提交使用当前人物／库存revision。容量不足保留待处理项，不能用空奖励宣称成功。关闭／重开交谈、换区、死亡或离线清理相关未提交准备；具体物件与NPC旅行例外见ACT1和[内核](../../modules/SERVER_SYSTEMS.md#第一幕任务与旅行)。

个人记录按难度持久化；本局清场、目标事件、在场集合、石柱顺序、机关／门户、时钟、准备token和输出重试属于实例运行态。已完成角色新游戏中的机关恢复按相应任务规则处理，不能保存整张任务世界或在客户端补推进。格式、原槽与当前规则指纹由[存档](../../modules/SAVES.md#任务槽与奖励语义)维护。

## 唯一显示链

原0x28／0x52／0x5D／0x50 → 本人原协议事实 → QuestProjectionInput → projectQuestDisplay → QuestView → 公共quest_panel／controller。每局ProtocolReady后一次原0x40请求日志；0x29本局公共任务字独立保存，不替代本人资格。

| 消息 | 消费边界 |
| --- | --- |
| 0x28／0x29 | 本人私有48项任务字／本局公共任务字；分开保存，不能混用 |
| 0x52／0x5D | 全量／增量日志状态与原记录编号；邪恶洞穴剩余数仅消费原附加字段 |
| 0x50 | 带原幕内任务号的特殊进度，含凯恩五柱顺序／原符号等；不按日志位置解释 |
| 0x27／0x8A | NPC对白／提示，与日志、奖励和服务资格分开；提示不能授权领奖 |
| 0x31／0x38／0x58 | 实际对白确认、原NPC服务请求与加工结果；确认或发送成功不是客户端发奖凭据 |

[quest/catalog.hpp](../../../src/gameplay/quest/catalog.hpp)分别声明内部ID、幕、面板槽、原幕内任务号、图像槽与D2S记录槽。27项按实际幕列表显示，第四幕只有三项；各幕身份表不重复通用规则。自研native_quest_wire从已提交个人记录投影第一幕28／5D和凯恩50，客户端不读取内部stage或推算宿主任务状态。

标题、qstsa／qsta描述、qstsComplete／ThankYou、newquestlog及原图取当前MPQ。缺细分状态、真墓图案或剩余数保持未知，不从种子、公共字、地图人口或旧单机执行器补齐。已完成条目仍可查看；首次未知→已知只建立动画基线，之后变化驱动完成动画与通知，不重播历史完成记录。NPC对白、菜单及清理规则只维护在[NPC交互](../npc/INTERACTIONS.md)，接口索引见[NPC／任务模块](../../modules/NPC_QUEST.md)。

## 核对与后续扩展

先参考master各任务纯规则、奖励与旧执行流程，再核对当前MPQ及本地D2MOO Quests、D2QuestRecord、各幕回调、ObjMode、SUnitNpc与PlrSave2。D2Common任务记录解释可共享，D2Game事件认定、随机奖励、世界资格与发奖只归服务端；不为公共函数提取而让客户端获取隐藏世界或完整存档。来源及许可见[资料来源](../../resources/THIRD_PARTY.md)，公共层边界见[参考设计](../../architecture/REFERENCE_DESIGN.md#9-npc怪物任务与物品怎样拆)。OpenDiablo2 quest_log仅交叉核对布局与文字。

后续每项需同时声明个人位／本局位、难度与前置条件、事件身份、奖励与材料事务、保存重入、原包生产／消费及明确未完成范围。若客户端原D2GS功能缺失，先核实原协议和消费；有原版依据的修复两端共用，不增加自研专用消息或成功fallback。运行认证与源码分开，未构建源码不沿用旧包证据。
