# 项目基线

本页只维护当前能力概况、架构边界和主要缺口。具体实现、原版依据及验证范围见[文档目录](docs/README.md)。

战斗重构当前处于R1–R5交错迁移后的公共底座收尾阶段，尚未完成整体范围验收。已建立hosting准备、server执行、gameplay公共计算的分层；公共整数／随机、公式、StatList／FullStats、定点资源及伤害、动作速率已有实际消费者。NPC、友军、怪物、佣兵与召唤物共用唯一非玩家Actor及公共参与者／属性／资源能力，接收资格保留各原程序差异。光环、诅咒、战吼、祭坛、药剂、冷毒伤口及护盾已接公共列表和生命周期；大量技能／飞弹程序、子体、接触、房间链与原包同步已迁入，但均不代表所有来源与组合完成。

当前可达动作／AI／移动／物件及物品再生已接所属帧事件，旧武器与怪物近战数值链已删除并共用原生联系。主要缺口是完整世界休眠重建、队列1／2全局交错及逐随机轨迹验收、全部属性／费用／死亡回调、剩余专用飞弹与世界载体、装备／伙伴组合，以及既有内容迁回后的逐项对齐。德鲁伊／刺客、后三幕专用AI／首领及特殊程序继续受上述底座门槛约束；不能以历史技能数量或公共函数存在宣称完整复刻。

实际阶段、优先级和完成条件统一见[战斗重构进度与剩余步骤](docs/architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)；源码与具体缺口见[内核子系统](docs/modules/SERVER_SYSTEMS.md)、[持续效果公共机制](docs/gameplay/skills/COMMON.md)，数值函数入口见[公共规则](docs/architecture/COMBAT_SHARED_RULES.md#重点战斗数值的当前公共入口)。已有构建／有限运行、当前包身份及性能证据只由[运行包记录](docs/development/BUILD_AND_RUN.md#运行包记录)维护，不沿用旧包认证后续工作区；本页不另复制批次记录。

## 产品与代码边界

D2X提供单机、TCP/IP和原服连接，共用一套客户端。自研宿主负责权威玩法，客户端读取状态并提交请求；断线不切换权威来源。

宿主负责内容准备、协议适配和角色存储，玩法内核不依赖资源加载、网络、文件或显示设备。资源与规则以当前MPQ为依据，未实现行为明确拒绝。模块职责见[架构速览](docs/architecture/OVERVIEW.md#agent速览)，协作约束见[AGENTS.md](AGENTS.md)。

服务端战斗目标以[复刻目标架构](docs/architecture/SERVER_COMBAT_RECONSTRUCTION.md)为准，实际状态和后续实施顺序唯一见[总计划](docs/architecture/MULTIPLAYER.md#23-战斗重构实际阶段与剩余步骤)。R1–R5允许按已满足依赖的切片交错，R6必须复核既有五职业／第一二幕等内容，R7补新内容；各阶段的完整准入条件尚未全部达到。性能优化必须保持有效输入下的数值、随机消费和执行次序，不能用降频、合帧或近似算法换速度。

## 当前实现范围

| 领域 | 能力概况 | 主要缺口／专题入口 |
| --- | --- | --- |
| 连接／宿主 | 单机、TCP/IP、原服连接及独立游戏服务 | 完整故障恢复；[联网](docs/modules/NETWORK.md)、[独立服务](docs/development/PVPGN_SERVER.md) |
| 地图／世界 | 五幕地图生成、原房间引用／活动碰撞、异步蓝图准备、连续跨区寻路、旅行、探索缓存及常见环境物件 | 动态单位占位、怪物跨级追击、逐房间蓝图／休眠实体内存回收与特殊内容；[地图](docs/modules/MAP.md)、[物件](docs/gameplay/world/OBJECTS.md) |
| 物品／成长 | 库存、装备、掉落、商店、合成、角色成长及死亡／尸体；掉落双流、冻结批次、逐操作属性及指定词缀已纳入Release构建；生成、镶嵌及保存重入获有限观察 | 原版同帧出生／逐件落点时序、全来源／属性组合仍未精确认证，复杂争用、硬核与多尸体；[物品](docs/gameplay/items/README.md)、[人物](docs/modules/CHARACTER.md) |
| 战斗／技能 | 普攻、法术、状态、武器效果及PvP；女巫、亚马逊、圣骑士、死灵、野蛮人技能；刺客蓄气／陷阱／独立伙伴施法及德鲁伊变形／召唤等现有程序已纳入Release构建；本轮只复验普通攻击与野外怪物／物件，其他技能运行范围见专题 | 全技能与来源组合验收、完整双持与触发组合、完整PvP规则；[刺客进度](docs/gameplay/skills/ASSASSIN.md)、[技能](docs/gameplay/skills/README.md)、[战斗](docs/gameplay/combat/FACTIONS.md) |
| 怪物／伙伴 | 第一、第二幕三难度怪物行为、精英／首领、佣兵及部分召唤物 | 第三至第五幕、完整召唤与伙伴行为；[怪物](docs/gameplay/monsters/README.md)、[佣兵](docs/gameplay/characters/HIRELINGS.md) |
| NPC／任务 | 城镇服务、五幕个人任务主流程、组队任务及奖励保存 | 复杂剧情、机关和特殊多人任务；[NPC／任务](docs/modules/NPC_QUEST.md)、[任务](docs/gameplay/quests/SYSTEM.md) |
| 多人／表现 | 组队、聊天、交易、敌意、共享收益及相关界面；物品提示及库存原调色、镶孔叠层、无形混色与定位已纳入Release两包，代表物品显示及重入获有限观察 | 特殊任务事件、硬核权限、中文聊天与部分原版表现；[联网](docs/modules/NETWORK.md)、[UI](docs/gameplay/ui/CLASSIC_HUD.md)、[物品提示](docs/gameplay/items/PRESENTATION.md#物品属性提示) |


已接能力不代表全部组合或原版客户端兼容性完成；具体支持条件和未覆盖范围由对应专题维护。

自然生成重构仍未完成：源码已接怪物区域／ObjectControl／NPCControl／QuestControl子流、全关卡目录与组件池、服务端随机物件PopulateFn1–9与主题、逻辑区域内实时密度及普通族群、单位流精英词缀／随从／姓名、主要DS1／任务固定首领、吉得宾与邪恶瓮实时精英、原中立罗格游荡、余光对象延迟出生及塔楼真实控制飞弹。活动房间按幕序和创建链头插次序遍历；出生／升级统一发布Actor及初始事件。完整休眠压缩／重建、TileInfo11保护搜索的关卡随机与房间激活、原GUID分配、全部初始化／修饰器回调及全局交错仍未闭合。异步掉落使用当前实例帧，AI等待时不再提交旧决策随机。构建／双包身份、有限运行范围与照明疑点统一见[运行包记录](docs/development/BUILD_AND_RUN.md#运行包记录)，不据此认证完整自然生成、全部初始化或原版逐随机一致性。入口和限制见[人口](docs/gameplay/monsters/POPULATION.md)和[自然物件生成](docs/gameplay/world/OBJECTS.md#自然生成源码边界)。

## 保存边界

角色持久数据由宿主保存，临时游戏状态不写入角色存档。格式、校验、拒绝与恢复规则见[存档](docs/modules/SAVES.md)，探索缓存独立见[自动地图](docs/gameplay/world/AUTOMAP.md)。

## 文档维护边界

本页保持简短，只替换能力概况与主要限制。不放协议编号、算法、公式、资源参数、逐项功能清单或开发日志。详细内容归负责专题，交付记录归[开发指南](docs/development/BUILD_AND_RUN.md)，修改历史由Git追溯。
