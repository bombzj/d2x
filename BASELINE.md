# 项目基线

更新：2026-10-10。本页只维护当前状态、源码与运行包差异及最新已记录交付；不维护实现细节和历史测试流水。开始修改前读[协作约定](AGENTS.md)、[架构速览](docs/architecture/OVERVIEW.md#agent速览)与[对应模块](docs/README.md)。

## 产品与代码边界

Single Player、LAN与原服共用一套原MCP／D2GS客户端。自研宿主拥有权威玩法，单机使用内存字节传输；客户端不读写角色D2S，不因断线切换本地权威。hosting负责内容／协议／存储，server负责领域状态和25Hz执行；内核不读取MPQ、文件、socket、设备或GPU。详细依赖与状态所有权见[架构](docs/architecture/OVERVIEW.md)和[内核](docs/modules/SERVER_SYSTEMS.md)。

规则／原图优先取当前MPQ，未实现行为明确拒绝；唯一已授权敌怪替身为沉沦魔，保留真实身份，中立单位不可替换。资料与许可见[来源](docs/resources/THIRD_PARTY.md)。

## 当前源码范围

以下为实现概况，不代表全部参数或路径通过运行验收；准确范围由负责专题维护。

| 领域 | 已接范围 | 主要缺口／入口 |
| --- | --- | --- |
| 连接／宿主 | 原服认证、原MCP选角／房间、单机／LAN、独立PvPGN游戏服务、多实例 | 完整恢复、跨机器／高负载与长期稳定性未验收；[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md) |
| 地图／旅行 | 五幕1–136原生成、碰撞、区域切换、传送点、门户及跨幕旅行 | 长绕墙／拥挤、完整活动房间与特殊内容；[地图](docs/modules/MAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 物品／成长 | 库存／装备／腰带、生成／掉落、商店／加工、经验／属性／技能、死亡／尸体 | 复杂争用、全属性／配方／奖励组合、硬核／多尸体；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md) |
| 战斗／技能 | 普攻／投射／六通道、通用十项、女巫／亚马逊／圣骑士各30项 | 其他职业、完整双持与吸取／压碎／撕裂／触发、PvP；[技能](docs/gameplay/skills/README.md) |
| 怪物／伙伴 | 第一幕63战斗类型／25类AI、精英／首领，Hydra、诱饵／女武神、罗格 | 其他幕与召唤、完整AI／伙伴服务与表现；罗格换装／喂药／自然射击有历史阻塞；[怪物](docs/gameplay/monsters/README.md)、[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、五幕27项个人任务主流程与保存 | 完整中立AI、剧情／机关及组队任务资格；[NPC／任务](docs/modules/NPC_QUEST.md) |
| 多人 | 1–8人名册／移动／聊天／交易、邀请／离队、同队头像／地图标记、共享门户、附近经验／NoDrop及友方光环 | 完整任务共享、敌意／PvP、尸体权限、八人背压与跨机器验收；[组队](docs/modules/NETWORK.md#组队邀请客户端)、[交易](docs/gameplay/items/PLAYER_TRADE.md) |

## 未交付源码

战斗关系、来源与控制代次、重试后续阶段、武器目标局部随机和耐久防重复提交已改；技能私有运行态／求值实现移出公共头，活单位空间索引接入武器及通用法术直线碰撞。AI、范围爆炸及怪物弹体仍有遍历。详细范围见[战斗关系与提交](docs/modules/SERVER_SYSTEMS.md#战斗关系与提交)。

上述源码尚未构建、运行或打包，未测量性能及增量编译收益。源码规则为**v33**，已记录运行包仍为**v32**；不能用旧包证据认证本次重构。

## 保存边界

D2S磁盘格式仍为v96；当前源码准入指纹、拒绝条件、锁与恢复语义由[存档](docs/modules/SAVES.md#文件与值边界)唯一维护。队伍、邀请、AI、弹体、临时状态、控制代次及重试队列不写盘；旧规则不静默迁移。原服保存由D2GS／D2DBS负责，自研由宿主持有角色与存储边界。

<a id="当前运行包与有限冒烟"></a>

## 当前运行包与验证边界

最新已记录交付为2026-10-10启动优化批，Windows **Release**，规则v32／D2S v96；本页未重新核验磁盘包，不代表包含上述未交付源码。

| 产物 | 目录 | SHA256 |
| --- | --- | --- |
| 客户端 | `dist/current/d2x.exe` | `BD304511433DFEAB2EDE6D68B234A5954EE40F853D1EF8C7CE731FB7A2D51B66` |
| 独立服务端 | `dist/server/d2x_server.exe` | `12F8E748D10278B50DA803AD278AF67EA3DE2238C246A553D5BAC9C2B64530E9` |
| 网络DLL | 两包的`d2x_bncs_legacy.dll` | `23895A63AABA49CD4B6040921AFBE2DAF4933FA146B9308C438B6300544DB4E7` |

该批有限验证菜单／单机／本机TCP启动、首帧、正常退出保存及资源枚举；启动测量条件、结果与限制见[开发指南](docs/development/BUILD_AND_RUN.md#启动测量)。未重新认证PvPGN后端互通、全部玩法、跨机器或Linux，既有编译警告仍保留。

## 专题证据入口

各页保存对应历史包、实际路径、失败样本与未覆盖范围；历史成功不等于当前源码通过。

| 证据 | 负责页面 |
| --- | --- |
| 组队／共享门户／经验与独立服务 | [组队有限证据](docs/modules/NETWORK.md#组队有限证据)、[PvPGN有限冒烟](docs/development/PVPGN_SERVER.md#有限冒烟) |
| 技能／施法表现 | [技能分类](docs/gameplay/skills/README.md)、[通用表现](docs/gameplay/skills/COMMON.md#通用施法表现有限证据) |
| 怪物／佣兵 | [第一幕](docs/gameplay/monsters/ACT1.md#有限运行证据)、[佣兵](docs/gameplay/characters/HIRELINGS.md#当前包有限冒烟) |
| 物品／交易 | [物品证据](docs/gameplay/items/EVIDENCE.md)、[玩家交易](docs/gameplay/items/PLAYER_TRADE.md#有限冒烟) |
| 任务 | [第一幕](docs/gameplay/quests/ACT1.md#有限运行证据)、[第二幕](docs/gameplay/quests/ACT2.md#有限运行证据)、[第三至第五幕](docs/gameplay/quests/ACT3_5.md#依据与运行证据) |

七职业、五幕三难度、完整多人、原版逐帧／像素及长期稳定性未整体认证。后续顺序见[总计划](docs/architecture/MULTIPLAYER.md)，执行授权与证据标准见[验证指南](docs/development/TESTING.md)。更新本页时替换当前状态，不追加批次流水；历史交付由专题与Git追溯。
