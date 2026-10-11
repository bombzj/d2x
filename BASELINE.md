# 项目基线

更新：2026-10-11。本页只维护当前状态、源码与运行包差异及最新已记录交付；不维护实现细节和历史测试流水。开始修改前读[协作约定](AGENTS.md)、[架构速览](docs/architecture/OVERVIEW.md#agent速览)与[对应模块](docs/README.md)。

## 产品与代码边界

Single Player、TCP/IP与原服共用一套D2GS客户端。自研宿主拥有权威玩法，单机使用内存字节传输；TCP/IP使用本地角色和默认4000直连，局前内存MCP只复用选角。network／UI不读写D2S，应用交由hosting持有本地租约和原版存档交换；断线不切换本地权威。hosting负责内容／协议／存储，server负责领域状态和25Hz执行；内核不读取MPQ、文件、socket、设备或GPU。详细依赖与状态所有权见[架构](docs/architecture/OVERVIEW.md)和[内核](docs/modules/SERVER_SYSTEMS.md)。

规则／原图优先取当前MPQ，未实现行为明确拒绝；唯一已授权敌怪替身为沉沦魔，保留真实身份，中立单位不可替换。资料与许可见[来源](docs/resources/THIRD_PARTY.md)。

## 当前源码范围

以下为实现概况，不代表全部参数或路径通过运行验收；准确范围由负责专题维护。

| 领域 | 已接范围 | 主要缺口／入口 |
| --- | --- | --- |
| 连接／宿主 | 原服认证、原MCP选角／房间、单机／LAN、独立PvPGN游戏服务、多实例 | 完整恢复、跨机器／高负载与长期稳定性未验收；[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md) |
| 地图／旅行 | 五幕1–136原生成、碰撞、区域切换、传送点、门户及跨幕旅行；单机／TCP/IP本人探索侧文件 | 长绕墙／拥挤、完整活动房间与特殊内容；[地图](docs/modules/MAP.md)、[自动地图](docs/gameplay/world/AUTOMAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 野外物件 | 当前MPQ祭坛1–22、水井解除状态、爆炸桶连锁、伤害箱、宝箱陷阱、地刺／火焰、原陷阱怪物AI及特殊藏物 | 完整五幕变体、自然生成概率与多人争用未验收；[物件](docs/gameplay/world/OBJECTS.md#自研野外祭坛与环境机关) |
| 物品／成长 | 库存／装备／腰带、生成／掉落、商店／加工、经验／属性／技能、死亡／尸体 | 复杂争用、全属性／配方／奖励组合、硬核／多尸体；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md) |
| 战斗／技能 | 普攻／投射／六通道、通用十项、女巫／亚马逊／圣骑士／死灵／野蛮人各30项 | 德鲁伊／刺客、完整双持与吸取／压碎／撕裂／触发、PvP；[技能](docs/gameplay/skills/README.md) |
| 怪物／伙伴 | 第一幕63战斗类型／25类AI、第二幕74身份／33类AI、动画与30项技能槽接线、精英／首领、随机姓名／勇士标签／原调色及血乌／安达利尔死亡表现，Hydra、诱饵／女武神、四类原佣兵、死灵召唤／重生及原宠物头像／数量／生命条 | 第三至第五幕与召唤、完整伙伴服务与表现；准确范围及运行边界见[怪物](docs/gameplay/monsters/README.md)、[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、五幕27项个人任务主流程与保存 | 完整中立AI、剧情／机关及组队任务资格；[NPC／任务](docs/modules/NPC_QUEST.md) |
| 多人 | 1–8人名册／移动／聊天／交易、邀请／离队、同队头像／地图标记、共享门户、附近经验／NoDrop及友方光环 | 完整任务共享、敌意／PvP、尸体权限、八人背压与跨机器验收；[组队](docs/modules/NETWORK.md#组队邀请客户端)、[交易](docs/gameplay/items/PLAYER_TRADE.md) |

## 当前重构与技能交付

战斗关系、来源与控制代次、重试后续阶段、武器目标局部随机和耐久防重复提交已改；技能私有运行态／求值实现移出公共头，活单位空间索引接入武器及通用法术直线碰撞。AI、范围爆炸及怪物弹体仍有遍历。详细范围见[战斗关系与提交](docs/modules/SERVER_SYSTEMS.md#战斗关系与提交)。

四类佣兵的服务／AI／装备／技能／生命／经验／保存已接，并完成代表单机路径的有限冒烟，详见佣兵专题。TCP/IP直连修正的自研双端有限冒烟及用户手动原版Host／D2X Join成功边界由[联网](docs/modules/NETWORK.md)维护。当前源码和Windows Release包已接入死灵及野蛮人各30项，规则指纹为**v41**；公共抽取前的代表单机证据见[野蛮人技能](docs/gameplay/skills/BARBARIAN.md#当前包有限冒烟)，铁魔历史证据见[死灵技能](docs/gameplay/skills/NECROMANCER.md#当前包有限冒烟)。未测量性能及增量编译收益，重构的完整争用／背压边界未认证。

头像已按当前MPQ和本地原1.13c接入召唤物分组、实际类型图片、数量、单只生命条、佣兵悬停／喂药／右键装备及Z显示开关，详见[头像](docs/gameplay/characters/HIRELINGS.md#召唤物与佣兵头像)。当前Release包保留这批改动，此前单机观察到铁魔原头像／生命条；本轮没有专项复验全部喂药／右键／队友路径。头像改动不改变规则指纹与D2S格式。

## 保存边界

D2S磁盘格式仍为v96；当前源码准入指纹、拒绝条件、锁与恢复语义由[存档](docs/modules/SAVES.md#文件与值边界)唯一维护。队伍、邀请、AI、弹体、临时状态、控制代次及重试队列不写盘；旧规则不静默迁移。原服保存由D2GS／D2DBS负责，自研由宿主持有角色与存储边界。本地探索是独立版本2 `.d2xmap`观察缓存，原件／校验／模式与实际种子边界由[自动地图](docs/gameplay/world/AUTOMAP.md#跨进程探索保存)维护。

<a id="当前运行包与有限冒烟"></a>

## 当前运行包与验证边界

当前包包含按住怪物后的目标信息条持续显示、独立于技能资格的悬停姓名及首领金色姓名修正，入口与清理边界见[怪物表现](docs/gameplay/monsters/PRESENTATION.md#目标姓名与按住锁定)。本批未专项复验鼠标锁定。

最新交付为2026-10-11第二幕怪物完整接线批次，Windows **Release**，规则v41／D2S v96。当前MPQ候选、固定身份及子怪共74身份／33类AI，30项技能槽逐项准备；补齐飞行／栖息／集合／颜色／再生AI、原序列与移动回绕、钻地、多段近战、召唤／复活、诅咒、冰尖柱／陨石／毒瓶、固有光环及甲虫／虫后回调。逐项范围及原MPQ资源限制见[第二幕](docs/gameplay/monsters/ACT2.md)，逐项技能见[怪物技能](docs/gameplay/skills/MONSTERS.md#第二幕技能逐项台账)。客户端和独立服务端由本批构建更新，同时保留第一幕补缺及[技能公共抽取](docs/gameplay/skills/COMMON.md#公共函数抽取)。以下哈希对应本批磁盘包。

| 产物 | 目录 | SHA256 |
| --- | --- | --- |
| 客户端 | `dist/current/d2x.exe` | `48ED42BCF7F76EA777A0AB823585BA3BB95AF4F32B5746997091737E41302EF7` |
| 独立服务端 | `dist/server/d2x_server.exe` | `8E87A3CDD32E320D7501D1A6AADCBEA3EE6646774D260E76D6DFF47860107628` |
| 网络DLL | 两包的`d2x_bncs_legacy.dll` | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

本批按最新授权完成Windows Release构建、两个包更新及现有游戏／调试管道的有限单机冒烟。最终包逐项进入74个真实身份及9个固定SuperUnique，身份均匹配原类型，场景不可用计数均为0；代表钻地／飞行／栖息、出生／复活、虫后死亡及召唤者五项技能和隔离角色保存重入见[第二幕有限证据](docs/gameplay/monsters/ACT2.md#有限运行证据)。独立服务端只核对二进制加载及Usage入口（按现有参数契约退出2），未连接PvPGN后端。未新增测试脚本／用例／专用程序。第一幕v39证据仍由[第一幕](docs/gameplay/monsters/ACT1.md#有限运行证据)维护，不代替本批验收。三难度自然出生、全部技能分支、精英组合、碰撞／中断／背压、联网、跨机器、Linux和性能均未获本批运行验收；既有声音中断淡出限制及当前MPQ缺失的豹族死亡血迹装饰层已在专题记录。此前启动测量见[开发指南](docs/development/BUILD_AND_RUN.md#启动测量)。

## 专题证据入口

各页保存对应历史包、实际路径、失败样本与未覆盖范围；历史成功不等于当前源码通过。

| 证据 | 负责页面 |
| --- | --- |
| 组队／共享门户／经验与独立服务 | [组队有限证据](docs/modules/NETWORK.md#组队有限证据)、[PvPGN有限冒烟](docs/development/PVPGN_SERVER.md#有限冒烟) |
| 技能／施法表现 | [技能分类](docs/gameplay/skills/README.md)、[通用表现](docs/gameplay/skills/COMMON.md#通用施法表现有限证据) |
| 怪物／佣兵 | [第一幕](docs/gameplay/monsters/ACT1.md#有限运行证据)、[第二幕](docs/gameplay/monsters/ACT2.md#有限运行证据)、[佣兵](docs/gameplay/characters/HIRELINGS.md#当前包有限冒烟) |
| 野外祭坛／环境机关 | [物件有限冒烟](docs/gameplay/world/OBJECTS.md#当前包有限冒烟) |
| 物品／交易 | [物品证据](docs/gameplay/items/EVIDENCE.md)、[玩家交易](docs/gameplay/items/PLAYER_TRADE.md#有限冒烟) |
| 任务 | [第一幕](docs/gameplay/quests/ACT1.md#有限运行证据)、[第二幕](docs/gameplay/quests/ACT2.md#有限运行证据)、[第三至第五幕](docs/gameplay/quests/ACT3_5.md#依据与运行证据) |

七职业、五幕三难度、完整多人、原版逐帧／像素及长期稳定性未整体认证。后续顺序见[总计划](docs/architecture/MULTIPLAYER.md)，执行授权与证据标准见[验证指南](docs/development/TESTING.md)。更新本页时替换当前状态，不追加批次流水；历史交付由专题与Git追溯。
