# 项目基线

本页只维护当前能力概况、架构边界和主要缺口。具体实现、原版依据及适用范围见[文档目录](docs/README.md)。

战斗重构当前处于R1–R5交错迁移后的公共底座收尾阶段，尚未完成整体范围验收。已建立hosting准备、server执行、gameplay公共计算的分层；公共整数／随机、公式、StatList／FullStats、定点资源及伤害、动作速率已有实际消费者。NPC、友军、怪物、佣兵与召唤物共用唯一非玩家Actor及公共参与者／属性／资源能力，接收资格保留各原程序差异。光环、诅咒、战吼、祭坛、药剂、冷毒伤口及护盾已接公共列表和生命周期；大量技能／飞弹程序、子体、接触、房间链与原包同步已迁入，但均不代表所有来源与组合完成。

当前可达动作／AI／移动／物件及物品再生已接所属帧事件，旧武器与怪物近战数值链已删除并共用原生联系。主要缺口是完整世界休眠重建、队列1／2全局交错及逐随机轨迹验收、全部属性／费用／死亡回调、剩余专用飞弹与世界载体、装备／伙伴组合，以及既有内容迁回后的逐项对齐。德鲁伊／刺客、后三幕专用AI／首领及特殊程序继续受上述底座门槛约束；不能以历史技能数量或公共函数存在宣称完整复刻。

已实现技能的执行入口核对及旧路径清理见[技能公共规范](docs/gameplay/skills/COMMON.md#技能执行路径源码核对)；死亡效果、直接伤害／移动持续技能及怪物准备快照已完成源码迁移，替换入口与尚未对齐的边界由该页维护，原版依据见[资料来源](docs/resources/THIRD_PARTY.md#原版113c静态与开发对照证据)。

实际阶段、优先级和完成条件统一见[战斗重构进度与剩余步骤](docs/architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)；源码与具体缺口见[内核子系统](docs/modules/SERVER_SYSTEMS.md)、[持续效果公共机制](docs/gameplay/skills/COMMON.md)，数值函数入口见[公共规则](docs/architecture/COMBAT_SHARED_RULES.md#重点战斗数值的当前公共入口)。

## 产品与代码边界

D2X提供单机、TCP/IP和原服连接，共用一套客户端。自研宿主负责权威玩法，客户端读取状态并提交请求；断线不切换权威来源。

宿主负责内容准备、协议适配和角色存储，玩法内核不依赖资源加载、网络、文件或显示设备。资源与规则以当前MPQ为依据，未实现行为明确拒绝。模块职责见[架构速览](docs/architecture/OVERVIEW.md#agent速览)，协作约束见[AGENTS.md](AGENTS.md)。

服务端战斗目标以[复刻目标架构](docs/architecture/SERVER_COMBAT_RECONSTRUCTION.md)为准，实际状态和后续实施顺序唯一见[总计划](docs/architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)。R1–R5允许按已满足依赖的切片交错，R6必须复核既有五职业／第一二幕等内容，R7补新内容；各阶段的完整准入条件尚未全部达到。性能优化必须保持有效输入下的数值、随机消费和执行次序，不能用降频、合帧或近似算法换速度。

## 当前实现范围

| 领域 | 能力概况 | 主要缺口／专题入口 |
| --- | --- | --- |
| 连接／宿主 | 单机、TCP/IP、原服连接及独立游戏服务 | 完整故障恢复；[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md) |
| 地图／世界 | 五幕地图生成及客户端房间重建；宿主按每幕逐房间物化，已核对原版瓦片／静态碰撞及房间调用链；连续跨区寻路、实时单位占位、多人出生／旅行／复活寻位、按路径结束切换走跑动画、探索缓存及常见环境物件 | 原版混合客户端及更广泛多人交错、完整单位随机／恢复、蓝图／休眠实体回收及特殊内容仍有缺口；[地图](docs/modules/MAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 物品／成长 | 库存、装备、掉落、商店、合成、角色成长及死亡／尸体；掉落双流、冻结批次、逐操作属性及指定词缀已有实现 | 原版同帧出生／逐件落点时序、全来源／属性组合仍未精确认证，复杂争用、硬核与多尸体；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md) |
| 战斗／技能 | 普攻、法术、状态、武器效果及PvP；女巫、亚马逊、圣骑士、死灵、野蛮人技能；刺客蓄气／陷阱／独立伙伴施法及德鲁伊变形／召唤等已有执行入口 | 全技能与来源组合验收、完整双持与触发组合、完整PvP规则；[刺客进度](docs/gameplay/skills/ASSASSIN.md)、[技能](docs/gameplay/skills/README.md)、[战斗](docs/gameplay/combat/FACTIONS.md) |
| 怪物／伙伴 | 第一、第二幕家族及第三至第五幕逐项程序已有接入，含NpcBarb、HellMeteor／投石机客户端程序、王座Baal波次／Diablo命令恢复及迷乱随机借用 | 完整NPC／MapAI、光环与修饰器重建仍有实现缺口；全局交错／逐随机及全难度组合尚未验收；钥匙／红门高级事件暂缓。范围见[怪物目录](docs/gameplay/monsters/README.md#当前实施范围)，具体覆盖见[逐项台账](docs/gameplay/monsters/ACT3.md)；[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、五幕个人任务主流程、组队任务及奖励保存；原任务日志已阅回执及完成动画消费 | 复杂剧情、机关和特殊多人任务；[NPC／任务](docs/modules/NPC_QUEST.md)、[任务](docs/gameplay/quests/SYSTEM.md) |
| 多人／表现 | 组队、聊天、交易、敌意及共享收益；物品提示、库存原调色／镶孔叠层／无形混色、自动地图大小／定位／平移及PL2 Fade；大面板原资源、M键消息日志、组队按钮、人物／技能按压及原表背包布局；英文ESC各级菜单、Sound／Music／NPC Speech、Lighting On／Off设置与未支持设置置灰；包裹即时拿起、放置、原快捷键／使用、方块收纳、鉴定准备、堆叠与购买／金币输入已有实现，移除拆分及额外快速转移／右键装备，见[包裹界面](docs/gameplay/items/INVENTORY_UI.md#操作) | 完整像素／状态对齐、特殊任务事件、硬核权限、中文聊天与部分原版表现；[联网](docs/modules/NETWORK.md)、[UI](docs/gameplay/ui/CLASSIC_HUD.md)、[自动地图](docs/gameplay/world/AUTOMAP.md)、[物品提示](docs/gameplay/items/PRESENTATION.md#物品属性提示) |


已接能力不代表全部组合或原版客户端兼容性完成；具体支持条件和未覆盖范围由对应专题维护。

自然生成由server/population唯一执行，hosting只准备当前MPQ规则、原型和预设描述；旧人口规划器与预掷出生队列已移除。普通怪物、精英／仆从、任务怪物及NPC共用Actor构造、身份／房间发布与休眠恢复继续点，任务清场共用实时生成覆盖查询。真实类型碰撞／替身、中立预设、囚犯激活、随机物件和共享生成流的当前范围见[人口](docs/gameplay/monsters/POPULATION.md#自然生成工作总览)及[物件](docs/gameplay/world/OBJECTS.md#自然生成源码边界)。完整特殊AI／MapAI恢复、地面物品休眠位流、按类型GUID、全部初始化／修饰器回调及全局交错仍未完成。

## 保存边界

角色持久数据由宿主保存，临时游戏状态不写入角色存档。格式、校验、拒绝与恢复规则见[存档](docs/modules/SAVES.md)，探索缓存独立见[自动地图](docs/gameplay/world/AUTOMAP.md)。

## 文档维护边界

本页保持简短，只替换能力概况与主要限制。不放协议编号、算法、公式、资源参数、逐项功能清单或构建／打包／自研运行记录。详细内容归负责专题，操作方法归[开发指南](docs/development/BUILD_AND_RUN.md)，修改历史由Git追溯。
