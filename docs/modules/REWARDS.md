# 死亡、经验与掉落结算基线

更新：2026-10-04。对应用户列表第八项及[技术改造方案](../architecture/REFACTOR_PLAN.md) P7 的死亡奖励切片。源码已接入，并按本轮后续授权随第九项完成 Windows Release 与简单冒烟；未打包。完整 P7 与多人尚未完成。

## 分工与入口

| 入口 | 当前职责 |
| --- | --- |
| [`rewards/death.hpp`](../../src/gameplay/rewards/death.hpp) | 独立 `EnemyDied` 权威事实：真实身份、死亡区域／位置、控制玩家／实际攻击者、难度、MF／GF、掉落种子、词缀奖励及原 NOTC 快照；没有活动单位指针 |
| [`rewards/death_settlement.*`](../../src/gameplay/rewards/death_settlement.cpp) | `settleMonsterDeaths` 顺序协调；通过 `IDeathSettlementHost` 接受显式受益者与任务事实、执行计划及交付结果，不查询完整角色／怪物／会话 |
| [`quest/death.*`](../../src/gameplay/quest/death.cpp) | 从死亡与旧任务记录生成首杀资格及有序转换／世界效果计划；不修改任务簿、不创建物品、不查询世界 |
| [`rewards/experience.*`](../../src/gameplay/rewards/experience.cpp) | 已有等级差、经验倍率、玩家加成与佣兵上限／间接击杀／升级算术；输入为已解析数值，不反查角色或原表 |
| [`rewards/death_wave.*`](../../src/gameplay/rewards/death_wave.cpp) | 对宿主提供的目标资格、坐标及随机流规划延迟死亡帧；不拥有活动怪物或房间 |
| [`content/monsters/death_loot.*`](../../src/content/monsters/death_loot.cpp) | 从已准备内容解析 TC／品质／掉落级别，接收职业、MF／GF、限量记录与首杀奖励资格；原 NOTC 使用死亡快照，规划不反查尸体 |
| [`content/monsters/monster_experience.*`](../../src/content/monsters/monster_experience.cpp) | 真实身份、难度、区域等级与原经验表适配；准备数值后调用纯经验算术 |
| [`session_deaths.cpp`](../../src/gameplay/session/session_deaths.cpp) | 复制死亡事件批次，绑定本地权威适配与已有 `LootSystem`，准备内容、诊断及发布暂缓消息 |
| [`session_death_quests.cpp`](../../src/gameplay/session/session_death_quests.cpp) | 提供本人任务／首领事实，按计划提交任务并操作任务书、箱、门、门户；筛选存活／盟友／邻房目标并写回死亡波结果 |
| [`session_death_experience.cpp`](../../src/gameplay/session/session_death_experience.cpp) | 复验受益者绑定、提交佣兵与玩家成长、刷新佣兵定义和生命 |
| [`session_loot.cpp`](../../src/gameplay/session/session_loot.cpp) | 保留原地面创建、放置失败通知和拾取适配；已移除死亡协调与任务／经验规则 |

`LootRequest`、`LootDrop`／`LootPlan` 分别位于 `loot/request.hpp`、`loot/plan.hpp`；`ItemGeneration`／词缀生成值位于 `items/generation.hpp`，实例头继续包含它。掉落接口无需完整库存实例／服务。`loot/loot.hpp` 保留兼容聚合、TC 类型与 `LootSystem`；本批未全面拆分内容目录或统计依赖闭包／增量编译耗时。

CMake：纯规则及协调加入 `d2x_gameplay`，原表适配加入 `d2x_content`，本地执行加入 `d2x_session`；保留已有库依赖方向。玩法代码不读取 MPQ、设备或 GPU；内容规划使用已经准备的表与目录，不直接打开资源档。

## 一次结算的顺序

1. 复制本步的 `EnemyDied`，避免任务／物品通知扩容事件容器后使输入失效。先按死亡 ID 检查 `LootSystem`，已结算者不再推进任务、消耗随机或补发经验。
2. 每一条死亡重新取得任务上下文，先从旧阶段计算安达利尔／都瑞尔首杀资格，再执行有序计划。同一步前一个死亡的任务、成长和暗金限量提交对后一个死亡可见。
3. 保留原任务通知与效果次序：血鸟进度后死亡波；女伯爵进度后宝箱；安达利尔死亡波后进度／对白、凯恩协调及门户；都瑞尔进度后开门；召唤者进度后避难所进度；罗达门特死亡波后提交待领书标志、落书，再发任务通知。
4. 召唤来源沿用原空掉落去重并结束结算的分支；不新加第一幕任务资格限制。普通死亡用单位死亡种子规划 TC／品质；已支持的安达利尔本人首杀三宝石使用原宿主任务随机流。原规则与随机调用顺序继续保留。
5. 发布掉落暂缓，再由 `LootSystem` 提交去重／种子与暗金限量记录，交付地面物品；最后对匹配的控制玩家先提交佣兵经验，再提交玩家经验。无玩家主人的击杀不借用本地玩家经验资格。

佣兵一次经验继续受级差资格、等级区间的 1/64 上限和主人间接击杀的 86/256 限制；倍率、整数截断、单次最多升级一次及升级补生命沿用原实现。怪物等级／经验、品质与 TC 数据继续来自当前 MPQ 的准备内容，本批没有改概率或加入新参数。

## 所有权与限制

- `LootSystem` 仍唯一拥有本局已结算 ID、暗金限量和共用物件掉落种子；死亡 TC 使用死亡快照种子，不推进该物件随机流。地面物品仍由 `InventoryService` 唯一拥有，任务记录仍属 `CharacterRecord`。
- 本地适配显式绑定一个玩家 ID，任务信用仍沿用本人任务簿。接口没有实现队伍经验、多人 NoDrop、共享首杀资格或其他玩家任务查询；远端客户端不接收这份内部奖励事实。
- 血鸟／安达利尔／罗达门特死亡波的盟友、存活、邻房及亡灵资格仍由宿主准备；当前只调度模拟器的活区，跨区域死亡波未实现。世界物件、凯恩协调与门户操作仍是本地适配职责。
- 掉落暂缓沿用一次结算，不是可重试队列；原暗金占用先于地面放置结果，空位不足不回滚占用。本批未改为任务／掉落／经验跨服务原子事务，失败恢复留后续独立设计。
- 本模块负责怪物死亡收益链；人物尸体／死亡惩罚归 [玩家死亡](../gameplay/characters/PLAYER_DEATH.md)，未实现技能、任务完整分支及联网后端不由该切片认证。D2S v96、保存语义和规则指纹不变，死亡事实、去重与区域运行态不新增到角色档。

## 当前验证状态

本轮随第九项通过 Windows Release 游戏／资源工具链接。普通 Sorceress／seed210 临时实例通过已有管道观察：普通沉沦魔击杀 +18 经验；另一击杀 +26，一级佣兵经验 200→205；自然安达利尔首杀 +1282，原三件 TC 物品后附 `gcb/gcb/gsb`，A1Q6=3／凯恩=8；再创建同身份首领击杀不附首杀三宝石。下水道三级调试生成的真实 radament 首次死亡创建一份 `ass`（本人等级7），A2Q1=3／pending 标志1，后续同身份死亡未再落书。已死亡目标的再次调试击杀在入口拒绝，不将其称为重复事件去重测试。

火弹正常施法／步进击杀近处临时沉沦魔，生命1→0、本人经验10000→10018，验证正常伤害链到结算的代表性路径。自然首领身份、调试生成和调试击杀分别如上记录，不冒充完整原版战斗流程。未覆盖同一步多死亡、转换／召唤收益、NOTC 生命周期、佣兵升级、三类死亡波逐目标／帧、限量暗金、缺表暂缓或地面空位不足；这些保持后续验收范围。

修复拆头后物品属性／报价声明及实现的显式依赖，未恢复完整库存向掉落头传播。证据为 `artifacts/refactor-final-build-fix2-20261004.log`、最终增量日志与 `artifacts/refactor-final-smoke-20261004/`，详细联合范围见[会话基线](SESSION.md#九项重构联合收尾与冒烟)。本轮提交源码／文档，未新增测试程序、未打包，`dist/current` 未包含这些重构。
