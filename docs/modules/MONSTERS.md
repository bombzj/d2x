# 怪物身份、服务端行为与原服表现

怪物由所连接的服务端分配；客户端不调用人口计划生成单位，不运行 AI、攻击／死亡奖励或复活结算器。第一幕接入范围见[怪物表现](../gameplay/world/MONSTERS.md)。

## 入口与所有权

| 入口 | 职责 |
| --- | --- |
| [remote_world.cpp](../../src/client/remote_world.cpp) | type1 GUID／classId、位置／模式／动作／生命／状态及移除回包 |
| [monster_catalog.cpp](../../src/content/monsters/monster_catalog.cpp) | 真实 MonStats → MonStats2，身份、尺寸、组件、技能／原图定义 |
| [remote_combat.cpp](../../src/client/remote_combat.cpp) | 唯一敌我、状态与尸体目标资格；命中选择／锁定和请求发送共用 monsterTargetEligible |
| [remote_scene.cpp](../../src/presentation/remote/remote_scene.cpp) | 原动作／技能及状态转换成公共动画、显示路径、弹体与声音事件 |
| [actor_animation.cpp](../../src/presentation/actors/actor_animation.cpp) | 原 COF／DCC／AnimData／MonSeq、变换、动作时钟 |
| [sound_catalog.cpp](../../src/content/audio/sound_catalog.cpp)、[scene_audio.cpp](../../src/presentation/audio/scene_audio.cpp) | 唯一 MonSounds 定义解释与公共声音播放规则 |
| [population.cpp](../../src/world/population.cpp) | 资源工具报告及自研宿主人口准备共用；客户端不调用它出生怪物 |

真实身份和外观分别建模。未支持的敌对怪物允许用沉沦魔外观并保留真实身份；中立 NPC／友军／佣兵／宠物不能冒充敌人。是否敌对、可选尸体及技能目标资格由 RemoteCombat 核对 MPQ 和原服 alignment／状态，不靠外观推断；RemoteScene 不再另行解释敌我规则。onlineMonsterCorpse 统一原死亡模式和剥离 rank 标志后的生命刻度，供声音、弹体接触、光照和 NPC 交互读取。

## 自研服务端普通近战

普通阶级Fallen／Zombie／Skeleton／Brute已有有限物理近战。新迁入CorruptRogue、Goatman、CorruptLancer案例，优先沿用master的家族决策、持续接近与速度规则；原MPQ准备参数和动作，不改客户端输入／原包消费者。`gameplay/monsters/melee_decision`只算下一动作，server/ai持有目标／等待／随机，monsters持有实体／路线，skills／combat／death负责攻击／伤害／经验。

实际准入、速度修正、限制及有限运行范围见[Act 1案例](SERVER_SYSTEMS.md#act-1普通怪物案例)。远程、萨满复活、精英／首领及全部Act 1行为未完成。技能／NPC／任务如何共用基础函数，及哪些只能在服务端执行，见[参考设计](../architecture/REFERENCE_DESIGN.md)。

## 生命周期与限制

原移动、攻击、受伤、死亡、复活及单位移除驱动表现。0x0C剥离暗金标志后解释生命刻度；原 hide／udead／corpseSel 分别控制尸体显示与选择。弹体出生与ClientSend按原程序决定，动作派生与原0x73同步去重，规则见攻击专题。换区／移除清理相应显示缓存。

怪物位置／死亡沿当前原包消费者投影；所有击杀、经验、掉落与任务结算由所连接的权威服务端决定，自研缺口见上述切片。旧家族AI／内部存档版本记录不代表当前实现。

完整随机精英名称／染色、死亡专用演出、客户端程序、状态速度和全部随机出生／战斗分支未认证。人口规则作为工具数据说明见[人口报告](../gameplay/world/POPULATION.md)，来源及许可见[资料来源](../resources/THIRD_PARTY.md)。
