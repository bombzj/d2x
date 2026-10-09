# 第二幕任务

更新：2026-10-09。本页维护第二幕六任务的服务端执行、原身份／槽映射及限制。资格、剧情、奖励和保存归服务端，共用客户端只消费原消息。通用边界见[任务系统](SYSTEM.md)。

## 原任务身份

| 任务 | 内部ID | 原幕内任务号 | 面板槽（从0起） | 图像槽 | 原记录槽 |
| --- | --- | --- | --- | --- | --- |
| 罗达门特的巢穴 | RadamentsLair | A2Q1 | 0 | 6 | 9 |
| 赫拉迪克法杖 | HoradricStaff | A2Q2 | 1 | 7 | 10 |
| 堕落的太阳 | TaintedSun | A2Q3 | 2 | 8 | 11 |
| 神秘避难所 | ArcaneSanctuary | A2Q4 | 3 | 9 | 12 |
| 召唤者 | Summoner | A2Q5 | 4 | 10 | 13 |
| 七座古墓 | SevenTombs | A2Q6 | 5 | 11 | 14 |

## 执行与依赖

A2Q0杰海因欢迎使用原槽8位0，独立于六项日志和普通初见。执行入口为`act_two.cpp`、`act_two_dialogue.cpp`、`act_two_objects.cpp`与`staff.cpp`；资格、领域所有权与事务约定见[任务系统](SYSTEM.md)。

| 任务 | 已接入主流程 |
| --- | --- |
| 罗达门特 | Atma接取／返城确认；自然Radament死亡冻结资格，原MPQ技能书在死亡位置准备、落地、拾取和消费；永久技能点与一次性使用位沿既有事务保存 |
| 赫拉迪克法杖 | 原operation40卷轴箱、39方块箱、41杖身箱及24祭坛护符；当前MPQ物品与配方；Cain说明并消耗卷轴；原0x58开启插杖、0x44提交／取消；复验真实Cursor法杖、难度、距离、实际真墓和租约，材料删除与个人提交位原子提交 |
| 堕落的太阳 | 蛇谷／利爪蝮蛇神殿触发原20帧节奏的日食时钟；Drognan说明；破祭坛、材料掉落、恢复环境与城镇确认 |
| 神秘避难所 | Drognan／Jerhyn前置、宫殿门禁与operation34双向旅行；Horazon日志原对象消息396，原0x31确认才提交阅读、真墓符号与峡谷红门；城镇完成评论保留待确认位 |
| 召唤者 | 自然Summoner进入活动范围后接取，死亡冻结资格，原城镇参与者确认；调试生成的同名怪物不计目标 |
| 七座古墓 | 实际生成器真墓、插杖定时生成原class100入口与开墙碰撞；operation43原出生标记到都瑞尔巢穴；自然Duriel死亡、Tyrael蓝门、Jerhyn／Meshif确认及40↔75旅行；完成幕与库拉斯特传送点在旅行事务中提交 |

任务箱按本局已入局人物的材料资格计数；没有材料资格仍执行原普通魔法箱TC和金币掉落。物品身份、属性、等级、TC、碰撞、图形与动画由hosting从当前MPQ准备。原回调定义的类／操作号、对话号和坐标偏移是原代码规则。插杖延迟取原Missiles行338 Range，原Objects动画决定入口何时可通行。材料查找沿原ITEMS_FindQuestItem，包含Cursor／背包／方块／私人箱及装备，排除尸体／孔内等非携带来源，并复验任务物品难度；可重复使用的方块不受该难度检查。

机关旅行沿DrlgDrlgWarp的原TileInfo：tile0匹配入口0–4；没有匹配时使用第一项真实标记。当前Duriel.ds1的唯一标记是style30／sequence11，operation43请求tile0，正好走该原版分支。Position=0仍走原房间／传送点规则；缺资源或实际标记明确拒绝，不自造落点。杰海因由原InitFn18／19标记准备，按实例任务状态只发布对应位置。

## 原包、共享地图与保存

自研宿主通过原0x28／0x5D发布任务字与细分日志，0x50任务号1发布真墓偏移，0x53发布日食环境。符号只来自服务端已发布的阅读资格，不在客户端从种子补答案。Cain原335卷轴、336护符、337杖身、338方块、339成杖，首次说明使用原menu0。

客户端补齐此前对原D2GS也缺失的0x53、对象type2滚动消息、0x58插杖租约与原17字节0x44。面板复用master的`horadricback.dc6`／`okcancelbtn.dc6`和原布局，Cursor物品仍由服务端拥有。class100 mode2调用两端共享Map开墙函数；原DT1实例标记移除而不重排索引，碰撞修改包含完整标志。没有自研专用协议或连接分支。

日志确认等待区域准备时由服务端保留有界请求，固定步复验人物、区域代次、存活、对象来源与交互几何后重试；不要求客户端重复发送已确认文本。插杖开启和租约清理使用同一交互几何。对象发起的旅行由travel绑定移动序号，后续移动／死亡／换区会使等待失效。

新游戏按个人插杖提交资格及当前真实墓穴恢复class100入口。成杖说明、日志待确认与日食等状态的原编码／不保存边界及当前规则指纹统一见[存档](../../modules/SAVES.md#任务槽与奖励语义)；下节v26只是历史证据版本。

## 依据与限制

先参考master的第二幕任务、物件、插杖UI和旅行，再核对本地D2MOO `QUESTS/ACT2/A2Q0..6`、ObjMode操作24／25／34／39–43、PlrMsg原44、D2PacketDef、D2QuestRecord及当前MPQ。纯记录／地图算法可以共享，目标认定、资格和发奖只归服务端，见[任务系统](SYSTEM.md#核对与后续扩展)。

第二幕完整怪物AI未迁入；不支持的敌对类型使用唯一授权的沉沦魔替身并保留真实身份，不能因此宣称都瑞尔等首领战斗完成。队伍跨区域传播、晚加入资格、完整城镇移动／剧情时序、丢失全部法杖材料后的实例计数细分状态和插杖特效仍待补齐。旧单机与第一幕证据不认证当前第二幕，更不认证三难度、多人、原服或Linux。

## 有限运行证据

2026-10-09历史v26包完成Windows Release构建（`build-09.log`），EXE SHA256为`6F249024C009CEF71BAA684E96E83B5B13F4FB0A775ACE2CB2B6AF3E5CF10D86`。它不是[当前运行包](../../../BASELINE.md#当前运行包与有限冒烟)。忽略目录`artifacts/act2-quests-20261009`保留构建／运行日志、快照、截图和临时存档；未新增测试脚本、用例或专用程序。

同日重新构建确认无待编译内容并重新打包，同一EXE的独立复验保存在`artifacts/act2-recheck-20261009`。从材料检查点副本启动，实际重做Cain339（说明位15→31）、日志确认等待与红门往返、插杖取消／提交、开墓／进入巢穴、Tyrael救援／蓝门、Jerhyn／Meshif交接与跨幕往返；新进程恢复六任务阶段4／6／4／4／3／5、说明位31、技能点84和已消耗材料，并再次恢复class100 mode2入口。重载时ProtocolReady、ignoredPackets=0、unavailableUnits=0、mapErrors为空、宿主failures=0，两次stderr为空。复验未重新获取技能书／材料或重做日食，这些只由下表的前次样本覆盖。具体快照为`cain-assembled`、`journal-read`、`staff-submitted`、`tomb-open`、`tyrael-rescued`、`meshif-east`、`saved-snapshot`、`reloaded-snapshot`／`reloaded-online`及`reloaded-tomb`。

普通难度临时女巫Hero，地图种子1144830459，实际真墓70／偏移4。经验、位置和自然怪物死亡由既有管理命令准备，交谈、拾取、消费、方块、插杖、机关及门户旅行发送原online请求。怪物使用授权替身，不能作为正常战斗证据；人物未走第一幕正常通关前置。以下是有限单人路径，其他分支仍未认证：

| 实际路径 | 观察与证据 |
| --- | --- |
| 罗达门特 | 自然目标死亡掉原技能书；拾取消费后余技能点83→84，Atma334确认。最终stage4／Used=2；`radament-book-scroll`、`atma-cain-reward`、`final-reloaded-snapshot` |
| 材料与合成 | 原卷轴箱／方块箱／杖身箱／祭坛掉落；Cain335消耗卷轴、336–338说明；原方块配方消耗msf／vip生成hst。`cube-drop`、`shaft-drop`、`staff-assembled` |
| 日食 | 蛇谷触发、Drognan348、祭坛破坏和371确认；服务端与原0x53客户端环境均恢复。最终stage4；`eclipse-active`、`eclipse-status`、`altar-destroyed` |
| 日志与召唤者 | 自然Summoner死亡、396阅读、原0x50偏移4；第一次确认等待峡谷准备后自动建立红门，不二次确认，红门74↔46及operation34的74↔54实际往返；城镇427／406清除待确认。`journal-retry-final`、`red-portal-canyon`、`arcane-cellar-warp` |
| 插杖／墓穴 | 原0x58打开原图面板、0x44取消／重开／提交，hst删除与stage6一起提交，结果5；延迟后class100 mode2、共享开墙与operation43进入73；保存重入再次恢复入口。`orifice.png`、`staff-submitted`、`tomb-open`、`duriel-before-final` |
| 救援与跨幕 | 自然Duriel死亡、Tyrael302及蓝门73→40、Jerhyn253／442、Meshif450后40→75并由Meshif2返回40；跨幕完成位及75传送点随事务提交。`tyrael-rescued`、`meshif-sailed`、`six-quests-confirmed` |
| 保存恢复 | 新进程恢复六任务阶段4／6／4／4／3／5、技能书Used、余技能点84、方块和跨幕位；已消耗的材料／法杖不再出现，待确认位不复活。最终宿主failures=0、characterIssues为空、客户端ignoredPackets=0。`final-saved-snapshot`、`final-reloaded-snapshot`、`final-reloaded-online` |

冒烟修复了杰海因标记遗漏、日志确认准备等待、插杖租约距离不一致、巢穴入口标记选择及对象旅行移动序号绑定。早期失败样本与旧包快照仍保留，只以上述成功样本认证对应路径。等待开墙期间曾发生死亡并用原复活请求回城，未把该过程作为首领战斗或完整死亡系统认证。
