# 第一幕任务

本页维护第一幕六项任务的服务端执行范围、原身份与核对入口。资格、剧情、奖励和保存由所连接的服务端拥有；两种服务端共用原协议客户端。源码实现与运行认证分开记录。

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

寻找凯恩的原任务号为4，交易工具为3；原面板顺序、图像顺序及保存槽不能混用。牛王限制使用A1Q4相关原位，不把它当凯恩代救位。

## 自研宿主执行

| 任务 | 执行路径 |
| --- | --- |
| 邪恶洞穴 | 接取、洞穴人口清场、在场个人资格、Akara一次性技能点；每难度一次免费重置，与Token共用纯退款函数 |
| 埋骨之地 | Den领奖后开放Kashya接取，也可直接击杀自然血乌；在场个人奖励资格、一次性罗格伙伴；保留已有佣兵（包括死亡记录） |
| 寻找凯恩 | 艾尼弗斯树真实bks落地、拾取、Akara消耗bks并给bkd、每局服务端石柱顺序、原红门60到Tristram、牢笼救援及蓝门59回营地、Akara一次性戒指、Cain免费鉴定；已完成角色的新游戏恢复石柱／红门与牢笼模式；跨幕未救援者按代救位收费 |
| 遗忘之塔 | 古书、塔／地窖进度、自然女伯爵死亡、原InitFn47箱及towerchestspawner开箱／金币时钟；常规首杀TC仍由死亡时冻结的人物记录决定 |
| 交易的工具 | 至少8级接取／领取真实hdm、Charsi消耗马勒斯授予灌注资格、原38加工与58确认；最终阶段与材料／产物同事务提交 |
| 屠戮的姐妹 | Cain接取、地穴进度、自然安达利尔死亡、原定时蓝门、Warriv原0x38目的地40、区域准备成功后完成幕与保存；Warriv2可返回营地 |

领域分工、异步内容准备、事务与消息投影见[任务系统](SYSTEM.md)。第一幕恢复还需核对实际携带物品：树／马勒斯可在新游戏补领丢失的任务物品；石柱顺序属于本局机关，不保存为全局地图种子，点错不会清空已激活柱。

血乌奖励生成真实罗格伙伴；同行、战斗、死亡和保存的范围及未完成服务见[佣兵](../characters/HIRELINGS.md)。

没有队伍权威时，击杀／救援资格只给目标区域当时在场的角色；不推断原版组队共享。管理生成的怪物不授予任务进度；自然身份的管理伤害仍经过死亡领域，仅用于准备冒烟条件。完整欢迎／NPC对白时序、Cain逃离牢笼的行走演出、原五幕剧情、多人争用、三难度及完整原服回归未因本次源码接入获得认证。

## 原规则证据

先迁入master的单机纯阶段规则和奖励／同行者参考，再核对当前MPQ及本地D2MOO A1Q1–6、ObjMode、SUnitNpc、MissMode::SrvDo18。树操作12、石柱9、牢笼10、马勒斯21与箱InitFn47由Objects表准备；戒指品质／等级采用A1Q4回调118的Normal magic7／NM rare30／Hell rare60，属性仍从当前MPQ生成。

原1.13c D2Client的Warriv回调6FAF87B0发送0x38 action0、NPC GUID、目的地40；最后字段由NPC身份解释，不统一当物品GUID。Akara确认6FAF90D0发送相同消息的parameter0，菜单6FAF8F18读取原slot41的RewardPending／RewardGranted；D2Game 6FCE0504的Akara148分支及6FC82340／6FC83BB0分别授予／消费slot41。两端共用客户端修复不含自研专用消息。

存档保持D2S v96，本任务批次构建／运行证据对应规则指纹v24；当前源码指纹见[存档](../../modules/SAVES.md)。slot41完整写入pending／used与PrimaryGoalDone，不能把非零pending字节导入成已使用。旧规则准入不静默迁移。任务阶段细节恢复结合实际携带物品，本局机关／门／顺序与临时AI不写盘。Warriv向东旅行解锁鲁高因传送点的后续源码修正见[传送点](../world/OBJECTS.md#自研传送点)，不据旧任务冒烟认证该新增路径。

## 有限运行证据

2026-10-09完成Windows Release构建、`dist/current`打包及普通难度单人冒烟。临时女巫Hero，地图种子1316597569，MPQ为`assets/mpq2`。使用已有调试管道准备经验、位置、自然怪物死亡及加工输入；交谈、拾取、机关、门户、重置、灌注和NPC旅行由同一客户端发送原C2S。不是一级角色正常通关、首领战斗或组队资格验收。没有新建测试程序、脚本或用例。

证据统一保留在忽略目录`artifacts/act1-quests-20261009`，失败样本也保留。构建和包身份见[基线](../../../BASELINE.md#当前运行包与有限冒烟)。

| 路径 | 实际观察与证据 |
| --- | --- |
| 邪恶洞穴 | 真实洞穴46个人口清零，原Akara76给一次技能点；学技后原重置退款，Den flags=1。`den-entered`／`den-cleared`／`den-rewarded`／`den-respec` |
| 血乌奖励 | 自然血乌死亡后原Kashya92，记录class271／level3／life45／merc02／sourceRow12；真实owned roguehire出生并同行。`burial-rewarded`／`cain-rescued`／`final-reloaded` |
| 凯恩机关与救援 | 原树取bks、原拾取与Akara112换bkd；原0x50五柱顺序19、21、17、20、18，两端一致。原交互打开红门60到38，牢笼生成蓝门59回营地。`cain-translated`／`cain-stones`／`cain-portal`／`cain-rescued` |
| 凯恩服务 | Akara118给真实7级Magic戒指，Cain城镇单位实际出现；金币0时原identify-all仍成功。`cain-rewarded-before-npc`／`cain-identify`。前一文件保留修复Town Cain准备前的样本，不认证当时NPC出生 |
| 遗忘之塔 | 自然The Countess死亡，InitFn47箱时钟与后续金币实际生成；常规TC同时产出符文。`tower-reward-final`；这段推进人物死亡，按原复活回城后继续其余任务 |
| 交易工具 | 原马勒斯生成／拾取hdm、Charsi163消费授予资格；真实白cap放Cursor，原38／58加工后Rare品质6、level13、identified、背包位置。`malus-returned`／`imbue-final`／`imbue-client` |
| 安达利尔与跨幕 | 自然Andy死亡，原定时蓝门回营地；原Warriv旅行1→40→1，Andy阶段5及completedActs[0][0]=true。`andariel-slain`／`andariel-portal`／`warriv-east`／`warriv-west` |
| 新进程保存恢复 | 最终包新进程恢复六阶段4、4、8、4、6、5、重置used位、幕完成位、真实罗格记录和Rare已鉴定cap。五柱mode2及红门实际恢复，原交互进入Tristram，牢笼原mode5。`final-reloaded`／`final-reloaded-items`／`final-restored-cairn`／`final-completed-tristram` |
| 重复资格与诊断 | 重入Akara无重复奖励或重置选项，原重置请求被客户端明确拒绝；六条原5D状态13。最终ProtocolReady、ignoredPackets=0、宿主failures=0、characterIssues为空、stderr为空。`final-akara-no-repeat`／`final-online-status`／`final-server-status` |

上述证据只覆盖列出的普通难度单人路径；其余随机分支、三难度、容量／背压、多人及Linux未认证。旧单机任务冒烟不认证当前联机；原服接取／STARTED／日志恢复的独立证据见[联网记录](../../modules/NETWORK.md#既有有限证据)。
