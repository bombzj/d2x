# 数据流与生命周期

本页只解释跨模块流向。各领域规则、持久字段和缺口由链接页面维护，不再保留旧内部存档版本的阶段记录。

| 数据 | 流程 | 生命周期与权威边界 |
| --- | --- | --- |
| 原始内容 | `Archives → resources 解码 → content 类型化目录` | 本局只读共享；资源来源、优先级及本机摘要见 [MPQ](../resources/MPQ.md) |
| 地图 | `WorldCatalog → WorldPlan / MapRecipe → RegionStore → MapTerrain / Map` | 当前区与直接连续邻区按需加载，已访地形缓存保留；[地图基线](../modules/MAP.md) 管所有权，[地图规则](../gameplay/world/MAPS.md) 管生成与碰撞 |
| 怪物 | `PopulationPlan → pendingSpawns → 房间激活 → Enemy` | 计划不分配实体 ID；远处已生成状态保留并休眠；[生成规则](../gameplay/world/POPULATION.md) 与 [单位基线](../modules/UNITS.md) |
| 技能 | `MPQ 定义 → 来源等级／加成 → resolveSkill → SkillRuntime → 世界端口` | 执行不查询完整人物或怪物；[技能基线](../modules/SKILL_RUNTIME.md) 管参数与端口，[技能规则](../gameplay/skills/COMMON.md) 管效果 |
| 物品 | `TC／品质规划 → ItemInstance → InventoryService 事务` | 真实身份决定掉落；实例位置唯一；[原表](../gameplay/items/DATA.md)、[容器模型](../gameplay/items/MODEL.md)、[奖励基线](../modules/REWARDS.md) |
| 客户端 | `权威状态 → Local*Client → 值投影 → UI；意图反向提交` | UI 不拥有权威可写状态；场景兼容查询尚未全部迁移，见 [客户端基线](../modules/CLIENT.md) |
| 联网 | `登录／角色／房间 UI 意图 → app → RealmSession；TCP → SID / MCP / D2GS → OnlineView / 有序 GamePacket` | 本轮补注册／建角／删角／Join，未构建；此前 Windows 包内双账号初始化、心跳与退局有限冒烟通过；UI 只读视图，当前显式丢弃世界包并停在交接页。原版文件只读认证，不进入本地玩法／存档，见 [联网基线](../modules/NETWORK.md) |
| 角色保存 | `characterSave → 原 D2S v96 → decode / 校验 → 新局 restore` | 角色和最早非空装备尸体可持久化；怪物、弹体和原局 AI 不保存；格式及拒绝边界见 [存档](../modules/SAVES.md) |
| 地图探索 | `当前可见 DT1 格 → 客户端揭示 → .d2xmap` | 与 D2S 同名边车文件；不增加 D2S 私有字段，见 [自动地图](../gameplay/world/AUTOMAP.md) |

规则／内容指纹用于运行时校验或相关边车数据，不作为私有字段写入原 D2S。恢复角色必须重新计算装备和派生属性，不能恢复临时库存访问授权、GPU 缓存或旧世界。

单机恢复会重建世界；联网底层使用独立连接／入局代次，不调用本地 restore。实际联网场景尚未实现，既有私服路线及验收条件见 [D2GS 接入计划](MULTIPLAYER.md)。
