# 项目基线

本页维护当前实现范围、架构边界和功能缺口；实现细节、原版依据、交付记录与验证证据由对应专题维护。开始修改前读[协作约定](AGENTS.md)、[架构速览](docs/architecture/OVERVIEW.md#agent速览)与[对应模块](docs/README.md)。

## 产品与代码边界

Single Player、TCP/IP与原服共用一套D2GS客户端。自研宿主拥有权威玩法，单机使用内存字节传输；TCP/IP使用本地角色和默认4000直连，局前内存MCP只复用选角。network／UI不读写D2S，应用交由hosting持有本地租约和原版存档交换；断线不切换本地权威。hosting负责内容／协议／存储，server负责领域状态和25Hz执行；内核不读取MPQ、文件、socket、设备或GPU。详细依赖与状态所有权见[架构](docs/architecture/OVERVIEW.md)和[内核](docs/modules/SERVER_SYSTEMS.md)。

规则／原图优先取当前MPQ，未实现行为明确拒绝；唯一已授权敌怪替身为沉沦魔，保留真实身份，中立单位不可替换。资料与许可见[来源](docs/resources/THIRD_PARTY.md)。

## 当前实现范围

以下为实现概况，具体规则、支持条件与限制由负责专题维护。

| 领域 | 已接范围 | 主要缺口／入口 |
| --- | --- | --- |
| 连接／宿主 | 原服认证、原MCP选角／房间、单机／LAN、独立PvPGN游戏服务、多实例 | 完整恢复；连接与宿主约束见[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md) |
| 地图／旅行 | 五幕1–136原生成、碰撞、区域切换、传送点、门户及跨幕旅行；单机／TCP/IP本人探索侧文件 | 长绕墙／拥挤、完整活动房间与特殊内容；[地图](docs/modules/MAP.md)、[自动地图](docs/gameplay/world/AUTOMAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 野外物件 | 当前MPQ祭坛1–22、水井解除状态、爆炸桶连锁、伤害箱、宝箱陷阱、地刺／火焰、原陷阱怪物AI及特殊藏物 | 五幕变体、生成概率与多人争用的具体支持条件见[物件](docs/gameplay/world/OBJECTS.md#自研野外祭坛与环境机关) |
| 物品／成长 | 库存／装备／腰带、生成／掉落、商店／加工、经验／属性／技能、死亡／尸体 | 复杂争用、全属性／配方／奖励组合、硬核／多尸体；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md) |
| 战斗／技能 | 普攻／投射／六通道、通用十项、女巫／亚马逊／圣骑士／死灵／野蛮人各30项 | 德鲁伊／刺客、完整双持与吸取／压碎／撕裂／触发、PvP；[技能](docs/gameplay/skills/README.md) |
| 怪物／伙伴 | 第一幕63战斗类型／25类AI、第二幕74身份／33类AI、动画与30项技能槽接线、精英／首领、随机姓名／勇士标签／原调色及血乌／安达利尔死亡表现，Hydra、诱饵／女武神、四类原佣兵、死灵召唤／重生及原宠物头像／数量／生命条 | 第三至第五幕与召唤、完整伙伴服务与表现；具体支持条件见[怪物](docs/gameplay/monsters/README.md)、[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、五幕27项个人任务主流程与保存 | 完整中立AI、剧情／机关及组队任务资格；[NPC／任务](docs/modules/NPC_QUEST.md) |
| 多人 | 1–8人名册／移动／聊天／交易、邀请／离队、同队头像／地图标记、共享门户、附近经验／NoDrop及友方光环；游戏内命令分流、BNCS私聊／命令回复、宿主虚拟人数 | 完整任务共享、敌意／PvP、尸体权限、中文聊天与头顶气泡；[联网](docs/modules/NETWORK.md#游戏内命令分流)、[交易](docs/gameplay/items/PLAYER_TRADE.md) |

## 公共计算与客户端显示

技能选择器／技能树共用[MPQ 描述层](docs/gameplay/skills/DESCRIPTIONS.md)，按原表生成说明、等级、数值及协同区；元素支配来自只读输入投影，物品授予、多段充能标签、学习资格和使用状态分别显示。完整变身／冷却显示仍待补。普通技能树按本职业基础技能状态显示绿色StrSkill119提示，并按原版顺序排列多个状态标题。公式与原版依据由描述专题维护。

玩家／佣兵属性页使用共用只读装备与原状态投影，显示防御、格挡、动作伤害／命中、抗性及佣兵装备数值；自研宿主通过原包发布永久抗性、佣兵基础属性及玩家技能状态。只在完整原快照齐备时确认默认零值，未知不以零掩盖。公式与输入边界见[人物属性](docs/gameplay/characters/ATTRIBUTES.md)及[佣兵](docs/gameplay/characters/HIRELINGS.md)。

战斗结算区分关系、来源与控制代次，保留重试阶段，使用武器目标局部随机和耐久防重复提交；技能私有运行态／求值实现与公共头分离，武器及通用法术直线碰撞使用活单位空间索引。AI、范围爆炸及怪物弹体仍有遍历。详细范围见[战斗关系与提交](docs/modules/SERVER_SYSTEMS.md#战斗关系与提交)。

四类佣兵的服务／AI／装备／技能／生命／经验／保存已接，支持条件见[佣兵](docs/gameplay/characters/HIRELINGS.md)。

头像按当前MPQ和本地原1.13c显示召唤物分组、实际类型图片、数量、单只生命条，并支持佣兵悬停／喂药／右键装备及Z显示开关，详见[头像](docs/gameplay/characters/HIRELINGS.md#召唤物与佣兵头像)。

右上角装备耐久小人提示使用当前MPQ原DuroIcon部位／三组颜色、活动武器组、原整数20%阈值、PL2半透明及自动地图位置分支。详见[经典HUD](docs/gameplay/ui/CLASSIC_HUD.md#装备耐久提示)。

怪物目标信息条在按住目标时持续显示，悬停姓名独立于技能资格，首领姓名使用金色；入口与清理边界见[怪物表现](docs/gameplay/monsters/PRESENTATION.md#目标姓名与按住锁定)。

## 保存边界

D2S磁盘格式为v96；角色校验、拒绝条件、锁与恢复语义由[存档](docs/modules/SAVES.md#文件与值边界)唯一维护。队伍、邀请、AI、弹体、临时状态、控制代次、重试队列、虚拟人数及客户端防误拾取开关不写盘。原服保存由D2GS／D2DBS负责，自研由宿主持有角色与存储边界。本地探索是独立版本2 `.d2xmap`观察缓存，原件／校验／模式与实际种子边界由[自动地图](docs/gameplay/world/AUTOMAP.md#跨进程探索保存)维护。

## 文档维护边界

功能顺序见[总计划](docs/architecture/MULTIPLAYER.md)，模块入口见[文档目录](docs/README.md)，构建与分发见[开发指南](docs/development/BUILD_AND_RUN.md)，验证方法见[测试指南](docs/development/TESTING.md)。运行证据及其覆盖范围在对应技能、怪物、物品、任务和联网专题维护。

更新本页时替换当前能力与限制，不记录对话批次、开发过程、是否构建／打包／运行、包日期／哈希或冒烟结果，不追加工作流水。交付记录由开发文档维护，验证证据由负责专题维护，修改历史由Git追溯。
