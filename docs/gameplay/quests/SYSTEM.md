# 任务系统

更新：2026-10-09。本页维护任务的通用所有权、执行入口、原协议和扩展约定；各幕规则、缺口与运行证据分别见[第一幕](ACT1.md)、[第二幕](ACT2.md)、[第三至第五幕](ACT3_5.md)。

## 范围与文档边界

当前自研服务端已接入五幕27项个人任务主流程。客户端可显示27项日志并消费原任务／对白消息，原D2GS和自研宿主使用同一客户端。个人主流程接入不代表完整剧情、组队共享、三难度或正常战斗通关；运行证据只覆盖各幕专题列出的路径。

| 问题 | 负责页面 |
| --- | --- |
| 通用所有权、资格／事务、原包、扩展规范 | 本页 |
| 各任务身份、前置条件、事件／机关、奖励、恢复例外、未完成范围与有限证据 | ACT1、ACT2、ACT3_5 |
| 客户端接口与修改入口 | [NPC／任务模块](../../modules/NPC_QUEST.md) |
| 对白／菜单／提示显示、交谈生命周期 | [NPC交互](../npc/INTERACTIONS.md) |
| 服务请求、加工与经济 | [NPC交易](../npc/TRADE.md)、[库存模块](../../modules/INVENTORY.md) |
| 原D2S任务槽／奖励位、progression及拒绝边界 | [存档](../../modules/SAVES.md#任务槽与奖励语义) |
| 当前规则指纹／运行包、后续实施顺序 | [基线](../../../BASELINE.md)、[总计划](../../architecture/MULTIPLAYER.md) |

## 服务端所有权

| 层／入口 | 职责 |
| --- | --- |
| gameplay/quest | 任务身份与纯阶段／条件函数；不读MPQ、不识别连接、不直接改世界或存档 |
| PlayerStore／CharacterRecord | 唯一的个人任务簿、难度、永久奖励和跨幕资格 |
| [quests/system.hpp](../../../src/server/systems/quests/system.hpp)、state.hpp | 任务领域入口及每实例按幕状态；GameInstance只组装，不实现任务规则 |
| act_one.cpp、act_two*.cpp、staff.cpp、act_three.cpp、act_four.cpp、act_five.cpp、ancients.cpp、baal.cpp | 分幕事件／对白／机关规则，古代人和巴尔控制器；不接管通用实体生命周期 |
| progress.cpp、[rewards.cpp](../../../src/server/systems/quests/rewards.cpp)、preparation.hpp | 冻结目标资格、逐人物提交；有界内容准备、来源复验、材料／奖励安装与完成回执 |
| [hosting/quest_content.cpp](../../../src/hosting/quest_content.cpp)、quest_world_content.cpp、npc_content.cpp／object_content.cpp | 当前MPQ物品／奖励池／佣兵／任务战斗组、对白与机关准备；只交不可变规则与结果，缺规则显式deferred |
| objects／monsters／population、npc／world／travel | 分别拥有物件模式／碰撞、怪物身份／死亡／人口、交谈与中立NPC逃离、世界位置与区域、门户与过渡 |
| merchant／crafting／companions、transactions／persistence | 服务／加工／实际同行者；材料、产物、人物奖励与任务资格原子提交，宿主保存原D2S |

实例清场、目标资格集合、石柱顺序、机关时钟、波次、准备token与输出重试不写盘。新游戏依据个人原任务位、实际材料和当前地图恢复规则允许的机关，不恢复旧实例世界；按难度存档语义见存档页。

## 资格、准备与提交

一般目标资格捕获事件发生时同区域的已入局角色；不等同原版队伍／邻区／距离传播。古代人另在开战冻结满足等级的活人，领奖再次复验在场与存活。管理员生成的怪物不计自然目标；管理员伤害真实自然目标仍经过正常死亡领域，只用于准备冒烟条件。缺失或未准入的人口不能被当成已经清场，中立单位不能替换为敌人。

内容准备绑定玩家／actor、来源区域及代次、来源GUID、token、库存版本和交谈revision。安装复验入局、存活、区域、等级、实际材料／奖励资格及来源；使用当前人物／库存revision提交。容量不足保留待处理项，提交成功才消费材料／资格和发布结果，不能空发奖励或重复领取。库存版本变动会使旧准备失效；普通人物revision变化仍需按当前语义复验，不能一律取消有效准备。

关闭／重开交谈、换区、死亡或离线使相关未提交准备失效。NPC对白确认仅在领域成功后清除待确认；目的地尚未准备好的NPC任务确认可重开交谈重试原消息，不能把客户端“已发送”当作已发奖。第二幕日志确认有独立的有界等待／固定步重试，见ACT2。NPC旅行允许已接受请求后原客户端关闭交谈，仍复验来源、资格与新交谈身份，见[内核旅行](../../modules/SERVER_SYSTEMS.md#任务与旅行)。

## 原协议与客户端投影

客户端只显示服务端已知的本人资格，不执行任务转换、认定目标、发奖或读取D2S。旧Local任务适配和客户端会话级任务执行器已删除；服务端与客户端不共享隐藏世界真值。

| 方向／消息 | 用途与边界 |
| --- | --- |
| C→S 0x40 | 每局ProtocolReady后一次日志请求 |
| S→C 0x28／0x29 | 本人私有48项原任务字／本局公共任务字；独立保存，公共字不授予个人资格。自研29仅入局初始化，完整本局共享语义未实现 |
| S→C 0x52／0x5D | 客户端消费全量／增量日志；自研当前按五幕生产0x28与0x5D，52生产者仍为stub。5D链编号为nativeSlot减从0起的幕索引，不是面板槽或D2S槽 |
| S→C 0x50 | 原特殊进度：任务号1含洞穴剩余、真墓偏移、囚犯剩余；任务号4为凯恩五柱。不是日志位置 |
| S→C 0x27／0x8A | 对白／NPC提示，0x27保留来源unitType（NPC为1，机关为2）；提示不授权领奖 |
| C→S 0x31／0x38 | 确认实际原stringId／原NPC服务与旅行；请求成功发送不表示任务完成 |
| S→C 0x58、C→S 0x44 | 原NPC加工结果或插杖交互租约、原17字节插杖提交／取消；由对应来源解释 |
| S→C 0x53 | 第二幕日食环境，独立于个人任务字 |

本人原任务事实 → QuestProjectionInput → projectQuestDisplay → QuestView → 共用quest_panel／controller。[catalog.hpp](../../../src/gameplay/quest/catalog.hpp)分别声明内部ID、幕、面板槽、原幕内任务号、图像槽及D2S槽；各幕身份表维护实际映射。第四幕只有三项，不用其他幕的六项布局补槽。

标题、qstsa／qsta描述、完成文字、newquestlog及原图取当前MPQ。未知细分状态、真墓图案或剩余数保持未知，不从种子、公共字、地图人口或旧单机流程补答案。已完成条目仍可查看；首次未知→已知只建立动画基线，之后变化驱动完成动画／通知，不重播历史完成。

## 核对与后续扩展

先参考master任务纯规则与旧单机执行，再核对当前MPQ及本地D2MOO Quests、D2QuestRecord、各幕回调、ObjMode、SUnitNpc与PlrSave2。D2Common记录解释可共享；D2Game目标认定、世界资格、随机奖励与发奖属于服务端。公共层依据见[参考设计](../../architecture/REFERENCE_DESIGN.md#9-npc怪物任务与物品怎样拆)，来源／许可见[资料来源](../../resources/THIRD_PARTY.md)。

每项扩展需声明个人位／本局位、难度及前置、真实事件身份、材料／奖励事务、保存恢复、原包生产／消费和未完成范围。若客户端对原D2GS也缺功能，先核实原协议；有原版依据的修复两端共用，禁止自研专用消息或成功fallback。代码接入、构建和实机验证分别记录；队伍、牛王资格与扩展事件的实施顺序归总计划，不在本页另建待办副本。
