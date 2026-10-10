# 第三至第五幕任务

本页维护第三至第五幕15项任务的原身份、服务端主流程与执行缺口。个人资格、奖励和保存由服务端拥有；按幕执行器及古代人／巴尔实例控制已接入。共用客户端显示与保存边界见[任务系统](SYSTEM.md)。

## 原任务身份

| 任务 | 内部ID | 原幕内任务号 | 面板槽（从0起） | 图像槽 | 原记录槽 |
| --- | --- | --- | --- | --- | --- |
| 黄金鸟 | GoldenBird | A3Q4 | 0 | 12 | 20 |
| 古代宗教之刃 | BladeOfTheOldReligion | A3Q3 | 1 | 13 | 19 |
| 克林姆的意志 | KhalimsWill | A3Q2 | 2 | 14 | 18 |
| 蓝·依森的古书 | LamEsensTome | A3Q1 | 3 | 15 | 17 |
| 黑暗神殿 | BlackenedTemple | A3Q5 | 4 | 16 | 21 |
| 守护者 | Guardian | A3Q6 | 5 | 17 | 22 |
| 堕落天使 | FallenAngel | A4Q1 | 0 | 18 | 25 |
| 地狱熔炉 | HellsForge | A4Q3 | 1 | 20 | 27 |
| 恐怖的结局 | TerrorsEnd | A4Q2 | 2 | 19 | 26 |
| 哈洛加斯围城 | SiegeOnHarrogath | A5Q1 | 0 | 21 | 35 |
| 亚瑞特山的救援 | RescueOnMountArreat | A5Q2 | 1 | 22 | 36 |
| 冰之囚 | PrisonOfIce | A5Q3 | 2 | 23 | 37 |
| 哈洛加斯的背叛 | BetrayalOfHarrogath | A5Q4 | 3 | 24 | 38 |
| 通道祭典 | RiteOfPassage | A5Q5 | 4 | 25 | 39 |
| 毁灭前夕 | EveOfDestruction | A5Q6 | 5 | 26 | 40 |

## 已接入的个人主流程

| 任务 | 原事件与执行 | 奖励／持久资格 |
| --- | --- | --- |
| 黄金鸟 | 本局合资格原生精英选择、雕像地面掉落、Meshif交换、Cain说明、Alkor收鸟／交药 | 原生命药剂，使用后永久生命与任务记录原子提交 |
| 古代宗教之刃 | MPQ祭坛Operate31、延时任务精英、基德宾掉落／交还 | Ormus稀有戒指与Asheara铁狼资格分别防重；保留已有佣兵 |
| 克林姆的意志 | 三件器官箱、议会连枷／缺方块人数掉落、逐件Cain说明、原方块合成、活动手持意志两次操作宝珠 | 消耗意志并开放憎恨囚牢；个人阶段与世界机关分开 |
| 蓝·依森的古书 | 神庙书架、古书掉落／观察、Alkor交书 | 消耗古书并加5未分配属性点，同笔事务保存 |
| 黑暗神殿 | 三个真实议会超级暗金死亡、Cain确认与宝珠状态 | 个人目标资格，不能用管理员生成怪物冒充 |
| 守护者 | 囚牢阶段、Mephisto自然死亡、灵魂石掉落／NPC补发、原红门 | 第四幕旅行、幕完成与城镇传送点同笔保存 |
| 堕落天使 | Izual死亡、原中立灵魂NPC、Tyrael确认 | 一次性2技能点 |
| 地狱熔炉 | Hephasto锤子、放灵魂石、活动手持锤子三次操作 | 消耗材料／锤子，4宝石与按难度原符文池掉落，Cain确认 |
| 恐怖的结局 | 五封印、三封印头目／仆从、清理剩余原生敌人、Diablo延时生成／死亡 | 原Objects566第五幕门、幕完成／Harrogath传送点 |
| 哈洛加斯围城 | Shenk自然死亡、Larzuk确认 | 原0x38／0x58一次性打孔资格，加工后消费 |
| 亚瑞特山的救援 | 原Init62三组各5中立囚犯、附近牢门死亡、独立NPC寻路逃离／计数 | Qual-Kehk按已救人数给原符文，保留已有佣兵或授予野蛮人 |
| 冰之囚 | 原冻结Anya对象消息、Malah解冻药剂、材料消费、城镇Anya准入 | 原抗性卷轴与职业稀有物品分别防重；卷轴使用加10全抗 |
| 哈洛加斯的背叛 | Anya原红门、Nihlathak原身份准入／死亡、Anya确认 | 原一次性署名资格，实际加工消费 |
| 通道祭典 | 原祭坛／三尊雕像、三古代人准入、开门、开回城门或全员离开／死亡重置 | 20／40／60等级门槛，1.4M／20M／40M经验且不超过一个等级差额；资格按开战在场活人冻结 |
| 毁灭前夕 | 原王座NPC、五波MPQ超级暗金／仆从（第二波额外冰法师）、清场等待、原563门、Baal死亡、Tyrael3／565出口 | 最终个人任务、Baal通关位及原头部progression／难度解锁；没有第五个跨幕完成字 |

执行入口为`act_three.cpp`、`act_four.cpp`、`act_five.cpp`、`ancients.cpp`、`baal.cpp`；hosting的`quest_world_content.cpp`准备战斗组，`quest_content.cpp`准备奖励。原中立NPC只从已准备规则准入。囚犯逃离状态归npc、位置归world，门户归travel；公共资格／事务／原协议与保存边界分别见[任务系统](SYSTEM.md)和[存档](../../modules/SAVES.md#任务槽与奖励语义)。

## 恢复与投影例外

新游戏依据个人任务位与实际材料恢复破珠／Mephisto、解冻安雅与古代人完成机关；五波、囚犯路径和死亡目标不恢复。巴尔结尾使用原565出口和最终任务位／头部progression，没有第五个跨幕完成字。第三幕没有第二幕完成资格时原日志状态不投影为可领取；这不等于客户端补推个人资格。

## 明确缺口与后续条件

- 队伍系统尚未建立，普通目标资格按事件发生时同区域入局玩家捕获；古代人另要求开战／领奖在场存活且满足等级。未复刻原队伍、邻区／距离传播、晚加入／本局已完成提示及全部多人剧情。
- 后三幕全量怪物AI／首领程序尚未迁入，替身允许验证任务事件链，不能据此认证正常战斗通关。
- 牢门保留原身份和静止／不攻击语义，tile生成怪物现已进入公共地图输出；当前战斗／外观数值仍来自已授权替身，不代表原牢门完整数值与演出已迁入。
- 囚犯目前是中立NPC移动实体，缺少完整可受伤／死亡AI；因此囚犯死亡导致13／14人奖励、少于13人失败分支尚未贯通。原囚犯蓝门演出也未实现。
- Anya野外解冻后的NPC逃离／传送演出、完整城镇剧情／NPC迁移时序、熔炉宝石逐批间隔掉落和通关演出仍有缺口；本轮熔炉奖励同笔原子落地。
- 古代人重置按任务组撤销实体，完整专用AI、原随机词缀筛选与召唤／效果清理的全部边界仍需逐项核对。
- 三难度、原D2GS对照、多人／跨机器、Linux及完整存档异常分支没有本轮运行认证。任务主流程接入不等于P8完整战役验收。

## 依据与运行证据

先参考master单机各幕conversation／events／npc、session_quest_rewards、session_later_act_objects、session_prisoners、session_ancients、session_baal，再核对本地D2MOO `QUESTS/ACT3`、`ACT4`、`ACT5`、ObjMode、Quests状态／特殊进度编码与当前MPQ。D2Common记录解释可共享，D2Game权威执行不搬到客户端。来源及共用链见[任务系统](SYSTEM.md)。

2026-10-09全部源码完成后统一Windows Release构建／打包，随后以普通难度84级角色副本通过现有named pipe做有限冒烟；冒烟暴露的问题修复后重新构建并复验。最终`build-14.log`、`package-final.log`和快照位于忽略目录`artifacts/act3-5-quests-20261009`；该历史v27包EXE SHA256 `9440CD86160F5635C2921F14AA37F2D053B75D91D84383C29C46675D96E8F5E4`，不是[当前运行包](../../../BASELINE.md#当前运行包与有限冒烟)。

| 范围 | 实际结果与代表证据 |
| --- | --- |
| 第三幕 | 原书架／Alkor交书加5属性；议会死亡触发雕像／连枷，Meshif／Alkor生命药剂使用加20生命；基德宾祭坛／任务精英／Ormus戒指／Asheara铁狼分别领完；持意志两次破珠、Mephisto死亡与原342门进入第四幕。`tome-rewarded.json`、`bird-potion-fixed.json`、`gidbinn-rewarded.json`、`orb-smashed.json`、`guardian-passage.json` |
| 第四幕 | Izual死亡与Tyrael加2技能；Hephasto原锤、放石／三次锤击、4宝石及1符文、Cain确认；五封印／三头目触发Diablo，Tyrael566门进入第五幕。`izual-slain.json`、`forge-smashed.json`、`diablo-slain.json`、`harrogath-passage.json` |
| 第五幕奖励 | Shenk／Larzuk原38／58给帽子2孔；三牢门死亡后15中立囚犯逃离，Qual-Kehk三符文并保留已有铁狼；冻结对象原20131消息、Malah解冻药与抗性卷轴使用、Anya稀有奖励、Nihlathak死亡／原加工给2孔帽子署名Hero。`larzuk-socketed.json`、`rescue-rewarded.json`、`anya-rewarded.json`、`anya-personalized.json` |
| 古代人与巴尔 | 祭坛生成三古代人／击杀给1.4M经验；新副本用原回城卷轴重置战斗并实际进城；原五波含第二波冰法师、563进入大厅、Baal死亡／Tyrael20175对白／565出口返回109、最终flags72。`ancients-rewarded.json`、`ancients-portal-reset-fixed.json`、`baal-wave-1.json`至`5.json`、`baal-exit.json` |
| 修复与恢复 | 复验牢门25帧后仍在原位置；最终原v96保存经工具解码和新进程入局，五幕个人记录、343生命／86技能余点／120属性余点、孔数／署名／铁狼恢复；最终ProtocolReady，地图／移动／玩家显示可用，host失败／characterIssues／ignoredPackets／mapErrors／effectLimitations为0或空，stderr为空。`prison-doors-static-before.json`／`after.json`、`final-save-info.txt`、`final-restored.json`、`final-online.json` |

冒烟用管理员换区、补材料、击杀当前原生身份怪物与有界步进准备条件，实际交互／对白／拾取／加工／门户仍走原包；管理员生成敌人不用于目标资格。破珠使用管理员准备的意志，三器官／方块完整链和下水道定时机关未运行认证；安雅红门往返、所有NPC说明位、奖励容量／背压异常、完整三难度及多人未逐一覆盖。基德宾首次等待期间角色被击杀后复活、清理附近敌人再复验，不能把替身或管理条件记为正常通关。

最终Harrogath场景仍报告`unavailableUnits=23`，可渲染单位为6；地图／移动可用及上述空错误列表不能证明全部单位外观已支持。本轮不据此改客户端或认证后三幕完整视觉。
