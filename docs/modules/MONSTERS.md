# 怪物模块入口与所有权

更新：2026-10-09。本页只维护代码入口、状态归属与模块协作；规则、按幕执行、原包表现及历史证据统一进入[怪物目录](../gameplay/monsters/README.md)。客户端消费所连接服务端的公开事实，不执行人口、AI、命中、复活或奖励。

## 服务端与内容入口

| 入口 | 职责 |
| --- | --- |
| [monster_catalog.cpp](../../src/content/monsters/monster_catalog.cpp)及content/monsters | 当前MonStats／MonStats2、AI、动作、数值、词缀／经验／掉落适配 |
| [population.cpp](../../src/world/population.cpp) | hosting与资源工具报告共用人口计划，客户端不据此出生 |
| [combat_content.cpp](../../src/hosting/combat_content.cpp) | 人口／战斗规则、准入／替身与组件准备，不持活动AI |
| [monster_actions_content.cpp](../../src/hosting/monster_actions_content.cpp) | 攻击／技能／弹体与原事件帧准备，内核不读MPQ |
| [population/system.cpp](../../src/server/systems/population/system.cpp) | 区域代次与spawn key准入，不因离开视野重生 |
| [monsters/system.cpp](../../src/server/systems/monsters/system.cpp)、[special_actions.cpp](../../src/server/systems/monsters/special_actions.cpp)、[controlled.cpp](../../src/server/systems/monsters/controlled.cpp) | 活动实体、生命、组件、位置／路线、版本、home／节点、复活／出生／死亡连锁及伙伴实体 |
| [ai/system.cpp](../../src/server/systems/ai/system.cpp)及family_actions／nest_family／unique_actions | 目标、决策时钟与随机，调用纯策略并提交movement／skills窄请求 |
| [gameplay/monsters](../../src/gameplay/monsters) | 家族／首领纯决策、方向、碰撞、速度、伤害及组件计算 |
| [skills/monster_actions.cpp](../../src/server/systems/skills/monster_actions.cpp) | 开始／释放、MonSeq多次事件及目标／中断／代次复验 |
| [missiles/monster.cpp](../../src/server/systems/missiles/monster.cpp) | 权威弹体、接触及多发／地面效果 |
| [effects/monster_enchantments.cpp](../../src/server/systems/effects/monster_enchantments.cpp)、[monster_periodic.cpp](../../src/server/systems/effects/monster_periodic.cpp) | 精英光环／诅咒／受击／死亡与周期效果时钟 |
| [monster_replication.cpp](../../src/hosting/detail/monster_replication.cpp) | 公开事实与状态的原包编码，不建第二份权威真值 |

combat拥有待命中计划，death／loot／progression分别提交死亡、掉落和经验；quests消费真实身份／出生来源与死亡事实。companions拥有主人与控制关系，monsters拥有活动实体。固定步、事务／可靠输出和调度统一见[SERVER_SYSTEMS](SERVER_SYSTEMS.md)，不将状态集中回会话。

Actor的damageable／combatCompanion／amazonAttacker／standardAttackSource集中表达当前受伤、战斗伙伴和普通攻击入口能力，由monsters目标／伤害、skills怪物施法及combat命中复用。它们不包括生命、区域、距离、视线、动作或所有权授权，调用者仍逐次复验这些条件；不是完整阵营／PvP接口。Hydra保留专用施放和不可受伤路径，诱饵可受伤但不进入普通攻击，女武神和佣兵保留各自装备／AI规则。静态城镇NPC仍归AreaStore与npc服务，不因type1相同迁入敌怪AI；中立不可替换敌人。

## 客户端入口

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | type1 GUID／classId、位置／模式／动作／生命／状态及移除副本 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 唯一已知阵营、技能与尸体目标资格，monsterTargetEligible共用 |
| [remote_scene.cpp](../../src/presentation/remote/remote_scene.cpp) | 原动作／技能／状态的动画、视觉弹体及声音 |
| [monster_effects.cpp](../../src/presentation/remote/monster_effects.cpp) | 原电／冰强化公开回调与显示时钟，不执行伤害 |
| [actor_animation.cpp](../../src/presentation/actors/actor_animation.cpp) | 原COF／DCC／AnimData／MonSeq、组件与动作时钟 |
| [sound_catalog.cpp](../../src/content/audio/sound_catalog.cpp)、[scene_audio.cpp](../../src/presentation/audio/scene_audio.cpp) | 唯一MonSounds解释与播放 |

RemoteCombat核对真实身份、原alignment及MPQ资格，RemoteScene不另推断敌我。onlineMonsterCorpse统一死亡模式与剥离触发位的生命刻度，供声音、弹体接触、光照和NPC读取。原服／自研使用同一客户端；修复与共享提取须有原版依据。

RemoteCombat内部以Unknown／Hostile／NonHostile区分信息缺失与已知非敌对，保留hostileSource只读接口供表现使用。友方技能只接受明确NonHostile的type1单位；不存在MonStats身份或状态未解码不能因hostileSource=false而获准。原宠物归属和已知NPC／中立表记录仍沿既有优先级处理。实际关系与未实现PvP见[阵营](../gameplay/combat/FACTIONS.md)。

## 修改与文档归属

规则准备、生命周期与迁入更新[COMMON](../gameplay/monsters/COMMON.md)；类型／AI覆盖更新所属幕；精英和专用策略分别更新ELITES／BOSSES；出生／房间更新POPULATION，原包／资源更新PRESENTATION。技能算法、任务、佣兵与掉落仍归原专题，入口见目录。

当前按幕缺口、替身适用范围和历史有限证据由专题维护，不在模块页复制完成清单。准入成功、任务完成或资源可读不等于真实怪物战斗验收；旧映射／旧单机实现不恢复客户端执行器。
